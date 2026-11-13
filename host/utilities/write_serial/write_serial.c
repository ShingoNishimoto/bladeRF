/*
 * Write a serial number into the bladeRF OTP flash.
 *
 * The OTP stores key/value pairs in "binkv" encoding:
 *   [1-byte total-len][key chars][value chars][2-byte LE CRC]
 * Serial number is stored under key "S", must be exactly 32 hex chars.
 *
 * Build:
 *   make                     (standalone, see the Makefile in this directory)
 * or, in-tree, as part of the host/ CMake build.
 *
 * Usage:
 *   sudo ./write_serial <32-hex-digit serial>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <libbladeRF.h>

#define OTP_SIZE          256
#define SERIAL_KEY        "S"
#define SERIAL_KEY_LEN    1
#define SERIAL_VAL_LEN    32   /* BLADERF_SERIAL_LENGTH - 1 */

/* CRC-16 with polynomial 0x1021 and a zero initial value (the "XMODEM"
 * variant -- note this is NOT CCITT-FALSE, which seeds with 0xffff).
 * This is a reimplementation of zcrc() from firmware_common/misc.h, which is
 * what libbladeRF's binkv_encode_field()/binkv_decode_field() use. */
static uint16_t zcrc(const uint8_t *buf, int len)
{
    uint16_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

/* Encode one binkv field into buf starting at *idx */
static int binkv_encode(char *buf, int buf_len, int *idx,
                        const char *key, const char *val)
{
    int klen = (int)strlen(key);
    int vlen = (int)strlen(val);
    int tlen = klen + vlen + 1;   /* length byte + key + val */

    if (tlen >= 256 || *idx + tlen + 2 > buf_len)
        return -1;

    buf[*idx] = (char)(klen + vlen);
    memcpy(buf + *idx + 1, key, klen);
    memcpy(buf + *idx + 1 + klen, val, vlen);
    uint16_t crc = zcrc((uint8_t *)(buf + *idx), tlen);
    buf[*idx + tlen]     = (char)(crc & 0xff);
    buf[*idx + tlen + 1] = (char)(crc >> 8);
    *idx += tlen + 2;
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <32-hex-digit-serial>\n", argv[0]);
        fprintf(stderr, "Example: %s 0123456789abcdef0123456789abcdef\n", argv[0]);
        return 1;
    }

    const char *serial = argv[1];
    if (strlen(serial) != SERIAL_VAL_LEN) {
        fprintf(stderr, "Error: serial must be exactly %d hex characters\n",
                SERIAL_VAL_LEN);
        return 1;
    }
    for (int i = 0; i < SERIAL_VAL_LEN; i++) {
        if (!isxdigit((unsigned char)serial[i])) {
            fprintf(stderr, "Error: non-hex character '%c' at position %d\n",
                    serial[i], i);
            return 1;
        }
    }

    struct bladerf *dev = NULL;
    int status;

    status = bladerf_open(&dev, NULL);
    if (status < 0) {
        fprintf(stderr, "Failed to open device: %s\n", bladerf_strerror(status));
        return 1;
    }

    /* Read current OTP so we can preserve any existing fields */
    uint8_t otp[OTP_SIZE];
    memset(otp, 0xff, sizeof(otp));

    status = bladerf_read_otp(dev, otp);
    if (status < 0) {
        fprintf(stderr, "Failed to read OTP: %s\n", bladerf_strerror(status));
        bladerf_close(dev);
        return 1;
    }

    /* Show current OTP contents (first 64 bytes for sanity check) */
    printf("Current OTP (first 64 bytes):\n");
    for (int i = 0; i < 64; i++) {
        printf("%02x ", otp[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    /* Build a new OTP buffer with the serial number as the first field.
     * We keep all existing fields that are NOT "S" to preserve calibration data. */
    uint8_t new_otp[OTP_SIZE];
    memset(new_otp, 0xff, sizeof(new_otp));
    int idx = 0;

    /* Write serial ("S") first */
    if (binkv_encode((char *)new_otp, OTP_SIZE, &idx, SERIAL_KEY, serial) < 0) {
        fprintf(stderr, "Error: failed to encode serial field\n");
        bladerf_close(dev);
        return 1;
    }

    /* Copy any non-"S" fields from the old OTP */
    const uint8_t *p = otp;
    const uint8_t *end = otp + OTP_SIZE;
    while (p < end && *p != 0xff) {
        uint8_t flen = *p;
        /* key is the character(s) right after the length byte */
        if (flen >= 1 && !(flen == 1 + SERIAL_VAL_LEN &&
                           memcmp(p + 1, SERIAL_KEY, SERIAL_KEY_LEN) == 0)) {
            int entry_size = flen + 3; /* len byte + data + 2 crc bytes */
            if (idx + entry_size > OTP_SIZE) {
                fprintf(stderr, "Warning: OTP full, some fields not copied\n");
                break;
            }
            memcpy(new_otp + idx, p, entry_size);
            idx += entry_size;
        }
        p += flen + 3;
    }

    printf("Writing serial '%s' to OTP...\n", serial);
    status = bladerf_write_otp(dev, new_otp);
    if (status < 0) {
        fprintf(stderr, "Failed to write OTP: %s\n", bladerf_strerror(status));
        bladerf_close(dev);
        return 1;
    }

    /* Verify by reading back */
    uint8_t verify[OTP_SIZE];
    memset(verify, 0xff, sizeof(verify));
    status = bladerf_read_otp(dev, verify);
    if (status < 0) {
        fprintf(stderr, "Failed to read back OTP: %s\n", bladerf_strerror(status));
        bladerf_close(dev);
        return 1;
    }

    if (memcmp(new_otp, verify, OTP_SIZE) != 0) {
        fprintf(stderr, "Warning: OTP readback mismatch — verify manually\n");
    } else {
        printf("OTP verified successfully.\n");
    }

    printf("\nPlease power-cycle (replug) the board and run 'bladeRF-cli -i' -> 'info' to confirm.\n");

    bladerf_close(dev);
    return 0;
}
