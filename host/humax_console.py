#!/usr/bin/env python3
"""Host console for the Humax/MStar ESP32 guardian.

The ESP32 remains the authoritative command allowlist. This program manages
session lifecycle, logging and operator input; it is not a transparent UART
bridge.
"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

import serial

PROMPT = b"k5tn#"
DEFAULT_PORT = "/dev/cu.usbserial-0001"
DEFAULT_BAUD = 921600

# UI hints only. Security enforcement lives on the ESP32.
DISPLAY_COMMANDS = (
    "help", "?", "version", "printenv", "bdinfo", "coninfo", "mversion",
    "showversion", "systeminfo_print", "loaderinfo_print", "read_boot_info",
    "help spi", "help crc32", "help fatwrite", "help fatload",
    "usb start", "usb start 0", "usb start 1", "usb tree", "usb info",
    "usb storage", "usb dev", "usb dev 0", "usb part",
    "fatinfo usb 0", "fatinfo usb 0:1", "fatls usb 0", "fatls usb 0:1",
    "spi info",
    "spi rdc 0x81000000 0x00000000 0x00800000",
    "crc32 0x81000000 0x00800000",
    "fatwrite usb 0 0x81000000 firmware.bin 0x00800000",
    "fatload usb 0:1 0x82000000 firmware.bin",
    "crc32 0x82000000 0x00800000",
)


@dataclass
class Config:
    port: str
    baud: int
    off_time: float
    boot_timeout: float
    log_dir: Path


class HumaxSession:
    def __init__(self, cfg: Config):
        self.cfg = cfg
        self.ser: serial.Serial | None = None
        self.textlog = None
        self.rawlog = None
        self.t0 = time.monotonic()
        self.console_ready = False
        self.rx_tail = b""

    def elapsed(self) -> float:
        return time.monotonic() - self.t0

    def event(self, message: str) -> None:
        line = f"[{self.elapsed():10.6f}] [HOST] {message}"
        print("\n" + line, flush=True)
        if self.textlog:
            self.textlog.write(line + "\n")
            self.textlog.flush()

    def open(self) -> None:
        self.cfg.log_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.text_path = self.cfg.log_dir / f"humax_{stamp}.log"
        self.raw_path = self.cfg.log_dir / f"humax_{stamp}.bin"
        self.textlog = self.text_path.open("w", encoding="utf-8", errors="replace")
        self.rawlog = self.raw_path.open("wb")
        self.ser = serial.Serial(
            self.cfg.port, self.cfg.baud,
            bytesize=8, parity="N", stopbits=1,
            timeout=0, write_timeout=1,
            xonxoff=False, rtscts=False, dsrdtr=False,
        )
        self.event("SERIALE APERTA - " + datetime.now().isoformat(timespec="seconds"))

    def guardian(self, command: str) -> None:
        if not self.ser or not self.ser.is_open:
            raise RuntimeError("serial port is not open")
        self.ser.write(f"@{command}\n".encode("ascii"))
        self.ser.flush()
        self.event(f"ESP32 <- @{command}")

    def read_available(self) -> bool:
        assert self.ser is not None
        n = self.ser.in_waiting
        if n <= 0:
            return False
        data = self.ser.read(min(n, 4096))
        if not data:
            return False
        self.rawlog.write(data)
        self.rawlog.flush()
        text = data.decode("utf-8", errors="replace")
        sys.stdout.write(text)
        sys.stdout.flush()
        self.textlog.write(text)
        self.textlog.flush()
        self.rx_tail = (self.rx_tail + data)[-131072:]
        if PROMPT in self.rx_tail or b"@@CONSOLE:READY:k5tn#" in self.rx_tail:
            self.console_ready = True
        return True

    def safe_off(self) -> None:
        if not self.ser or not self.ser.is_open:
            return
        try:
            self.guardian("DISARM")
            time.sleep(0.1)
            self.guardian("OFF")
            time.sleep(0.25)
            self.event("TARGET OFF")
        except Exception as exc:
            print(f"\n[HOST] fail-safe OFF error: {exc}")

    def cold_boot_to_console(self) -> None:
        assert self.ser is not None
        time.sleep(1.0)
        self.guardian("DISARM")
        time.sleep(0.1)
        self.guardian("OFF")
        self.event(f"OFF {self.cfg.off_time:.1f}s")
        time.sleep(self.cfg.off_time)
        self.ser.reset_input_buffer()
        self.rx_tail = b""
        self.console_ready = False
        self.guardian("ARM")
        time.sleep(0.15)
        self.guardian("ON")
        self.event("COLD BOOT - attendo k5tn#")
        deadline = time.monotonic() + self.cfg.boot_timeout
        while time.monotonic() < deadline and not self.console_ready:
            self.read_available()
            time.sleep(0.001)
        if not self.console_ready:
            raise TimeoutError("prompt k5tn# non rilevato")
        self.event("CONSOLE k5tn# CONFERMATA")
        self.guardian("DISARM")

    def drain_response(self) -> None:
        assert self.ser is not None
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline:
            if self.read_available():
                deadline = time.monotonic() + 0.5
            time.sleep(0.001)

    def interactive(self) -> None:
        print("\nCONSOLE PRONTA")
        print("ESP32 = autorità della whitelist; il lato host non è una console trasparente.")
        print("Comandi locali: :status  :cr  :off  :quit  :commands")
        while True:
            self.read_available()
            try:
                user = input("guardian> ").strip()
            except EOFError:
                user = ":quit"
            if not user:
                continue
            low = user.lower()
            if low in (":quit", ":off"):
                self.event("Chiusura console richiesta")
                return
            if low == ":status":
                self.guardian("STATUS")
            elif low == ":cr":
                self.guardian("CR")
            elif low == ":commands":
                print("\n".join(f"  {x}" for x in DISPLAY_COMMANDS))
                continue
            else:
                # Intentionally forward the request only to the guardian. The ESP32
                # independently decides whether it is allowed.
                self.guardian("EXEC " + low)
            self.drain_response()

    def close(self) -> None:
        self.safe_off()
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        if self.rawlog:
            self.rawlog.close()
        if self.textlog:
            self.textlog.close()


def parse_args() -> Config:
    p = argparse.ArgumentParser(description="Humax/MStar ESP32 guardian host console")
    p.add_argument("--port", default=DEFAULT_PORT)
    p.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    p.add_argument("--off-time", type=float, default=5.0)
    p.add_argument("--boot-timeout", type=float, default=20.0)
    p.add_argument("--log-dir", type=Path, default=Path("logs"))
    a = p.parse_args()
    return Config(a.port, a.baud, a.off_time, a.boot_timeout, a.log_dir)


def main() -> int:
    session = HumaxSession(parse_args())
    print("=" * 72)
    print(" HUMAX / MSTAR GUARDIAN HOST")
    print("=" * 72)
    try:
        session.open()
        session.cold_boot_to_console()
        session.interactive()
        return 0
    except KeyboardInterrupt:
        session.event("CTRL+C - termino")
        return 130
    except (serial.SerialException, TimeoutError, RuntimeError) as exc:
        print(f"\n[HOST] ERRORE: {exc}")
        return 1
    finally:
        session.close()
        print("\nTarget: OFF")
        if hasattr(session, "text_path"):
            print(f"Log: {session.text_path}")
            print(f"RAW: {session.raw_path}")


if __name__ == "__main__":
    raise SystemExit(main())
