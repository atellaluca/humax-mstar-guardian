# Architecture and safety model

## Goal

The project deliberately separates the operator interface from the target UART. The host cannot directly type arbitrary MBoot commands. Every target-bound command crosses the ESP32 guardian and must exactly match an allowlisted entry.

## Layers

1. **Target** — MStar-based embedded receiver exposing a 115200 8N1 UART.
2. **Guardian** — ESP32-WROOM-32 controlling UART forwarding decisions and a power relay.
3. **Host** — Python application responsible for lifecycle, logs and user interaction.

## Boot interception

On the reference target, `bootdelay=0` made normal interactive interruption impractical. During experimentation, the string `Changelist` was found to occur immediately before the relevant transition. The guardian watches the target stream locally and emits one CR when this marker is observed while armed.

The behavior is intentionally target-specific and should not be assumed to work on other firmware versions.

## Deny-by-default

There is no generic target passthrough. `@EXEC` requests are accepted only when the complete command is an exact match for an entry compiled into the ESP32 firmware.

The public allowlist contains no SPI erase/write operation.

Capabilities are classified as:

- `INSPECT`: metadata/status queries and USB enumeration.
- `READ_DEVICE`: target flash-to-RAM reads and checksums.
- `READ_USB`: FAT/USB reads and checksums.
- `WRITE_USB`: export of an already-read RAM buffer to removable USB storage.

The last class is intentionally distinguished from read-only operations. It writes the removable USB filesystem, not the target SPI flash.

## Fail-safe power handling

The relay is active LOW. ESP32 startup explicitly drives it to OFF. The host attempts `DISARM` and `OFF` during all normal and exceptional shutdown paths.

This reduces accidental continued target operation, but it is not a substitute for hardware electrical safety or independent recovery procedures.
