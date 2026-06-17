# write_serial

A standalone utility that burns a serial number into the bladeRF's One-Time
Programmable (OTP) flash.

## Background

The OTP flash stores device-specific key/value pairs in "binkv" encoding.  The
serial number is stored under key `"S"` and must be exactly 32 lower-case hex
characters (matching `BLADERF_SERIAL_LENGTH - 1`).  `write_serial` reads the
current OTP contents, replaces any existing `"S"` field, and preserves all
other fields (e.g. calibration data) before writing the new image back.

**Warning:** OTP flash is write-once per bit.  A write cannot be fully undone;
only bits that are still `0xff` can be changed.  Double-check your serial string
before running the tool.

## Prerequisites

- A bladeRF device connected via USB
- `libbladeRF` installed (headers + shared library)
  - If built from this repository: install via `make install` under `host/`
  - Package managers: `libbladerf-dev` (Debian/Ubuntu) or equivalent

## Build

```sh
gcc -o write_serial write_serial.c \
    -I/usr/local/include \
    -L/usr/local/lib \
    -lbladeRF
```

If `libbladeRF` is installed system-wide (e.g. via a package manager), the
`-I` / `-L` flags can usually be omitted:

```sh
gcc -o write_serial write_serial.c -lbladeRF
```

Use `pkg-config` when available for correct flags:

```sh
gcc -o write_serial write_serial.c $(pkg-config --cflags --libs libbladeRF)
```

## Usage

```
sudo ./write_serial <32-hex-digit-serial>
```

The serial must be exactly 32 hexadecimal characters (digits `0–9`, letters
`a–f` / `A–F`).

### Example

```sh
sudo ./write_serial 0123456789abcdef0123456789abcdef
```

The tool will:

1. Print the current OTP contents (first 64 bytes) for a sanity check.
2. Write the new serial to OTP.
3. Read back and verify the written data.
4. Prompt you to power-cycle the board.

### Confirming the serial after power-cycle

After replugging the device, verify the serial was accepted:

```sh
bladeRF-cli -i
```

Then inside the CLI:

```
bladeRF> info
```

The `Serial #` field should reflect the value you wrote.
