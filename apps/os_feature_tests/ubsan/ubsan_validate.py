#!/usr/bin/env python3
"""Validate BL616CL UBSAN and peripheral regressions on one UART fd."""

import argparse
import array
import fcntl
import os
import re
import select
import termios
import time


FORBIDDEN = (b"PANIC", b"Assertion failed", b"up_dump_register")


def configure(fd, baudrate):
    baud = getattr(termios, f"B{baudrate}")
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


def modem(fd, flag, enabled):
    bits = array.array("i", [0])
    fcntl.ioctl(fd, termios.TIOCMGET, bits, True)
    bits[0] = bits[0] | flag if enabled else bits[0] & ~flag
    fcntl.ioctl(fd, termios.TIOCMSET, bits)


def read_until(fd, markers, timeout):
    output = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.2)
        if not ready:
            continue
        chunk = os.read(fd, 65536)
        if chunk:
            output.extend(chunk)
            if all(marker in output for marker in markers):
                return bytes(output)
    return bytes(output)


def command(fd, text, timeout=30):
    termios.tcflush(fd, termios.TCIFLUSH)
    encoded = text.encode()
    for byte in encoded + b"\r\n":
        os.write(fd, bytes((byte,)))
        termios.tcdrain(fd)
        time.sleep(0.001)

    output = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.2)
        if not ready:
            continue
        chunk = os.read(fd, 65536)
        if not chunk:
            continue
        output.extend(chunk)
        echo_at = output.find(encoded)
        if echo_at >= 0 and output.find(b"nsh>", echo_at + len(encoded)) >= 0:
            break

    return bytes(output)


def validate_command(text, output, required, allow_ubsan=False):
    failures = []
    if text.encode() not in output:
        failures.append(f"{text}: command echo")
    if b"nsh>" not in output:
        failures.append(f"{text}: prompt")
    for marker in required:
        if marker not in output:
            failures.append(f"{text}: {marker.decode(errors='replace')}")
    for marker in FORBIDDEN:
        if marker in output:
            failures.append(f"{text}: forbidden {marker.decode()}")
    if not allow_ubsan and b"UBSAN:" in output:
        failures.append(f"{text}: unexpected UBSAN report")
    return failures


def validate_ubsan_case(case, output, reason=None):
    text = f"ubsan_test {case}"
    failures = validate_command(
        text,
        output,
        [f"UBSAN_TEST BEGIN case={case}".encode()],
        allow_ubsan=reason is not None,
    )
    reports = re.findall(
        rb"UBSAN: ([a-z-]+) in ([^\r\n]+ubsan_test\.c):(\d+):(\d+)",
        output,
    )

    if reason is None:
        if f"UBSAN_TEST RESULT case={case} PASS".encode() not in output:
            failures.append(f"{case}: result")
        if reports:
            failures.append(f"{case}: unexpected report")
    else:
        expected = (
            f"UBSAN_TEST RESULT case={case} FAULT verification=required"
        ).encode()
        if expected not in output:
            failures.append(f"{case}: result")
        if len(reports) != 1:
            failures.append(f"{case}: report count={len(reports)}")
        elif reports[0][0] != reason.encode():
            failures.append(f"{case}: reason={reports[0][0].decode()}")
        elif int(reports[0][2]) == 0 or int(reports[0][3]) == 0:
            failures.append(f"{case}: zero source line or column")

    return failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("test", "product"), required=True)
    parser.add_argument("--port", default="/dev/ttyUSB2")
    parser.add_argument("--baudrate", type=int, default=2000000)
    parser.add_argument("--log", required=True)
    args = parser.parse_args()

    chunks = []
    case_outputs = []
    command_outputs = []
    fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_SYNC)
    try:
        configure(fd, args.baudrate)
        modem(fd, termios.TIOCM_DTR, True)
        modem(fd, termios.TIOCM_RTS, False)
        startup = read_until(fd, [b"NuttShell (NSH)", b"nsh>"], 30)
        chunks.append(startup)

        if args.mode == "product":
            output = command(fd, "help")
            command_outputs.append(("help", output, []))
            chunks.append(output)
        else:
            cases = (
                ("legal", None),
                ("add-overflow", "add-overflow"),
                ("legal", None),
                ("shift-out-of-bounds", "shift-out-of-bounds"),
                ("legal", None),
            )
            for case, reason in cases:
                output = command(fd, f"ubsan_test {case}")
                case_outputs.append((case, reason, output))
                chunks.append(output)

        checks = (
            (
                "mcu_gpio_test -c edge --out /dev/gpio12 -n 3 -v",
                20,
                [b"[GPIO-edge] PASS"],
            ),
            (
                "mcu_timer_test -c 001 -t 100000 -n 5 -e 5 -v",
                20,
                [b"[TIMER-001] PASS accuracy within tolerance"],
            ),
            (
                "mcu_timer_test -c 002 -t 500000 -a 39 -b 79 -v",
                20,
                [b"[TIMER-002] PASS prescaler takes effect"],
            ),
            (
                "mcu_timer_test -c 005",
                20,
                [b"[TIMER-005] PASS"],
            ),
            (
                "oneshot -d 100000 /dev/oneshot",
                10,
                [b"Starting oneshot timer", b"Finished"],
            ),
            (
                "mcu_wdt_test -c 002 -t 1000 -p 3000 -i 500 -v",
                15,
                [b"[WDT-002]", b"PASS: fed 6 times", b"no reset"],
            ),
            (
                "mcu_wdt_test -c 003 -t 1000",
                20,
                [b"[WDT-003]", b"PASS: invalid/live changes rejected"],
            ),
            (
                "echo ST013_FINAL_ALIVE",
                10,
                [b"\nST013_FINAL_ALIVE\r\n"],
            ),
        )
        for text, timeout, required in checks:
            output = command(fd, text, timeout)
            command_outputs.append((text, output, required))
            chunks.append(output)
    finally:
        modem(fd, termios.TIOCM_DTR, True)
        modem(fd, termios.TIOCM_RTS, False)
        time.sleep(0.2)
        with open(args.log, "wb") as stream:
            stream.write(b"".join(chunks))
        os.close(fd)

    data = b"".join(chunks)
    failures = []
    for marker in (b"NuttShell (NSH)", b"nsh>"):
        if marker not in startup:
            failures.append(f"startup: {marker.decode()}")
    for marker in FORBIDDEN:
        if marker in startup:
            failures.append(f"startup: forbidden {marker.decode()}")
    if b"UBSAN:" in startup:
        failures.append("startup: unexpected UBSAN report")

    for text, output, required in command_outputs:
        failures.extend(validate_command(text, output, required))

    if args.mode == "product":
        if b"ubsan_test" in data or b"UBSAN_TEST" in data:
            failures.append("product: ubsan_test command was not trimmed")
        if b"UBSAN:" in data:
            failures.append("product: unexpected UBSAN report")
    else:
        for case, reason, output in case_outputs:
            failures.extend(validate_ubsan_case(case, output, reason))
        if data.count(b"UBSAN:") != 2:
            failures.append(f"test: total report count={data.count(b'UBSAN:')}")

    print(f"bytes={len(data)} failures={failures} log={args.log}")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
