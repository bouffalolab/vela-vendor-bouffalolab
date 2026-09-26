#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Wi-Fi iperf bench on one boot of the flashed image, with perfmon.

Opening the console resets the board.  The DUT joins the AP (WIFI_TEST_PSK
from the environment; empty or unset means an open AP), then each rep runs
the cases back to back so slow drift hits every case alike: tcp-tx and
udp-tx with the DUT as iperf2 client, tcp-rx and udp-rx with the host as
client (UDP offered at --udp-rx Mbit/s).  With --perfmon, "perfmon stat" or
"perfmon prof -r" runs on the DUT inside each traffic window (from 2 s to
2 s before its end) and its output is saved as r<rep>-<case>.perfmon for
perfmon_report.py.

Rates come from the DUT's iperf report: the whole-run line for TX, the
intervals inside the traffic window for RX.  TCP RX also records the host
RetransSegs delta.  After each case the DUT pings the host; on failure it
reconnects and the case is marked RECONNECT.

When several benches share the AP, set WIFI_AIRTIME_LOCK to a file that
all of them flock exclusively while their traffic runs; each case then waits
for it.

Output in <out-dir>: results.csv, uart.log (PSK masked), per case
r<rep>-<case>-host.txt and r<rep>-<case>.perfmon.
"""

import argparse
import array
import fcntl
import os
import re
import select
import subprocess
import sys
import termios
import time
from pathlib import Path

CASES = ("tcp-tx", "udp-tx", "tcp-rx", "udp-rx")

# The 2 Mbaud console drops a byte now and then ("Mits/sec").
IVAL = re.compile(r"(\d+\.\d+)-\s*(\d+\.\d+) sec\s+(\d+) Bytes\s+"
                  r"([\d.]+) M[a-z]{2,4}/sec")


class Console:
    def __init__(self, port, log, psk):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
        a = termios.tcgetattr(self.fd)
        a[0] = 0
        a[1] = 0
        a[2] &= ~(termios.PARENB | termios.CSTOPB | termios.CSIZE |
                  termios.HUPCL | getattr(termios, "CRTSCTS", 0))
        a[2] |= termios.CLOCAL | termios.CREAD | termios.CS8
        a[3] = 0
        a[4] = a[5] = termios.B2000000
        a[6][termios.VMIN] = 0
        a[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        termios.tcflush(self.fd, termios.TCIFLUSH)

        # DTR=1, RTS=0 releases reset on the Ai-M64L-32S kit.

        for flag, on in ((termios.TIOCM_DTR, True),
                         (termios.TIOCM_RTS, False)):
            bits = array.array("i", [0])
            fcntl.ioctl(self.fd, termios.TIOCMGET, bits, True)
            bits[0] = bits[0] | flag if on else bits[0] & ~flag
            fcntl.ioctl(self.fd, termios.TIOCMSET, bits)
        self.log = log
        self.psk = psk.encode()

    @staticmethod
    def clean(b):
        return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "",
                      b.decode("utf-8", "replace")).replace("\r", "")

    def read(self, timeout, needle=None, until=None):
        """Read until needle shows up, until() is true or the timeout."""
        buf = b""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            r, _, _ = select.select([self.fd], [], [], 0.1)
            if r:
                chunk = os.read(self.fd, 65536)
                buf += chunk
                self.log.write(chunk.replace(self.psk, b"********")
                               if self.psk else chunk)
                self.log.flush()
                if needle and needle.encode() in buf:
                    return True, self.clean(buf)
            if until and until():
                return True, self.clean(buf)
        return False, self.clean(buf)

    def write(self, line):
        os.write(self.fd, line.encode() + b"\r")

    def cmd(self, line, timeout=15):
        """Run line; return the output up to the next prompt."""
        self.write(line)
        buf = ""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            _, out = self.read(0.2)
            buf += out
            pos = buf.find(line)
            if pos >= 0 and "nsh>" in buf[pos:]:
                break
        return buf


def tx_rate(text, dur):
    """Whole-run line of the DUT client, e.g. 0.00- 20.02 sec N Bytes R."""
    runs = [(float(e), int(b)) for s, e, b, _ in IVAL.findall(text)
            if float(s) == 0 and float(e) > dur / 2]
    return runs[-1][1] * 8 / runs[-1][0] / 1e6 if runs else None


def rx_rate(text, dur):
    """Bytes of the DUT server's intervals inside the traffic window."""
    iv = {(float(s), float(e)): int(b) for s, e, b, _ in IVAL.findall(text)
          if float(e) - float(s) <= 6 and float(e) <= dur + 2}
    if not iv:
        return None
    return sum(iv.values()) * 8 / max(e for _, e in iv) / 1e6


def retrans():
    lines = [ln.split() for ln in open("/proc/net/snmp")
             if ln.startswith("Tcp:")]
    return int(lines[1][lines[0].index("RetransSegs")])


def perfmon_block(text):
    """Cut the perfmon output out of the console text of one case."""
    lines = text.splitlines()
    first = next((i for i, ln in enumerate(lines)
                  if ln.startswith("perfmon: ")), None)
    if first is None:
        return None
    keep = [ln for ln in lines[first:] if not IVAL.search(ln)
            and "iperf" not in ln and not ln.startswith("nsh>")]
    return "\n".join(keep) + "\n"


def main():
    ap = argparse.ArgumentParser(
        description="Wi-Fi iperf bench with optional perfmon capture.")
    ap.add_argument("out", type=Path)
    ap.add_argument("--port", default=os.environ.get("WIFI_TEST_PORT",
                                                     "/dev/ttyUSB3"))
    ap.add_argument("--ssid", default=os.environ.get("WIFI_TEST_SSID",
                                                     "ax86u"))
    ap.add_argument("--host-ip", default=os.environ.get("IPERF_HOST_IP",
                                                        "192.168.50.200"))
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--time", type=int, default=20, help="seconds per case")
    ap.add_argument("--udp-rx", default="30", help="offered Mbit/s")
    ap.add_argument("--cases", default=",".join(CASES))
    ap.add_argument("--perfmon", choices=("none", "stat", "prof"),
                    default="none")
    args = ap.parse_args()
    cases = args.cases.split(",")
    dur = args.time
    if any(c not in CASES for c in cases) or dur < 8:
        ap.error("unknown case or --time below 8")

    psk = os.environ.get("WIFI_TEST_PSK", "")
    args.out.mkdir(parents=True, exist_ok=True)
    con = Console(args.port, (args.out / "uart.log").open("wb"), psk)
    results = (args.out / "results.csv").open("w")
    results.write("rep,case,mbps,extra\n")

    def row(rep, case, mbps, extra=""):
        line = f"{rep},{case},{'' if mbps is None else f'{mbps:.2f}'},{extra}"
        print(line, flush=True)
        results.write(line + "\n")
        results.flush()

    def connect():
        if psk:
            con.cmd(f"wapi psk wlan0 {psk} 3")
        con.cmd(f"wapi essid wlan0 {args.ssid} 1", 50)
        con.cmd("ifconfig wlan0 dhcp", 30)
        time.sleep(2)
        m = re.search(r"inet addr:(\d+\.\d+\.\d+\.\d+)",
                      con.cmd("ifconfig wlan0"))
        return m.group(1) if m and m.group(1) != "0.0.0.0" else None

    def link_ok():
        m = re.search(r"(\d+) packets transmitted, (\d+) received",
                      con.cmd(f"ping -c 2 {args.host_ip}", 10))
        return bool(m and int(m.group(2)) > 0)

    ok, boot = con.read(30, "nsh>")
    if not ok and "NuttShell" in boot:
        # The console sometimes drops a prompt byte; ask for a new one.
        con.write("")
        ok, more = con.read(3, "nsh>")
        boot += more
    if not ok or re.search(r"panic|assert|hardfault", boot, re.I):
        sys.exit("boot failed")

    dut_ip = connect()
    if not dut_ip:
        row(0, "setup", None, "dhcp_failed")
        return 1
    free = con.cmd("free")
    (args.out / "free-boot.txt").write_text(free)
    m = re.search(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+.*\bUmem\s*$", free, re.M)
    row(0, "setup", None, f"dut_ip={dut_ip} heap_total="
        f"{m.group(1) if m else '?'} heap_free={m.group(3) if m else '?'}")

    perfmon = None
    if args.perfmon != "none":
        perfmon = (f"perfmon {args.perfmon} -d 2 -t {dur - 4}" +
                   (" -r" if args.perfmon == "prof" else "") + " &")

    port = 7000
    for rep in range(args.reps):
        for case in cases:
            port += 1
            udp = case.startswith("udp")
            name = f"r{rep}-{case}"
            extra = ""
            lock = None
            if os.environ.get("WIFI_AIRTIME_LOCK"):
                lock = os.open(os.environ["WIFI_AIRTIME_LOCK"],
                               os.O_RDWR | os.O_CREAT, 0o666)
                fcntl.flock(lock, fcntl.LOCK_EX)
            if case.endswith("tx"):
                srv = subprocess.Popen(
                    ["iperf", "-s", "-B", args.host_ip, "-p", str(port)] +
                    (["-u"] if udp else []),
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=True)
                time.sleep(1)
                if perfmon:
                    con.cmd(perfmon, 5)
                con.write(f"iperf {'-u ' if udp else ''}-c {args.host_ip} "
                          f"-p {port} -i 5 -t {dur}")
                _, out = con.read(dur + 25, "iperf exit")
                _, more = con.read(3, "nsh>")
                out += more
                srv.kill()             # iperf2 -u -s ignores SIGTERM
                (args.out / f"{name}-host.txt").write_text(
                    srv.communicate(timeout=10)[0])
                rate = tx_rate(out, dur)
            else:
                con.cmd(f"iperf {'-u ' if udp else ''}-s -p {port} -i 5 "
                        f"-t {dur + 6} &", 5)
                time.sleep(1)
                before = retrans()
                cl = subprocess.Popen(
                    ["iperf", "-c", dut_ip, "-B", args.host_ip, "-p",
                     str(port), "-t", str(dur), "-i", "5"] +
                    (["-u", "-b", f"{args.udp_rx}M"] if udp else []),
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=True)
                out = ""
                if perfmon:
                    out = con.cmd(perfmon, 5)

                # Keep draining the console while the host sends: the
                # perfmon dump is larger than the tty buffer.

                _, more = con.read(dur + 60, until=lambda: cl.poll()
                                   is not None)
                out += more
                host = cl.communicate(timeout=10)[0]
                delta = retrans() - before
                (args.out / f"{name}-host.txt").write_text(host)
                _, more = con.read(20, "iperf exit")
                out += more
                _, more = con.read(3, "nsh>")
                out += more
                rate = rx_rate(out, dur)
                hm = re.findall(r"([\d.]+) Mbits/sec", host)
                extra = (f"offered={args.udp_rx}M" if udp
                         else f"host_retrans={delta}")
                extra += f" host_mbps={hm[-1] if hm else None}"
            if lock is not None:
                os.close(lock)
            if perfmon:
                block = perfmon_block(out)
                if block:
                    (args.out / f"{name}.perfmon").write_text(block)
                else:
                    extra += " no_perfmon_output"
            if not link_ok():
                ip = connect()
                extra += f" RECONNECT(ip={ip})"
            row(rep, case, rate, extra.strip())
            time.sleep(2)

    con.cmd("wapi disconnect wlan0", 20)
    return 0


if __name__ == "__main__":
    sys.exit(main())
