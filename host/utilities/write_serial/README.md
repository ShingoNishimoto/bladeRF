# write_serial

A standalone utility that burns a serial number into the OTP (One-Time
Programmable) flash of a bladeRF-compatible board.

It exists because `bladeRF-cli` offers no way to *write* a serial number — the
stock tooling assumes the serial was programmed at the factory. On a
self-manufactured board (same configuration as a bladeRF 2.0 micro), the OTP
ships blank, so the serial has to be written once during bring-up.

> **New board?** Writing the serial is only one step. See
> [BRINGUP.md](BRINGUP.md) for the full procedure, including the FX3 firmware,
> the FPGA bitstream, and the libbladeRF patch a blank board needs in order to
> be opened at all.

## Background: how the serial is stored

The OTP is a 256-byte region in the board's SPI flash. It holds device-specific
key/value pairs in Nuand's "binkv" encoding — a flat sequence of records:

```
+-----+----------+------------+---------------+
| len |   key    |   value    |  CRC-16 (LE)  |
+-----+----------+------------+---------------+
  1 B   variable   variable         2 B
```

- `len` is a single byte holding `strlen(key) + strlen(value)` — it does **not**
  include itself or the CRC. A record therefore occupies `len + 3` bytes.
- `CRC-16` covers the length byte plus the key and value (`len + 1` bytes),
  using polynomial `0x1021` with a **zero** initial value, stored
  little-endian. This is the XMODEM variant, not CCITT-FALSE.
- Unwritten flash reads back as `0xff`, so a `0xff` length byte terminates the
  record list.

The serial number lives under key `"S"` and must be exactly 32 hex characters
(`BLADERF_SERIAL_LENGTH - 1`). A serial record is thus `len = 33`, 36 bytes
total.

`write_serial` reimplements both halves of this format rather than linking
libbladeRF's internals, because `binkv_encode_field()` and `zcrc()` are private
to the library. The reference implementations it mirrors are:

| `write_serial` | Reference in this repository |
| --- | --- |
| `zcrc()` | [`firmware_common/misc.h`](../../../firmware_common/misc.h) |
| `binkv_encode()` | `binkv_encode_field()` in [`host/libraries/libbladeRF/src/board/bladerf1/flash.c`](../../libraries/libbladeRF/src/board/bladerf1/flash.c) |

The read path that consumes what this tool writes is
`spi_flash_read_otp(dev, "S", ...)` in the same `flash.c`.

## What the tool does

1. Validates that the argument is exactly 32 hexadecimal characters.
2. Opens the first available device (`bladerf_open(&dev, NULL)`).
3. Reads the current 256-byte OTP and prints the first 64 bytes as a hex dump,
   so you can see what was there before.
4. Builds a new OTP image with the serial record first, then **copies forward
   every existing record whose key is not `"S"`**. This preserves factory
   calibration fields such as the VCTCXO DAC trim.
5. Writes the image with `bladerf_write_otp()`.
6. Reads the OTP back and `memcmp`s it against what was written, reporting
   either `OTP verified successfully.` or a mismatch warning.
7. Reminds you to power-cycle the board, because the serial is only picked up
   when libbladeRF next enumerates the device.

It deliberately does **not** call `bladerf_lock_otp()`. Locking the OTP region
is irreversible and would foreclose any future attempt to touch it, so the
region is left unlocked.

## ⚠️ OTP flash is write-once

This is the most important thing to understand before running the tool.

The OTP region is programmed bit-by-bit: a write can only flip bits from `1` to
`0`, and there is no erase operation exposed through this path. In practice:

- **On a blank board** (OTP all `0xff`) the write succeeds and verifies.
- **On a board that already has a serial**, re-running the tool with a
  *different* serial will not replace the old one. The hardware ANDs the new
  bytes into the existing ones, producing a record whose CRC no longer
  validates. The readback verification will fail and the board may end up with
  no readable serial at all.

So: **double-check the serial string before you press enter.** Step 3's hex
dump is your safety net — if the first bytes are not `ff ff ff ...`, the OTP has
already been written and you should stop.

## Prerequisites

- A bladeRF-compatible device connected via USB, with working FX3 firmware (the
  board must appear in `bladeRF-cli -p`).
- `libbladeRF` installed — headers and shared library.
  - Built from this repository: `sudo make install && sudo ldconfig` from
    `host/build`, which installs to `/usr/local`.
  - Or from packages: `libbladerf-dev` on Debian/Ubuntu.

## Build

### Standalone (quickest)

```sh
make
```

The `Makefile` uses `pkg-config --cflags --libs libbladeRF` when available and
otherwise falls back to `-I/usr/local/include -L/usr/local/lib -lbladeRF`,
which matches a `make install` from this repository's `host/build`.

The equivalent one-liner, if you would rather not use the Makefile:

```sh
gcc -o write_serial write_serial.c -I/usr/local/include -L/usr/local/lib -lbladeRF
```

### In-tree, with the rest of the host tools

`write_serial` is registered in `host/utilities/CMakeLists.txt`, so a normal
host build produces it alongside `bladeRF-cli`:

```sh
cd host
mkdir -p build && cd build
cmake ..
make -j$(nproc)
sudo make install && sudo ldconfig
```

This is the preferred route on a self-manufactured board, because the same
build also installs the patched `libbladeRF` described in [BRINGUP.md](BRINGUP.md).

## Usage

```
sudo ./write_serial <32-hex-digit-serial>
```

`sudo` is required unless the bladeRF udev rules are installed (a
`sudo make install` of this repository does that on Linux) *and* you are in the
`plugdev` group.

The serial must be exactly 32 hexadecimal characters (`0-9`, `a-f`, `A-F`).
Lower case is conventional, since that is how libbladeRF prints it back.

### Example

```sh
sudo ./write_serial cafe0700000000000000000000000000
```

Expected output:

```
Current OTP (first 64 bytes):
ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
...

Writing serial 'cafe0700000000000000000000000000' to OTP...
OTP verified successfully.

Please power-cycle (replug) the board and run 'bladeRF-cli -i' -> 'info' to confirm.
```

### Serial numbering convention

A readable convention is a short prefix padded with zeros, e.g.
`cafe05...`, `cafe07...`, `cafe08...` for boards 5, 7 and 8. This keeps the
serials easy to recognise and lets you select a board by prefix (see below).

## Verification

Power-cycle the board first — replug the USB cable, or if you have a Yepkit
YKUSH switchable hub:

```sh
sudo ykushcmd -d 1    # power down port 1
sudo ykushcmd -u 1    # power up port 1
```

Then check the serial was accepted:

```sh
bladeRF-cli -i
```

and inside the CLI:

```
bladeRF> info
```

The `Serial #` field should show the value you wrote.

Once a board has a serial, you can address it specifically — useful when
several boards are attached at once:

```sh
bladeRF-cli -p                           # list all attached devices
bladeRF-cli -d "*:serial=cafe07" -i      # open one board by serial prefix
```

Note the `*:serial=` device-identifier syntax: a bare `bladeRF-cli -d cafe07`
does **not** work.

## Troubleshooting

| Symptom | Likely cause and fix |
| --- | --- |
| `Failed to open device: No devices available` | Board not enumerating. Check `bladeRF-cli -p` and `dmesg`; the FX3 firmware may be missing — see [BRINGUP.md](BRINGUP.md). |
| `Failed to open device` on a brand-new board | libbladeRF aborts when it cannot read the FPGA size from blank flash. Requires the forced-A4 patch in [BRINGUP.md](BRINGUP.md). |
| `Failed to read OTP` / `Failed to write OTP` | Usually a permissions problem — run under `sudo`, or install the udev rules and join the `plugdev` group. |
| `Warning: OTP readback mismatch` | The OTP already contained data. Because OTP bits only go `1`→`0`, the record is now likely corrupt. Dump the OTP and assess manually. |
| Serial still blank after writing | The board was not power-cycled. The serial is read during enumeration only. |
| `Error: non-hex character ... at position N` | Typo in the serial, or a stray shell character. |

## Files

| File | Purpose |
| --- | --- |
| `write_serial.c` | The whole tool — ~170 lines, three functions. |
| `Makefile` | Standalone build against an installed libbladeRF. |
| `CMakeLists.txt` | In-tree build, wired into `host/utilities/CMakeLists.txt`. |
| `README.md` | This document. |
| `BRINGUP.md` | Full bring-up procedure for a new self-manufactured board. |
