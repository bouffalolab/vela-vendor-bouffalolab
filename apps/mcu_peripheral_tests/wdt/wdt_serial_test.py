#!/usr/bin/env python3
"""Host-side serial driver for the mcu_wdt_test NSH command.

Drives the BL616 NuttX console over UART to verify the watchdog test cases:

  * WDT-002: start the watchdog, feed it periodically, confirm it reports
    PASS and the board does NOT reset.
  * WDT-001: timeout-reset check.
      - Stage 1: run `mcu_wdt_test -c 001`. The command reports the previous
        reset cause, then arms the watchdog without feeding it, so the board
        resets.
      - Stage 2: after the reboot, run `mcu_wdt_test -c 001 -s` (status only,
        non-destructive) and expect it to report the previous reset cause was
        the WATCHDOG -- proving stage 1 actually triggered a watchdog reset.

The external UART adapter stays enumerated across a chip reset, so one serial
file descriptor remains open for the complete run and receives reboot logs.

All raw serial traffic (including boot logs) plus the script's own step
markers and verdicts are saved to a log file (--log, default auto-named).

Usage:
  python3 wdt_serial_test.py --port /dev/ttyUSB2 --baud 2000000 \
      [--case all|001|002] [--log PATH]
"""

import argparse
import array
import fcntl
import os
import select
import sys
import termios
import time

# Module-level log handle, set up in main().
LOG = None


def now():
    return time.monotonic()


def logmsg(text):
    """Print to stdout and mirror into the log file as a marked line."""
    sys.stdout.write(text + "\n")
    sys.stdout.flush()
    if LOG:
        LOG.write("### " + text + "\n")
        LOG.flush()


class Console:
    def __init__(self, port, baud):
        self.port = port
        self.baud = baud
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self._configure()
        self._restore_run_state()

    def _configure(self):
        baud = getattr(termios, f"B{self.baud}", None)
        if baud is None:
            raise RuntimeError(f"unsupported baud rate: {self.baud}")

        attrs = termios.tcgetattr(self.fd)
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
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        termios.tcflush(self.fd, termios.TCIFLUSH)

    def _restore_run_state(self):
        bits = array.array("i", [0])
        fcntl.ioctl(self.fd, termios.TIOCMGET, bits, True)
        bits[0] |= termios.TIOCM_DTR
        bits[0] &= ~termios.TIOCM_RTS
        fcntl.ioctl(self.fd, termios.TIOCMSET, bits)

    def close(self):
        try:
            if self.fd >= 0:
                self._restore_run_state()
                os.close(self.fd)
                self.fd = -1
        except Exception:
            pass

    def write_line(self, text):
        termios.tcflush(self.fd, termios.TCIFLUSH)
        os.write(self.fd, (text + "\r").encode())
        termios.tcdrain(self.fd)
        if LOG:
            LOG.write("### > %s\n" % text)
            LOG.flush()

    def _read_chunk(self):
        ready, _, _ = select.select([self.fd], [], [], 0.2)
        if not ready:
            return ""

        data = os.read(self.fd, 4096)
        if data:
            text = data.decode("utf-8", "replace")
            if LOG:
                LOG.write(text)
                LOG.flush()
            return text
        return ""

    def expect(self, markers, timeout, echo=True):
        """Read until one marker substring appears or timeout elapses.

        markers: list of (name, substring).
        Returns (name_or_None, captured_text).
        """
        deadline = now() + timeout
        captured = ""
        while now() < deadline:
            chunk = self._read_chunk()
            if chunk:
                captured += chunk
                if echo:
                    sys.stdout.write(chunk)
                    sys.stdout.flush()
                for name, sub in markers:
                    if sub in captured:
                        return name, captured
        return None, captured


def banner(msg):
    logmsg("\n" + "=" * 70)
    logmsg(msg)
    logmsg("=" * 70)


def wake_nsh(con, timeout=10):
    """Nudge the console until the NSH prompt responds."""
    deadline = now() + timeout
    while now() < deadline:
        con.write_line("")
        name, _ = con.expect([("prompt", "nsh>")], timeout=2, echo=False)
        if name:
            return True
    return False


def run_wdt002(con):
    banner("WDT-002: periodic keepalive, expect PASS and NO reset")
    if not wake_nsh(con):
        logmsg("RESULT WDT-002: FAIL (no NSH prompt before start)")
        return False

    con.write_line("mcu_wdt_test -c 002")
    # Default case 002 feeds for 9s; allow generous headroom.
    name, _ = con.expect(
        [
            ("pass", "PASS: fed"),
            ("fail", "FAIL"),
            ("err", "execution failed"),
            ("reset", "Reset cause:"),
        ],
        timeout=30,
    )
    if name == "pass":
        logmsg("RESULT WDT-002: PASS (fed without reset)")
        return True
    if name == "reset":
        logmsg("RESULT WDT-002: FAIL (unexpected reset during feeding)")
        return False
    logmsg("RESULT WDT-002: FAIL (marker=%s)" % name)
    return False


def run_wdt001(con):
    banner("WDT-001 stage 1: arm watchdog without feeding, expect RESET")
    if not wake_nsh(con):
        logmsg("RESULT WDT-001: FAIL (no NSH prompt before stage 1)")
        return False

    con.write_line("mcu_wdt_test -c 001")
    name, _ = con.expect(
        [
            ("reboot", "NuttShell (NSH)"),
            ("reboot2", "Reset cause:"),
            ("nofeed_fail", "did not reset the device"),
        ],
        # ~3s timeout + reboot; allow margin.
        timeout=30,
    )

    if name not in ("reboot", "reboot2"):
        logmsg("RESULT WDT-001: FAIL (no reset observed, marker=%s)" % name)
        return False

    logmsg("--- reset observed, waiting for reboot to settle ---")

    banner("WDT-001 stage 2: confirm reset cause = WATCHDOG (status-only)")
    if not wake_nsh(con, timeout=20):
        logmsg("RESULT WDT-001: FAIL (no NSH prompt after reboot)")
        return False

    # Status-only (-s): confirm the previous reset cause without arming a
    # new reset, so the device stays at NSH afterwards.
    con.write_line("mcu_wdt_test -c 001 -s")
    name, _ = con.expect(
        [
            ("pass", "PASS: previous reset cause = WATCHDOG"),
            ("notwdt", "not WATCHDOG"),
            ("fail", "FAIL"),
        ],
        timeout=15,
    )
    if name == "pass":
        logmsg("RESULT WDT-001: PASS (reset cause confirmed = WATCHDOG)")
        return True
    if name == "notwdt":
        logmsg("RESULT WDT-001: FAIL (reset cause was not WATCHDOG)")
        return False
    logmsg("RESULT WDT-001: FAIL (marker=%s)" % name)
    return False


def main():
    global LOG

    ap = argparse.ArgumentParser(description="Serial driver for mcu_wdt_test")
    ap.add_argument("--port", default="/dev/ttyUSB2")
    ap.add_argument("--baud", type=int, default=2000000)
    ap.add_argument("--case", default="all", choices=["all", "001", "002"])
    ap.add_argument(
        "--log",
        default=None,
        help="raw log file path (default: auto-named with " "timestamp)",
    )
    args = ap.parse_args()

    log_path = args.log or ("wdt_serial_%s.log" % time.strftime("%Y%m%d_%H%M%S"))
    LOG = open(log_path, "w", encoding="utf-8")
    LOG.write(
        "### mcu_wdt_test serial run: port=%s baud=%d case=%s start=%s\n"
        % (args.port, args.baud, args.case, time.strftime("%Y-%m-%d %H:%M:%S"))
    )
    LOG.flush()
    logmsg("Raw log: %s" % log_path)

    con = Console(args.port, args.baud)
    results = {}
    try:
        if args.case in ("all", "002"):
            results["WDT-002"] = run_wdt002(con)
        if args.case in ("all", "001"):
            results["WDT-001"] = run_wdt001(con)
    finally:
        con.close()

    banner("SUMMARY")
    ok = True
    for k in ("WDT-002", "WDT-001"):
        if k in results:
            logmsg("  %s: %s" % (k, "PASS" if results[k] else "FAIL"))
            ok = ok and results[k]
    logmsg("Raw log saved to: %s" % log_path)
    LOG.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
