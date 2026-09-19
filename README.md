<p align="center">
  <img src="assets/humax-guardian-banner.png"
       alt="HumaxGuardian — Embedded UART Reverse Engineering"
       width="100%">
</p>

# HumaxGuardian

ESP32-based UART guardian and Python tooling for safe, controlled
reverse engineering of MStar MBoot embedded systems.

Fail-safe UART research tooling developed during a firmware reverse-engineering
case study on a Humax/Changhong DVB-T receiver based on an MStar MBoot/U-Boot
platform.

The project uses an ESP32 between the target UART and the host computer as a
**command guardian**, rather than exposing the bootloader through a transparent
serial bridge.

## What it does

- Controls target power through a relay.
- Watches the boot UART locally.
- Sends a single CR when the observed `Changelist` marker appears, allowing access to the MBoot prompt on the researched target.
- Detects the `k5tn#` console prompt.
- Uses deny-by-default command allowlisting on the ESP32.
- Separates inspection, device-read, USB-read and USB-write capabilities.
- Logs both text and raw UART traffic on the host.
- Powers the target off on normal exit, Ctrl+C, or serial errors.

## Architecture

```text
Target UART (115200 8N1)
        |
        v
ESP32-WROOM-32
  - RX2 GPIO16
  - TX2 GPIO17
  - relay GPIO25 (active LOW)
  - guardian / allowlist
        |
        | USB serial @ 921600
        v
Host Python console
  - lifecycle control
  - logging
  - operator UI
```

## Important design rule

The ESP32 is the security boundary. The host script does not provide an unrestricted pass-through to the target console. Commands reaching the target must be explicitly allowed by the firmware.

No SPI erase/write command is included in the allowlist.

One capability (`fatwrite`) writes the already-read RAM buffer to a USB filesystem. It is therefore classified separately as `WRITE_USB`; it does **not** write the target SPI flash.

## Reference target

The addresses and command behavior in this repository are target-specific and were validated only on the researched device/build.

Observed reference characteristics:

- MStar MBoot / U-Boot 2011.06 derivative
- target UART: 115200 8N1
- 256 MiB RAM
- SPI NOR: 8 MiB (`0x00800000`)
- SPI CRC32 observed during acquisition: `89BFAD81`
- verified dump SHA-256: `de7b98ffea94c77f15e12c8e644d52586ba6dff24c5ae3f08108f9f1ed252e64`

The proprietary firmware image is **not distributed** with this repository.

## Repository layout

```text
esp32/humax_guardian.ino   ESP32 guardian firmware
host/humax_console.py      Host console/logger

docs/architecture.md      Design and safety model
docs/protocol.md          Host <-> guardian protocol
```

## Hardware wiring used during the case study

| Signal | ESP32 |
|---|---:|
| Target TX -> ESP32 RX | GPIO16 |
| Target RX <- ESP32 TX | GPIO17 |
| Relay IN | GPIO25 |
| Ground | Common GND |

The UART used by the reference target was 3.3 V logic. Verify voltage levels on your own hardware before connecting anything.

## Host requirements

- Python 3.10+
- `pyserial`

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python host/humax_console.py --port /dev/cu.usbserial-0001
```

Close any Arduino Serial Monitor or other program holding the serial port before starting the host tool.

## Command model

The public release deliberately uses a small exact-match allowlist. `help <topic>` is not accepted generically.

The acquisition-related commands retained here correspond to the reference target and are documented as research artifacts. Review the source before using them on different hardware.

## Firmware acquisition result from the case study

The original investigation ultimately used the bootloader's SPI-to-DRAM primitive and its FAT support to acquire the complete 8 MiB SPI image. Integrity was checked by reading the resulting file back into a second RAM region and comparing CRC32 values.

```text
SPI -> DRAM -> CRC32
            -> FAT/USB -> DRAM -> CRC32

SPI buffer CRC32 : 89BFAD81
USB reload CRC32 : 89BFAD81
```

The repository intentionally does not include the resulting proprietary `firmware.bin`.

## Scope

This is research tooling and documentation from a specific embedded-system case study, not a universal Humax service utility. MBoot commands, memory addresses and boot behavior can differ between devices and firmware versions.

## License

The original source code and documentation in this repository are licensed under the MIT License. This license does not apply to third-party firmware, bootloader code, trademarks or other proprietary material.
