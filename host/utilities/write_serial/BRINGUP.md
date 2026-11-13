# Bringing up a self-manufactured bladeRF-compatible board

This is the end-to-end procedure for taking a freshly assembled SSDR board —
same configuration as a bladeRF 2.0 micro xA4 — from "blank flash" to a device
that `bladeRF-cli` and GNSS-SDR can open by serial number.

Everything here is specific to the **`feature/ssdr`** branch of this fork,
which is based on Nuand's **`2021.10`** release tag. A stock upstream
`libbladeRF` will *not* open a blank board; see
[step 2](#2-patch-and-build-libbladerf) for why.

## Why a blank board needs special handling

A factory bladeRF arrives with three things already programmed that a
self-manufactured board does not have:

| Item | Where it lives | Blank-board symptom |
| --- | --- | --- |
| FX3 USB firmware | FX3 I²C EEPROM / SPI flash | Board does not enumerate at all |
| FPGA bitstream | SPI flash | Board enumerates but has no FPGA image to autoload |
| FPGA size + serial + VCTCXO trim | SPI flash OTP / calibration region | `bladerf_open()` fails or the device has no identity |

The last row is the subtle one. During `bladerf2_open()`, libbladeRF calls
`spi_flash_read_fpga_size()`. On blank flash that read fails, and upstream
2021.10 only logs a warning while leaving `fpga_size` unset — which leaves the
device unusable. The fork patches this to fall back to a known size.

## Prerequisites

```sh
sudo apt update
sudo apt install build-essential cmake libusb-1.0-0-dev
```

You will also need these files, which are not in the repository:

| File | What it is | Source |
| --- | --- | --- |
| `bladeRF_fw_v2.4.0.img` | FX3 USB firmware | [nuand.com/fx3](https://www.nuand.com/fx3/) |
| `hostedxA4.rbf` | FPGA bitstream for the 40 kLE (xA4) Artix-7 | [nuand.com/fpga](https://www.nuand.com/fpga/) |

Keep both in a known directory — e.g. `~/work/` — and make sure you grabbed the
**xA4** bitstream. An xA9 image on an xA4 board will not configure.

---

## 1. Clone the fork and check out the SSDR branch

```sh
cd ~/work
git clone https://github.com/ShingoNishimoto/bladeRF.git
cd bladeRF
git remote add upstream https://github.com/Nuand/bladeRF.git   # if not present
git checkout feature/ssdr
git submodule update --init
```

## 2. Patch and build libbladeRF

The `feature/ssdr` branch already carries the required patch, in
[`host/libraries/libbladeRF/src/board/bladerf2/bladerf2.c`](../../libraries/libbladeRF/src/board/bladerf2/bladerf2.c):

```c
status = spi_flash_read_fpga_size(dev, &board_data->fpga_size);
if (status < 0) {
    log_warning("Failed to get FPGA size %s\n", bladerf_strerror(status));
    board_data->fpga_size = BLADERF_FPGA_A4;          /* <-- SSDR patch */
    log_warning("Force FPGA size to be BLADERF_FPGA_A4 %s\n", bladerf_strerror(status));
}
```

**This is what makes a blank board openable.** Without it, every step below
that talks to the device will fail with an open error.

> If your board is *not* an xA4, change `BLADERF_FPGA_A4` to match your part.
> Upstream also honours the `BLADERF_FORCE_FPGA_A9` environment variable, which
> is the cleaner mechanism if A9 is all you need.

The same commit adds a diagnostic print of the VCTCXO DAC trim in
`bladerf2_read_flash_vctcxo_trim()`. On blank flash the trim read fails and
libbladeRF substitutes the default `0x1ffc`, which is fine for bring-up but
means the board is **not frequency-calibrated**. Plan a VCTCXO calibration pass
(e.g. with `kalibrate-bladeRF`) before trusting it for GNSS work.

Build and install:

```sh
cd host
mkdir -p build && cd build
cmake ..
make -j$(nproc)
sudo make install && sudo ldconfig
```

This installs to `/usr/local` — library in `/usr/local/lib`, headers in
`/usr/local/include`, and the tools (`bladeRF-cli`, `write_serial`) in
`/usr/local/bin`.

Confirm you are running the patched build:

```sh
bladeRF-cli --version
```

The version string embeds the git hash of the base commit and is marked
`-dirty` when built from a modified tree, e.g.
`1.8.0-git-87173784-dirty`. If you see a plain distribution version instead,
a packaged `libbladerf` is shadowing your build — remove it or fix your
library path.

`sudo make install` also installs the udev rules (`88-nuand-bladerf1.rules`,
`88-nuand-bladerf2.rules`, `88-nuand-bootloader.rules`) into
`/etc/udev/rules.d` — on Linux `INSTALL_UDEV_RULES` defaults to `ON`. The rules
assign bladeRF devices to the **`plugdev`** group, so to use the board without
`sudo`, add yourself to it and reload:

```sh
sudo usermod -aG plugdev $USER      # log out and back in for this to take effect
sudo udevadm control --reload-rules
sudo udevadm trigger
```

(The group is configurable at configure time with `-DBLADERF_GROUP=<group>`.)

## 3. Load the FX3 USB firmware

If the board does not enumerate at all, the FX3 has no firmware yet and must be
loaded over its USB bootloader — see [Recovery](#recovery-fx3-bootloader) below.

If the board *does* enumerate (even as a bare bootloader device), flash the
firmware with the CLI:

```sh
cd ~/work
bladeRF-cli -f bladeRF_fw_v2.4.0.img
```

Power-cycle the board, then confirm:

```sh
bladeRF-cli -p      # should list the device
bladeRF-cli -i      # opens an interactive session
```

Check `dmesg` if the device does not appear:

```sh
sudo dmesg | tail -30
```

## 4. Write the FPGA bitstream to flash

Two different options, and the distinction matters:

| Command | Effect |
| --- | --- |
| `bladeRF-cli -l hostedxA4.rbf` | **Load** into FPGA RAM — volatile, lost on power cycle. Good for a quick test. |
| `bladeRF-cli -L hostedxA4.rbf` | **Write** to SPI flash — persistent, autoloaded at every power-up. This is what you want. |

```sh
bladeRF-cli -L hostedxA4.rbf
```

Then power-cycle and verify the FPGA autoloads:

```sh
bladeRF-cli -i
```

```
bladeRF> info
```

The `FPGA size` and `FPGA loaded` fields should be populated.

## 5. Write the serial number

Now the board works but has no identity — it reports a blank or garbage serial,
so you cannot address it when more than one board is attached.

```sh
sudo write_serial cafe0700000000000000000000000000
```

Pick a serial following a convention you can recognise — a short board index
padded with zeros to 32 hex characters works well (`cafe05…`, `cafe07…`,
`cafe08…` for boards 5, 7 and 8).

> **⚠️ One attempt per board.** The OTP region is write-once: bits only go
> `1`→`0` and there is no erase. Check the hex dump the tool prints first — if
> it is not all `ff`, stop. See [README.md](README.md#️-otp-flash-is-write-once)
> for the full explanation.

## 6. Power-cycle and verify

The serial is only read during USB enumeration, so a power cycle is mandatory.
Replug the cable, or if you have a Yepkit YKUSH switchable hub:

```sh
sudo ykushcmd -d 1    # power down port 1
sudo ykushcmd -u 1    # power up port 1
```

Then:

```sh
bladeRF-cli -i
```

```
bladeRF> info
```

Check that `Serial #` matches what you wrote, and that the FPGA is loaded.

Finally, confirm the board is individually addressable:

```sh
bladeRF-cli -p                              # list every attached board
bladeRF-cli -d "*:serial=cafe07" -i         # open this board specifically
```

The `*:serial=` prefix is required — a bare `bladeRF-cli -d cafe07` will not
match.

---

## Checklist

For a board to be considered brought up:

- [ ] Appears in `bladeRF-cli -p`
- [ ] FX3 firmware version reported by `info`
- [ ] FPGA autoloads after a power cycle (written with `-L`, not `-l`)
- [ ] `Serial #` matches the intended value
- [ ] Addressable via `bladeRF-cli -d "*:serial=<prefix>" -i`
- [ ] VCTCXO trim calibrated (not the `0x1ffc` fallback) before RF work

## Recovery: FX3 bootloader

If the board does not enumerate as a bladeRF at all, the FX3 must be talked to
directly over its bootloader. This needs Cypress/Infineon tooling, which is not
in this repository:

| Package | Purpose |
| --- | --- |
| `cyusb_linux_1.0.5.tar.gz` | `cyusb_linux` GUI — can download firmware to a bootloader-mode FX3 |
| `ezusbfx3sdk_1.3.5_Linux_x32-x64.tar.gz` | EZ-USB FX3 SDK, including `download_fx3` / `elf2img` |

Both are downloadable from Infineon's EZ-USB FX3 SDK page (registration
required). Extract, then load `bladeRF_fw_v2.4.0.img` to the FX3 in bootloader
mode. Once the firmware is resident, return to [step 3](#3-load-the-fx3-usb-firmware).

Note the host-side architecture when picking binaries:

```sh
lscpu | grep Architecture
```

## Common problems

| Symptom | Cause and fix |
| --- | --- |
| `Failed to open device` on an otherwise-enumerating board | Missing the forced-A4 patch from [step 2](#2-patch-and-build-libbladerf), or a packaged `libbladeRF` shadowing your build. Check `bladeRF-cli --version` for the `-dirty` marker. |
| `Failed to get FPGA size` warning | Expected on blank flash. The patch handles it. Harmless once the bitstream is written with `-L`. |
| FPGA not loaded after power cycle | The bitstream was loaded with `-l` (RAM) instead of `-L` (flash). Re-run with `-L`. |
| Device vanishes from `bladeRF-cli -p` after a firmware write | Normal — it needs a power cycle to re-enumerate on the new firmware. |
| `DAC trim is 1ffc` | The VCTCXO calibration region is blank and the default was substituted. Not fatal; calibrate before RF work. |
| Permission errors reading/writing flash or OTP | Run under `sudo`, or install the udev rules from [step 2](#2-patch-and-build-libbladerf). |
| `bladeRF-cli -d cafe07` finds nothing | Wrong syntax. Use `-d "*:serial=cafe07"`. |

## References

- [`write_serial` README](README.md) — the OTP format and the tool itself
- Nuand FX3 firmware and FPGA images: <https://www.nuand.com/fpga/>
- Upstream bladeRF documentation: <https://github.com/Nuand/bladeRF/wiki>
