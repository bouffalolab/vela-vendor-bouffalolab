#!/usr/bin/env python3
"""Validate BL616CL KASAN and peripheral regressions over one UART fd."""

import argparse
import array
import fcntl
import os
import re
import select
import termios
import time

FAULT_CASES = {
    1: b"test_heap_underflow",
    2: b"test_heap_overflow",
    3: b"test_heap_use_after_free",
}

REGRESSION_COMMANDS = (
    (
        "mcu_gpio_test -c edge --out /dev/gpio12 -n 3 -v",
        20,
        (b"[GPIO-edge] PASS",),
    ),
    (
        "mcu_timer_test -c 001 -t 100000 -n 5 -e 5 -v",
        20,
        (b"[TIMER-001] PASS accuracy within tolerance",),
    ),
    (
        "mcu_timer_test -c 002 -t 500000 -a 39 -b 79 -v",
        20,
        (b"[TIMER-002] PASS prescaler takes effect",),
    ),
    (
        "mcu_timer_test -c 005",
        20,
        (b"[TIMER-005] PASS",),
    ),
    (
        "oneshot -d 100000 /dev/oneshot",
        10,
        (b"Starting oneshot timer", b"Finished"),
    ),
    (
        "mcu_wdt_test -c 002 -t 1000 -p 3000 -i 500 -v",
        15,
        (b"[WDT-002]", b"PASS: fed "),
    ),
    (
        "mcu_wdt_test -c 003 -t 1000",
        20,
        (b"[WDT-003]", b"PASS: invalid/live changes rejected"),
    ),
)


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


def set_modem_line(fd, flag, enabled):
    bits = array.array("i", [0])
    fcntl.ioctl(fd, termios.TIOCMGET, bits, True)
    bits[0] = bits[0] | flag if enabled else bits[0] & ~flag
    fcntl.ioctl(fd, termios.TIOCMSET, bits)


def set_run_state(fd):
    set_modem_line(fd, termios.TIOCM_DTR, True)
    set_modem_line(fd, termios.TIOCM_RTS, False)


def read_until(fd, markers, timeout):
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
        if all(marker in output for marker in markers):
            break

    return bytes(output)


def send_command(fd, text, timeout=30):
    for byte in (text + "\r\n").encode():
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
        if text.encode() in output and output.count(b"nsh>") >= 2:
            break

    return bytes(output)


def warm_reset(fd):
    set_modem_line(fd, termios.TIOCM_DTR, True)
    time.sleep(0.05)
    set_modem_line(fd, termios.TIOCM_RTS, True)
    time.sleep(0.05)
    set_modem_line(fd, termios.TIOCM_RTS, False)
    time.sleep(0.1)
    return read_until(fd, (b"NuttShell (NSH)", b"nsh>"), 30)


def validate_fault_case(number, output):
    failures = []
    result = re.search(
        rb"KASANTEST result: case="
        + str(number).encode()
        + rb" .* FAULT status=([0-9]+) verification=required",
        output,
    )
    if result is None or result.group(1) == b"0":
        failures.append(f"case {number} fault status")

    target = re.search(rb"KASANTEST access: .* target=(0x[0-9a-fA-F]+)", output)
    reports = re.findall(
        rb"kasan detected a (read|write) access error, address at "
        rb"(0x[0-9a-fA-F]+),size is ([0-9]+)",
        output,
    )
    if target is None:
        failures.append(f"case {number} target")
    if len(reports) != 1:
        failures.append(f"case {number} report count={len(reports)}")
    elif target is not None:
        access, address, size = reports[0]
        if access != b"write" or address != target.group(1) or size != b"1":
            failures.append(f"case {number} report mismatch")

    if FAULT_CASES[number] not in output or b"run_child" not in output:
        failures.append(f"case {number} backtrace")
    if b"KASANTEST expected fault was not triggered" in output:
        failures.append(f"case {number} fault not triggered")
    return failures


def validate_nonfault_case(number, output):
    failures = []
    result = re.search(
        rb"KASANTEST result: case=" + str(number).encode() + rb" .* PASS status=0",
        output,
    )
    if result is None:
        failures.append(f"case {number} result")
    if b"kasan detected" in output:
        failures.append(f"case {number} unexpected KASAN report")
    return failures


def validate_regression(outputs, alive_output):
    failures = []
    for command, output, markers in outputs:
        if command.encode() not in output:
            failures.append(f"{command} command echo")
        if output.count(b"nsh>") < 2:
            failures.append(f"{command} prompt")
        for marker in markers:
            if marker not in output:
                failures.append(f"{command} marker {marker.decode()}")
        if b"kasan detected" in output:
            failures.append(f"{command} unexpected KASAN report")

    if b"echo ST012_FINAL_ALIVE" not in alive_output:
        failures.append("ST012_FINAL_ALIVE command echo")
    if alive_output.count(b"nsh>") < 2:
        failures.append("ST012_FINAL_ALIVE prompt")
    if re.search(rb"\r?\nST012_FINAL_ALIVE\r?\n", alive_output) is None:
        failures.append("ST012_FINAL_ALIVE output")
    return failures


def run(args):
    chunks = []
    case_outputs = {}
    reset_outputs = []
    fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_SYNC)
    try:
        configure(fd, args.baudrate)
        set_run_state(fd)
        chunks.append(read_until(fd, (b"NuttShell (NSH)", b"nsh>"), 30))

        if args.mode == "test":
            for number in (21, 1, 2, 3, 21, 34, 35):
                output = send_command(fd, f"kasantest {number}", timeout=60)
                case_outputs.setdefault(number, []).append(output)
                chunks.append(output)

            for _ in range(args.warm_resets):
                output = warm_reset(fd)
                reset_outputs.append(output)
                chunks.append(output)
        else:
            chunks.append(send_command(fd, "help"))

        regression_outputs = []
        for command, timeout, markers in REGRESSION_COMMANDS:
            output = send_command(fd, command, timeout)
            regression_outputs.append((command, output, markers))
            chunks.append(output)
        alive_output = send_command(fd, "echo ST012_FINAL_ALIVE")
        chunks.append(alive_output)
    finally:
        set_run_state(fd)
        time.sleep(0.2)
        with open(args.log, "wb") as stream:
            stream.write(b"".join(chunks))
        os.close(fd)

    data = b"".join(chunks)
    failures = []
    if b"NuttShell (NSH)" not in data:
        failures.append("initial NSH banner")

    if args.mode == "test":
        legal_outputs = case_outputs.get(21, [])
        if len(legal_outputs) != 2:
            failures.append("case 21 execution count")
        for output in legal_outputs:
            failures.extend(validate_nonfault_case(21, output))

        for number in FAULT_CASES:
            outputs = case_outputs.get(number, [])
            if len(outputs) != 1:
                failures.append(f"case {number} execution count")
            else:
                failures.extend(validate_fault_case(number, outputs[0]))

        for number in (34, 35):
            outputs = case_outputs.get(number, [])
            if len(outputs) != 1:
                failures.append(f"case {number} execution count")
            else:
                failures.extend(validate_nonfault_case(number, outputs[0]))

        for index, output in enumerate(reset_outputs, 1):
            if b"NuttShell (NSH)" not in output or b"nsh>" not in output:
                failures.append(f"warm reset {index} boot")
            if re.search(rb"kasan detected|Assertion failed|panic", output, re.I):
                failures.append(f"warm reset {index} unexpected fault")
    else:
        if re.search(rb"^\s+kasantest(?:\s|$)", data, re.M):
            failures.append("kasantest command was not trimmed")
        if b"kasan detected" in data:
            failures.append("unexpected KASAN report")

    failures.extend(validate_regression(regression_outputs, alive_output))
    print(f"bytes={len(data)} failures={failures} log={args.log}")
    return 0 if not failures else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("test", "product"), required=True)
    parser.add_argument("--port", default="/dev/ttyUSB2")
    parser.add_argument("--baudrate", type=int, default=2000000)
    parser.add_argument("--log", required=True)
    parser.add_argument("--warm-resets", type=int)
    args = parser.parse_args()
    if args.warm_resets is None:
        args.warm_resets = 3 if args.mode == "test" else 0
    if args.warm_resets < 0:
        parser.error("--warm-resets must be non-negative")
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
