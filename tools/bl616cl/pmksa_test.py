#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""PMKSA ioctl test for the BL616CL Wi-Fi STA driver.

Design under test (SIOCSIWPMKSA / SIOCGIWPMKSA, PMK as 64 hex digits):
  set  stores a PMK for the next connection attempt only; the attempt
       uses it and skips deriving the PMK from the passphrase.
  get  returns the PMK of the current connection; only PSK connections
       have one.
The DUT joins an AP that supports SAE with SAE, which cannot use a PMK:
such an attempt fails and the application connects again with the
passphrase.

The image needs CONFIG_BL_WIRELESS_TESTS_PMKSA (command "pmksa"): copy
configs/wifi to configs/tmp-* and add the option.  The caller sets up the
AP and passes its security mode as --mode: wpa2 (WPA2-PSK), sae (WPA3-SAE
only), mixed (WPA2/WPA3 transition) or open.  The passphrase comes from
WIFI_TEST_PSK.  Opening the console resets the board; every case reboots
the DUT first, after a disconnect so a PMF AP drops the old association.

Each check prints PASS, FAIL (the design does not hold) or NOTE (the
design holds, or does not cover the case, but the behaviour is worth a
look).  --out gets results.txt and uart.log with the passphrase masked.
"""

import argparse
import array
import fcntl
import hashlib
import os
import re
import select
import sys
import termios
import time
from pathlib import Path

EIO, EINVAL, ENOSYS, ENOTCONN = 5, 22, 38, 107
ERRNO = {EIO: "EIO", EINVAL: "EINVAL", ENOSYS: "ENOSYS",
         ENOTCONN: "ENOTCONN"}
OTHER_PASSPHRASE = "pmksa-other-pass"
NSH_LINE_MAX = 79  # CONFIG_NSH_LINELEN 80, NUL included


class Console:
    def __init__(self, port, log, secrets):
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
        self.secrets = [s.encode() for s in secrets if s]

    @staticmethod
    def clean(b):
        return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "",
                      b.decode("utf-8", "replace")).replace("\r", "")

    def read(self, timeout, needle=None):
        buf = b""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            r, _, _ = select.select([self.fd], [], [], 0.1)
            if r:
                chunk = os.read(self.fd, 65536)
                buf += chunk
                for s in self.secrets:
                    chunk = chunk.replace(s, b"********")
                self.log.write(chunk)
                self.log.flush()
                if needle and needle.encode() in buf:
                    return True, self.clean(buf)
        return False, self.clean(buf)

    def write(self, line):
        os.write(self.fd, line.encode() + b"\r")

    def cmd(self, line, timeout=15):
        """Run line; return the output up to the next prompt."""
        if len(line) > NSH_LINE_MAX:
            sys.exit(f"command longer than the NSH line: {line[:20]}...")
        self.write(line)
        buf = ""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            _, out = self.read(0.2)
            buf += out
            pos = buf.find(line)
            if pos >= 0 and "nsh>" in buf[pos + len(line):]:
                break
        return buf


class Dut:
    def __init__(self, con, ssid, passphrase):
        self.con = con
        self.ssid = ssid
        self.passphrase = passphrase

    def wait_prompt(self, timeout=30):
        ok, out = self.con.read(timeout, "nsh>")
        if not ok and "NuttShell" in out:
            # The console sometimes drops a prompt byte; ask for a new one.
            self.con.write("")
            ok, more = self.con.read(3, "nsh>")
            out += more
        if not ok or re.search(r"panic|assert|hardfault", out, re.I):
            sys.exit("DUT boot failed")
        time.sleep(2)
        self.con.read(0.5)

    def reboot(self):
        self.run("wapi disconnect wlan0", 20)
        time.sleep(1)
        self.con.write("reboot")
        self.wait_prompt()

    def run(self, line, timeout=15):
        """Run line; return (exit status, output)."""
        out = self.con.cmd(line, timeout)
        m = re.search(r"rc=(\d+)", self.con.cmd("echo rc=$?"))
        return (int(m.group(1)) if m else None), out

    def pmksa(self, *args):
        out = self.con.cmd("pmksa " + " ".join(str(a) for a in args), 30)
        m = re.search(r"pmksa: \w+ ret=(-?\d+)(?: len=\d+)?"
                      r"(?: pmk=(\S+))?(?: time_ms=(\d+))?", out)
        if not m:
            return None, None, None
        return int(m.group(1)), m.group(2), m.group(3)

    def get(self, length=64):
        ret, pmk, ms = self.pmksa("get", length)
        return ret, (pmk.lower() if ret == 0 else None), ms

    def set(self, pmk, length=None):
        args = ["set", pmk] + ([length] if length is not None else [])
        return self.pmksa(*args)[0]

    def key(self, key):
        return self.pmksa("key", key)[0]

    def psk(self, passphrase=None):
        return self.run(f"wapi psk wlan0 {passphrase or self.passphrase} 3")[0]

    def connect(self):
        """Return (connected, seconds)."""
        t0 = time.monotonic()
        rc, _ = self.run(f"wapi essid wlan0 {self.ssid} 1", 90)
        return rc == 0, time.monotonic() - t0

    def disconnect(self):
        self.run("wapi disconnect wlan0", 20)
        time.sleep(2)


def errname(ret):
    if ret is None:
        return "no-output"
    if ret >= 0:
        return str(ret)
    return "-" + ERRNO.get(-ret, str(-ret))


def short(pmk):
    return pmk[:8] + "..." if pmk else "-"


CASES = []


def case(cid, modes, title):
    def register(fn):
        CASES.append((cid, modes, title, fn))
        return fn
    return register


def must_fail_not_connected(ret):
    """Without a connection get must fail, ideally with -ENOTCONN."""
    if ret is None or ret == 0:
        return "FAIL", f"returned {errname(ret)} while not connected"
    if ret != -ENOTCONN:
        return "NOTE", f"fails with {errname(ret)}, not -ENOTCONN"
    return "PASS", errname(ret)


def get_equals(d, pmk, what):
    ret, got, ms = d.get()
    if ret != 0:
        return "FAIL", f"{what}: get {errname(ret)}"
    if got != pmk:
        return "FAIL", f"{what}: get {short(got)} != PMK in use {short(pmk)}"
    return "PASS", f"{what}: get matches ({ms} ms)"


# --- Parameter checks (independent of the AP; run in wpa2 mode) ----------

@case("A1", ("wpa2",), "get rejects a buffer that is not 64 bytes")
def a1(d, pmk, wrong):
    bad = {n: d.get(n)[0] for n in (0, 32, 63, 65)}
    off = {n: errname(r) for n, r in bad.items() if r != -EINVAL}
    return [("FAIL", f"len->ret {off}") if off else ("PASS", "all -EINVAL")]


@case("A2", ("wpa2",), "set rejects a PMK that is not 64 characters")
def a2(d, pmk, wrong):
    bad = {32: d.set(pmk[:32], 32), 63: d.set(pmk[:63], 63),
           65: d.set(pmk + "0", 65)}
    off = {n: errname(r) for n, r in bad.items() if r != -EINVAL}
    return [("FAIL", f"len->ret {off}") if off else ("PASS", "all -EINVAL")]


@case("A3", ("wpa2",), "set rejects 64 characters that are not hex")
def a3(d, pmk, wrong):
    ret = d.set("z" * 64)
    if ret == -EINVAL:
        return [("PASS", "-EINVAL")]
    d.psk()
    ok, sec = d.connect()
    return [("FAIL", f"set returned {errname(ret)}; then connect with the "
             f"right passphrase {'ok' if ok else 'failed'} in {sec:.1f} s")]


@case("A4", ("wpa2",), "get after boot, nothing set, not connected")
def a4(d, pmk, wrong):
    return [must_fail_not_connected(d.get()[0])]


@case("A5", ("wpa2",), "get with a passphrase set but not connected")
def a5(d, pmk, wrong):
    d.psk()
    rets = [d.get()[0] for _ in range(3)]
    res = [must_fail_not_connected(r) for r in rets]
    worst = next((r for r in res if r[0] == "FAIL"), res[0])
    return [(worst[0], f"3 gets -> {[errname(r) for r in rets]}")]


# --- WPA2-PSK -------------------------------------------------------------

@case("W1", ("wpa2",), "passphrase connection: get returns its PMK")
def w1(d, pmk, wrong):
    d.psk()
    ok, sec = d.connect()
    if not ok:
        return [("FAIL", f"connect failed ({sec:.1f} s)")]
    return [get_equals(d, pmk, f"connect {sec:.1f} s"),
            get_equals(d, pmk, "again")]


@case("W2", ("wpa2",), "64-digit hex PSK connection: get returns it")
def w2(d, pmk, wrong):
    if d.key(pmk) != 0:
        return [("FAIL", "driver rejected a 64-digit hex key")]
    ok, sec = d.connect()
    if not ok:
        return [("FAIL", f"connect with hex PSK failed ({sec:.1f} s)")]
    return [get_equals(d, pmk, f"connect {sec:.1f} s")]


@case("W3", ("wpa2",), "set PMK only, no passphrase: connect, get")
def w3(d, pmk, wrong):
    if d.set(pmk) != 0:
        return [("FAIL", "set failed")]
    ok, sec = d.connect()
    if not ok:
        return [("FAIL", f"connect with set PMK failed ({sec:.1f} s)")]
    return [get_equals(d, pmk, f"connect {sec:.1f} s")]


@case("W4", ("wpa2",), "set PMK and passphrase: connect, get")
def w4(d, pmk, wrong):
    d.psk()
    if d.set(pmk) != 0:
        return [("FAIL", "set failed")]
    ok, sec = d.connect()
    if not ok:
        return [("FAIL", f"connect failed ({sec:.1f} s)")]
    return [get_equals(d, pmk, f"connect {sec:.1f} s")]


@case("W5", ("wpa2",), "set PMK wins over the passphrase; a failed "
      "attempt does not leave it behind")
def w5(d, pmk, wrong):
    d.psk()
    d.set(wrong)
    ok, sec = d.connect()
    res = [("FAIL", f"connected with a wrong set PMK and the right "
            f"passphrase ({sec:.1f} s): set PMK not used") if ok else
           ("PASS", f"wrong set PMK -> connect failed in {sec:.1f} s")]
    if ok:
        return res
    d.psk()
    ok, sec = d.connect()
    res.append(("NOTE", f"retry with only the passphrase "
                f"{'connects' if ok else 'fails'} ({sec:.1f} s): the failed "
                f"attempt {'dropped' if ok else 'kept'} the set PMK"))
    return res


@case("W6", ("wpa2",), "passphrase changed after connecting: get still "
      "returns the PMK in use")
def w6(d, pmk, wrong):
    d.psk()
    ok, sec = d.connect()
    if not ok:
        return [("FAIL", f"connect failed ({sec:.1f} s)")]
    d.psk(OTHER_PASSPHRASE)
    return [get_equals(d, pmk, "after wapi psk <other>")]


@case("W7", ("wpa2",), "PMK set while connected: current connection keeps "
      "its PMK; lifecycle after disconnect")
def w7(d, pmk, wrong):
    d.psk()
    ok, sec = d.connect()
    if not ok:
        return [("FAIL", f"connect failed ({sec:.1f} s)")]
    d.set(wrong)
    res = [get_equals(d, pmk, "after set <wrong> while connected")]
    d.disconnect()
    d.psk()
    ok, sec = d.connect()
    res.append(("NOTE", f"after disconnect, passphrase-only reconnect "
                f"{'connects' if ok else 'fails'} ({sec:.1f} s): the PMK "
                f"set while connected was {'dropped' if ok else 'kept'}"))
    return res


@case("W8", ("wpa2",), "get after disconnecting")
def w8(d, pmk, wrong):
    d.psk()
    ok, sec = d.connect()
    if not ok:
        return [("FAIL", f"connect failed ({sec:.1f} s)")]
    d.disconnect()
    return [must_fail_not_connected(d.get()[0])]


# --- PMK is one-shot even when the attempt fails early -------------------

@case("W9", ("wpa2",), "a PMK does not outlive an attempt that fails "
      "before associating")
def w9(d, pmk, wrong):
    d.set(wrong)
    d.key("z" * 64)
    ok, sec = d.connect()
    if ok:
        return [("FAIL", "connected with a non-hex 64-character key")]
    d.psk()
    ok, sec = d.connect()
    return [("PASS", f"passphrase connects ({sec:.1f} s)") if ok else
            ("FAIL", f"passphrase connect failed ({sec:.1f} s): the wrong "
             f"PMK survived the early failure")]


# --- WPA3-SAE only, and WPA2/WPA3 transition (the DUT picks SAE) ----------

def sae_get_fails(d, pmk):
    d.psk()
    ok, sec = d.connect()
    if not ok:
        return [("FAIL", f"connect failed ({sec:.1f} s)")]
    ret, got, _ = d.get()
    if ret == 0:
        return [("FAIL", f"get returned {short(got)} "
                 f"({'= PBKDF2 PMK' if got == pmk else 'unknown'}); "
                 f"SAE does not use it")]
    return [("PASS" if ret == -ENOSYS else "NOTE", f"get {errname(ret)}")]


def sae_pmk_then_passphrase(d, pmk, with_passphrase):
    """A set PMK fails on SAE; the application retries with the passphrase."""
    if with_passphrase:
        d.psk()
    d.set(pmk)
    ok, sec = d.connect()
    res = [("NOTE", f"connect with set PMK {'ok' if ok else 'failed'} "
            f"({sec:.1f} s)")]
    if ok:
        return res
    d.psk()
    ok, sec = d.connect()
    res.append(("PASS", f"retry with the passphrase connects ({sec:.1f} s)")
               if ok else
               ("FAIL", f"retry with the passphrase failed ({sec:.1f} s)"))
    return res


@case("S1", ("sae",), "SAE connection: get must not return a PMK")
def s1(d, pmk, wrong):
    return sae_get_fails(d, pmk)


@case("S2", ("sae",), "SAE AP, set PMK and passphrase, then passphrase")
def s2(d, pmk, wrong):
    return sae_pmk_then_passphrase(d, pmk, True)


@case("S3", ("sae",), "SAE AP, set PMK only, then passphrase")
def s3(d, pmk, wrong):
    return sae_pmk_then_passphrase(d, pmk, False)


@case("M1", ("mixed",), "transition AP: DUT uses SAE, get must not "
      "return a PMK")
def m1(d, pmk, wrong):
    return sae_get_fails(d, pmk)


@case("M2", ("mixed",), "transition AP, set PMK only, then passphrase")
def m2(d, pmk, wrong):
    return sae_pmk_then_passphrase(d, pmk, False)


@case("M3", ("mixed",), "transition AP, set PMK and passphrase, then "
      "passphrase")
def m3(d, pmk, wrong):
    return sae_pmk_then_passphrase(d, pmk, True)


# --- Open -----------------------------------------------------------------

@case("O1", ("open",), "open connection: get fails")
def o1(d, pmk, wrong):
    ok, sec = d.connect()
    if not ok:
        return [("FAIL", f"open connect failed ({sec:.1f} s)")]
    ret = d.get()[0]
    if ret == 0:
        return [("FAIL", "get returned a PMK for an open connection")]
    return [("PASS" if ret == -ENOSYS else "NOTE", f"get {errname(ret)}")]


@case("O2", ("open",), "open AP with a set PMK")
def o2(d, pmk, wrong):
    d.set(pmk)
    ok, sec = d.connect()
    return [("NOTE", f"connect {'ok' if ok else 'failed'} ({sec:.1f} s)")]


# --- Scan ----------------------------------------------------------------

@case("E1", ("wpa2", "sae", "mixed", "open"), "scan reports encryption")
def e1(d, pmk, wrong):
    want = "8000" if d.mode == "open" else "0800"
    out = d.con.cmd("wapi scan wlan0", 30)
    rows = [ln.split("\t") for ln in out.splitlines() if ln.count("\t") == 4]
    got = [r[3] for r in rows if r[4].strip() == d.ssid]
    if not got:
        return [("FAIL", f"{d.ssid} not in the scan result")]
    return [("PASS" if got[0] == want else "FAIL",
             f"encode {got[0]}, want {want}")]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", default=os.environ.get("WIFI_TEST_PORT",
                                                     "/dev/ttyUSB3"))
    ap.add_argument("--ssid", default=os.environ.get("WIFI_TEST_SSID",
                                                     "ax86u"))
    ap.add_argument("--mode", required=True,
                    choices=("wpa2", "sae", "mixed", "open"))
    ap.add_argument("--cases", help="comma-separated case IDs to run")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    passphrase = os.environ.get("WIFI_TEST_PSK", "")
    if args.mode != "open" and not passphrase:
        sys.exit("WIFI_TEST_PSK is not set")
    pmk = hashlib.pbkdf2_hmac("sha1", passphrase.encode(),
                              args.ssid.encode(), 4096, 32).hex()
    wrong = hashlib.sha256(b"pmksa-test wrong pmk").hexdigest()

    todo = [c for c in CASES if args.mode in c[1] and
            (not args.cases or c[0] in args.cases.split(","))]
    args.out.mkdir(parents=True, exist_ok=True)
    log = open(args.out / "uart.log", "ab")
    results = open(args.out / "results.txt", "a")
    con = Console(args.port, log, [passphrase])
    d = Dut(con, args.ssid, passphrase)
    d.mode = args.mode
    d.wait_prompt()

    for cid, _, title, fn in todo:
        d.reboot()
        con.log.write(f"\n===== {cid} {title} =====\n".encode())
        for status, detail in fn(d, pmk, wrong):
            line = f"{args.mode:5} {cid:3} {status:4} {title} | {detail}"
            print(line, flush=True)
            results.write(line + "\n")
            results.flush()

    d.disconnect()
    return 0


if __name__ == "__main__":
    sys.exit(main())
