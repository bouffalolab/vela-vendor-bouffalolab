#!/usr/bin/env python3
"""Reset a BouffaloLab module into normal boot and verify serial output."""

from __future__ import annotations

import argparse
import array
import fcntl
import json
import os
import re
import select
import sys
import termios
import time
from pathlib import Path


CKLINK_USB_ID = ("42bf", "b210")
CKLINK_RESET_STEPS = (
    (b"BOUFFALOLAB5555DTR1", 0.05),
    (b"BOUFFALOLAB5555RTS1", 0.05),
    (b"BOUFFALOLAB5555RTS0", 0.1),
)
FIRST_BYTE_TIMEOUT_S = 5.0
IDLE_TIMEOUT_S = 0.5
CAPTURE_TIMEOUT_S = 5.0
MAX_CAPTURE_BYTES = 64 * 1024
MIN_LOG_BYTES = 16
MIN_PRINTABLE_RATIO = 0.8
RESULT_PREFIX = "BL_MODULE_RESET_RESULT="
SYS_CLASS_TTY = Path("/sys/class/tty")


class ArgumentError(Exception):
    """Report command-line errors without losing the result record."""


class ResultArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise ArgumentError(message)


def positive_int(value: str) -> int:
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return number


def parse_args() -> argparse.Namespace:
    parser = ResultArgumentParser(
        description="Reset a BouffaloLab module and verify startup serial output."
    )
    parser.add_argument("--port", required=True, help="UART device, such as /dev/ttyUSB2")
    parser.add_argument("--baudrate", required=True, type=positive_int)
    parser.add_argument(
        "--expect",
        action="append",
        default=[],
        help="Required startup marker; may be specified more than once",
    )
    return parser.parse_args()


def emit_result(status: str, **fields: object) -> None:
    result = {"status": status, **fields}
    print(RESULT_PREFIX + json.dumps(result, ensure_ascii=False, sort_keys=True))


def configure_serial(fd: int, baudrate: int) -> None:
    baud = getattr(termios, f"B{baudrate}", None)
    if baud is None:
        raise RuntimeError(f"baudrate {baudrate} is not supported by this host")

    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] &= ~(termios.PARENB | termios.CSTOPB | termios.CSIZE)
    attrs[2] |= termios.CLOCAL | termios.CREAD | termios.CS8
    attrs[2] &= ~getattr(termios, "CRTSCTS", 0)
    attrs[2] &= ~getattr(termios, "HUPCL", 0)
    attrs[3] = 0
    attrs[4] = baud
    attrs[5] = baud
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIFLUSH)


def find_usb_id(port: str) -> tuple[str, str] | None:
    tty_name = Path(os.path.realpath(port)).name
    try:
        device_path = (SYS_CLASS_TTY / tty_name / "device").resolve(strict=True)
    except OSError:
        return None

    for path in (device_path, *device_path.parents):
        try:
            vendor = (path / "idVendor").read_text().strip().lower()
            product = (path / "idProduct").read_text().strip().lower()
        except FileNotFoundError:
            continue
        except OSError:
            return None
        if vendor and product:
            return vendor, product
    return None


def write_all(fd: int, data: bytes) -> None:
    offset = 0
    while offset < len(data):
        written = os.write(fd, data[offset:])
        if written <= 0:
            raise OSError("serial write made no progress")
        offset += written
    termios.tcdrain(fd)


def reset_cklink(fd: int) -> None:
    for command, delay_s in CKLINK_RESET_STEPS:
        write_all(fd, command)
        time.sleep(delay_s)


def set_modem_line(fd: int, flag: int, enabled: bool) -> None:
    bits = array.array("i", [0])
    fcntl.ioctl(fd, termios.TIOCMGET, bits, True)
    if enabled:
        bits[0] |= flag
    else:
        bits[0] &= ~flag
    fcntl.ioctl(fd, termios.TIOCMSET, bits)


def reset_standard(fd: int) -> None:
    # Up/down describe modem-control assertion, while CH340 DTR#/RTS# are active-low.
    set_modem_line(fd, termios.TIOCM_DTR, True)
    time.sleep(0.05)
    set_modem_line(fd, termios.TIOCM_RTS, True)
    time.sleep(0.05)
    set_modem_line(fd, termios.TIOCM_RTS, False)
    time.sleep(0.1)


def reset_module(fd: int, usb_id: tuple[str, str] | None) -> str:
    if usb_id == CKLINK_USB_ID:
        reset_cklink(fd)
        return "cklink-control-string"
    reset_standard(fd)
    return "standard-dtr-rts"


def capture_output(fd: int) -> bytes:
    first_byte_deadline = time.monotonic() + FIRST_BYTE_TIMEOUT_S
    idle_deadline: float | None = None
    capture_deadline: float | None = None
    output = bytearray()

    while len(output) < MAX_CAPTURE_BYTES:
        now = time.monotonic()
        deadline = first_byte_deadline
        if idle_deadline is not None and capture_deadline is not None:
            deadline = min(idle_deadline, capture_deadline)
        remaining = deadline - now
        if remaining <= 0:
            break

        ready, _, _ = select.select([fd], [], [], min(0.2, remaining))
        if not ready:
            continue
        chunk = os.read(fd, min(4096, MAX_CAPTURE_BYTES - len(output)))
        if not chunk:
            continue
        output.extend(chunk)
        received_at = time.monotonic()
        if capture_deadline is None:
            capture_deadline = received_at + CAPTURE_TIMEOUT_S
        idle_deadline = received_at + IDLE_TIMEOUT_S

    return bytes(output)


def log_is_readable(output: bytes) -> tuple[bool, str]:
    if not output:
        return False, "no data"
    if len(output) < MIN_LOG_BYTES:
        return False, f"too short ({len(output)} bytes)"
    if b"\n" not in output and b"\r" not in output:
        return False, "no line break"
    printable = sum(byte in (9, 10, 13) or 0x20 <= byte <= 0x7e for byte in output)
    ratio = printable / len(output)
    if ratio < MIN_PRINTABLE_RATIO:
        return False, f"printable ratio {ratio:.0%}"
    tokens = set(re.findall(rb"[A-Za-z][A-Za-z0-9_.:/+-]{2,}", output))
    if len(tokens) < 2:
        return False, "fewer than two text tokens"
    return True, "readable text"


def printable_output(output: bytes) -> str:
    visible = []
    for byte in output:
        if byte in (9, 10, 13) or 0x20 <= byte <= 0x7e:
            visible.append(chr(byte))
        else:
            visible.append(f"\\x{byte:02x}")
    return "".join(visible).replace("\r\n", "\n").replace("\r", "\n").strip()


def main() -> int:
    try:
        args = parse_args()
    except ArgumentError as exc:
        print(f"[error] {exc}", file=sys.stderr)
        emit_result("error", error=str(exc))
        return 1

    try:
        fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_SYNC)
    except OSError as exc:
        print(f"[error] cannot open {args.port}: {exc}", file=sys.stderr)
        emit_result("error", port=args.port, baudrate=args.baudrate, error=str(exc))
        return 1

    usb_id = None
    reset_method = "not-attempted"
    try:
        configure_serial(fd, args.baudrate)
        usb_id = find_usb_id(args.port)
        usb_label = ":".join(usb_id) if usb_id else "unknown"
        print(f"[run] port={args.port} baudrate={args.baudrate} usb_id={usb_label}")
        reset_method = reset_module(fd, usb_id)
        print(f"[run] reset_method={reset_method}; capturing startup output")
        output = capture_output(fd)
    except KeyboardInterrupt:
        print("[error] interrupted", file=sys.stderr)
        emit_result("error", port=args.port, baudrate=args.baudrate, error="interrupted")
        return 1
    except (OSError, RuntimeError, termios.error) as exc:
        print(f"[error] {exc}", file=sys.stderr)
        emit_result(
            "error",
            port=args.port,
            baudrate=args.baudrate,
            reset_method=reset_method,
            error=str(exc),
        )
        return 1
    finally:
        os.close(fd)

    if output:
        print("[serial]")
        print(printable_output(output))

    readable, reason = log_is_readable(output)
    common = {
        "port": args.port,
        "baudrate": args.baudrate,
        "reset_method": reset_method,
        "bytes": len(output),
        "usb_id": ":".join(usb_id) if usb_id else None,
    }
    if not readable:
        print(f"[error] startup log rejected: {reason}", file=sys.stderr)
        emit_result("no-valid-log", **common, reason=reason)
        return 2

    missing = [marker for marker in args.expect if marker.encode() not in output]
    if missing:
        print(f"[error] missing expected markers: {missing}", file=sys.stderr)
        emit_result("expect-missing", **common, missing_expect=missing)
        return 3

    emit_result("ok", **common, matched_expect=args.expect)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
