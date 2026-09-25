#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Summarize perfmon output saved by wifi_bench.py (or copied by hand).

Each input is the console text of one "perfmon stat" or "perfmon prof -r"
run, named <anything>-<case>.perfmon; runs with the same case are merged.
Prints per case IPC, cache and branch miss rates and the share of cycles
per IRQ.  With --map and a raw histogram it also attributes the busy tick
samples to modules (from the archive in the linker map) and functions (nm
of the ELF): a 2^shift byte bucket counts for the function holding its
first byte.

--hot MODULE_REGEX lists the functions of the matching modules by samples,
each bucket counting for every function it overlaps (so that small hot
neighbours are not lost), with size and whether the ld.script hot list
already has it.  Use it to refresh the list after wl80211 or macsw
updates; see docs/bl616cl-hot-code-layout.md.
"""

import argparse
import bisect
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
NM = ROOT / "prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf-nm"

SUMMARY = re.compile(r"perfmon: ([\d.]+) s, (\d+) cycles, (\d+) instructions")
# The 2 Mbaud console drops a byte now and then; match loosely.
COUNTER = re.compile(r"^\s+(I-cache|D-cache rd|D-cache wr|branch)\s+(\d+) "
                     r"\S*\s+(\d+) miss")
IRQ = re.compile(r"^\s+(\d+) (\S+)\s+(\d+)\s+(\d+)\s+[\d.]+%\s+\d+$")
RAW = re.compile(r"perfmon-raw base=0x([0-9a-f]+) shift=(\d+) buckets=\d+ "
                 r"samples=(\d+) idle=(\d+) outside=(\d+)")
BUCKET = re.compile(r"^([0-9a-f]+) (\d+)$")


def parse(path):
    run = {"counters": {}, "irqs": {}, "hist": {}}
    raw = False
    for line in Path(path).read_text(errors="replace").splitlines():
        if raw:
            m = BUCKET.match(line)
            if m:
                run["hist"][int(m.group(1), 16)] = int(m.group(2))
            elif line.startswith("perfmon-raw end"):
                raw = False
            continue
        m = SUMMARY.search(line)
        if m:
            run["cycles"] = int(m.group(2))
            run["instret"] = int(m.group(3))
        m = COUNTER.match(line)
        if m:
            run["counters"][m.group(1)] = (int(m.group(2)), int(m.group(3)))
        m = IRQ.match(line)
        if m:
            run["irqs"][(int(m.group(1)), m.group(2))] = (int(m.group(3)),
                                                           int(m.group(4)))
        m = RAW.search(line)
        if m:
            raw = True
            run["base"], run["shift"] = int(m.group(1), 16), int(m.group(2))
            run["samples"], run["idle"], run["outside"] = (
                int(m.group(3)), int(m.group(4)), int(m.group(5)))
    return run


def merge(runs):
    out = {"cycles": 0, "instret": 0, "counters": {}, "irqs": {}, "hist": {},
           "samples": 0, "idle": 0, "outside": 0, "nraw": 0}
    for r in runs:
        if "cycles" not in r:
            continue
        out["cycles"] += r["cycles"]
        out["instret"] += r["instret"]
        for k, (a, b) in r["counters"].items():
            x = out["counters"].get(k, (0, 0))
            out["counters"][k] = (x[0] + a, x[1] + b)
        for k, (a, b) in r["irqs"].items():
            x = out["irqs"].get(k, (0, 0))
            out["irqs"][k] = (x[0] + a, x[1] + b)
        if "base" in r:
            out["nraw"] += 1
            out["base"], out["shift"] = r["base"], r["shift"]
            for k in ("samples", "idle", "outside"):
                out[k] += r[k]
            for b, n in r["hist"].items():
                out["hist"][b] = out["hist"].get(b, 0) + n
    return out


def pct(a, b):
    return f"{a * 100 / b:.2f}%" if b else "-"


def print_counters(cases):
    print("| case | runs | IPC | I-cache miss | D-cache rd miss | "
          "D-cache wr miss | branch miss | IRQ+exception | idle |")
    print("| --- " * 9 + "|")
    for case, (n, c) in cases.items():
        cnt = c["counters"]

        def miss(k):
            return pct(cnt[k][1], cnt[k][0]) if k in cnt else "-"

        irq = sum(cy for _, cy in c["irqs"].values())
        print(f"| {case} | {n} | {c['instret'] / c['cycles']:.3f} | "
              f"{miss('I-cache')} | {miss('D-cache rd')} | "
              f"{miss('D-cache wr')} | {miss('branch')} | "
              f"{pct(irq, c['cycles']) if c['irqs'] else '-'} | "
              f"{pct(c['idle'], c['samples']) if c['nraw'] else '-'} |")

    irqs = {}
    for _, c in cases.values():
        for k, (_, cy) in c["irqs"].items():
            irqs[k] = irqs.get(k, 0) + cy / c["cycles"]
    top = sorted(irqs, key=lambda k: -irqs[k])[:6]
    if top:
        print("\n| IRQ (share of cycles, cycles/call) | " +
              " | ".join(cases) + " |")
        print("| --- " * (len(cases) + 1) + "|")
        for k in top:
            cells = []
            for _, c in cases.values():
                calls, cy = c["irqs"].get(k, (0, 0))
                cells.append(f"{pct(cy, c['cycles'])}, "
                             f"{cy // calls if calls else 0}")
            print(f"| {k[0]} {k[1]} | " + " | ".join(cells) + " |")


def load_map(path):
    """Input sections in .text: sorted [(addr, size, archive(object))]."""
    secs, pending = [], None
    for line in open(path, errors="replace"):
        m = re.match(r"^ (\.\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$",
                     line)
        if m:
            name, addr, size, obj = (m.group(1), int(m.group(2), 16),
                                     int(m.group(3), 16), m.group(4))
            pending = None
        else:
            m2 = re.match(r"^ (\.\S+)\s*$", line)
            if m2:
                pending = m2.group(1)
                continue
            m3 = re.match(r"^\s+0x([0-9a-f]{8,16})\s+0x([0-9a-f]+)\s+(\S.*)$",
                          line)
            if not (m3 and pending):
                pending = None
                continue
            name, addr, size, obj = (pending, int(m3.group(1), 16),
                                     int(m3.group(2), 16), m3.group(3))
            pending = None
        if size and name.startswith(".text") and 0x80000000 <= addr:
            secs.append((addr, size, obj.strip()))
    return sorted(secs)


def load_funcs(nm, elf):
    funcs = []
    out = subprocess.run([str(nm), "-n", "-S", elf], stdout=subprocess.PIPE,
                         text=True, check=True).stdout
    for line in out.splitlines():
        f = line.split()
        if len(f) == 4 and f[2] in "tTwW":
            funcs.append((int(f[0], 16), int(f[1], 16), f[3]))
    return funcs


WIFI_GLUE = ("bl616cl_wlan", "bl616cl_wifi")


def module(obj):
    name = re.search(r"\(([^)]*)\)", obj)
    name = name.group(1) if name else Path(obj).name
    if "ltrans" in obj or "libmacsw" in obj or "libwl80211_bl616cl" in obj:
        return "wifi-core(LTO)"
    if "libbl_wl80211" in obj:
        return "wifi-port(wl80211)"
    if "libbl_wpa_supplicant" in obj or "libmbedtls" in obj:
        return "supplicant/mbedtls"
    if "phyrf" in obj or "rfparam" in obj:
        return "phyrf"
    if "libarch.a" in obj:
        if name.startswith(WIFI_GLUE):
            return "wifi-glue"
        if name.startswith("bl616cl_chksum"):
            return "net"
        if name.startswith("riscv_") or "irq" in name or "head" in name:
            return "arch-irq/ctx"
        return "arch-other"
    for lib, mod in (("libsched.a", "sched"), ("libnet.a", "net"),
                     ("libfs.a", "fs"), ("libdrivers.a", "drivers"),
                     ("libgcc", "libgcc"), ("libapps_iperf", "iperf"),
                     ("libapps", "apps-other"), ("libbl_std", "lhal"),
                     ("libbl_lhal", "lhal")):
        if lib in obj:
            return mod
    if "libmm.a" in obj:
        return "mm-iob" if name.startswith("iob_") else "mm-heap"
    if "libc.a" in obj:
        return ("libc-mem" if re.search(r"memcpy|memset|memmove|memcmp|"
                                        r"strlen", name) else "libc")
    return "other:" + Path(obj.split("(")[0]).name


class Locator:
    def __init__(self, secs, funcs):
        self.secs, self.funcs = secs, funcs
        self.sa = [s[0] for s in secs]
        self.fa = [f[0] for f in funcs]

    def module(self, addr):
        i = bisect.bisect_right(self.sa, addr) - 1
        if i >= 0 and addr < self.sa[i] + self.secs[i][1]:
            return module(self.secs[i][2])
        return "unmapped"

    def func(self, addr):
        j = bisect.bisect_right(self.fa, addr) - 1
        if j >= 0 and addr < self.fa[j] + max(self.funcs[j][1], 1):
            return self.funcs[j][2]
        return "?"

    def overlapping(self, addr, size):
        j = max(bisect.bisect_right(self.fa, addr) - 1, 0)
        while j < len(self.funcs) and self.funcs[j][0] < addr + size:
            s, n, fn = self.funcs[j]
            if s + max(n, 1) > addr:
                yield s, n, fn
            j += 1


def print_profile(cases, loc, top):
    shares = {}
    for case, (_, c) in cases.items():
        if not c["nraw"]:
            continue
        busy = c["samples"] - c["idle"]
        got = sum(c["hist"].values())
        mods, fns = {}, {}
        for b, n in c["hist"].items():
            addr = c["base"] + (b << c["shift"])
            m, f = loc.module(addr), loc.func(addr)
            mods[m] = mods.get(m, 0) + n
            fns[(m, f)] = fns.get((m, f), 0) + n
        if c["outside"]:
            mods["ram code"] = c["outside"]
        shares[case] = (c["samples"], mods, fns)
        if got + c["outside"] != busy:
            print(f"note: {case}: {got + c['outside']} of {busy} busy "
                  f"samples parsed (console byte loss or saturation)")
    if not shares:
        return
    allm = sorted({m for _, ms, _ in shares.values() for m in ms},
                  key=lambda m: -sum(ms.get(m, 0) / s
                                     for s, ms, _ in shares.values()))
    print("\n| module (share of all ticks) | " + " | ".join(shares) + " |")
    print("| --- " * (len(shares) + 1) + "|")
    for m in allm:
        print(f"| {m} | " + " | ".join(pct(ms.get(m, 0), s)
                                       for s, ms, _ in shares.values())
              + " |")
    for case, (s, _, fns) in shares.items():
        print(f"\n{case}: top {top} functions (share of all ticks)")
        for (m, f), n in sorted(fns.items(), key=lambda kv: -kv[1])[:top]:
            print(f"  {n * 100 / s:5.1f}%  {m:20s} {f}")


def hot_list(ld_script):
    names, inside = set(), False
    for line in open(ld_script):
        if "__bl616cl_wifi_hot_start" in line:
            inside = True
        elif "__bl616cl_wifi_hot_end" in line:
            break
        elif inside:
            names.update(re.findall(r"\*\(\.text\.([A-Za-z0-9_]+) ", line))
    return names


def print_hot(cases, loc, regex, listed, min_share):
    agg, sizes, busy = {}, {}, 0
    for _, c in cases.values():
        if not c["nraw"]:
            continue
        busy += c["samples"] - c["idle"]
        step = 1 << c["shift"]
        for b, n in c["hist"].items():
            addr = c["base"] + (b << c["shift"])
            for s, size, fn in loc.overlapping(addr, step):
                if re.search(regex, loc.module(s)):
                    # LTO copies (.lto_priv/.isra/...) share the list line
                    base = fn.split(".")[0]
                    agg[base] = agg.get(base, 0) + n
                    sizes.setdefault(base, {})[s] = size
    sizes = {fn: sum(v.values()) for fn, v in sizes.items()}
    rows = sorted(agg.items(), key=lambda kv: -kv[1])
    limit = busy * min_share / 100
    shown = [(fn, n) for fn, n in rows if n >= limit]
    print(f"\n{len(rows)} functions in modules matching '{regex}' with "
          f"samples (bucket overlap); {len(shown)} at or above "
          f"{min_share}% of the busy samples:")
    print("   samples    size  cumulative  status  function")
    total = 0
    for fn, n in shown:
        total += sizes[fn]
        mark = "listed" if fn in listed else "NEW"
        print(f"  {n:8d}  {sizes[fn]:6d}  {total / 1024:6.1f} KiB  "
              f"{mark:6s}  {fn}")
    below = [fn for fn, _ in rows[len(shown):] if fn in listed]
    if below:
        print(f"\nlisted, below the threshold ({len(below)}): " +
              ", ".join(below))
    cold = sorted(listed - set(agg))
    if listed:
        print(f"\nlisted, no samples ({len(cold)}; interrupt handlers "
              f"never get tick samples): " + ", ".join(cold))
        new = [fn for fn, _ in shown if fn not in listed]
        if new:
            print("\nld.script lines for the NEW functions, to place by "
                  "call path:")
            for fn in new:
                print(f"    *(.text.{fn} .text.{fn}.*)")


def main():
    ap = argparse.ArgumentParser(
        description="Summarize perfmon stat/prof output per case.")
    ap.add_argument("files", nargs="+", type=Path)
    ap.add_argument("--map", help="nuttx.map of the measured image")
    ap.add_argument("--elf", help="final_nuttx of the measured image")
    ap.add_argument("--nm", default=str(NM))
    ap.add_argument("--top", type=int, default=15)
    ap.add_argument("--hot", metavar="MODULE_REGEX",
                    help="list hot-list candidates, e.g. 'wifi'")
    ap.add_argument("--ld-script", help="mark functions already listed")
    ap.add_argument("--min-share", type=float, default=0.1,
                    help="--hot threshold in %% of busy samples")
    args = ap.parse_args()

    groups = {}
    for f in args.files:
        m = re.search(r"(tcp-tx|udp-tx|tcp-rx|udp-rx|[A-Za-z0-9_]+)\.perfmon$",
                      f.name)
        groups.setdefault(m.group(1) if m else f.stem, []).append(parse(f))
    cases = {}
    for case, runs in groups.items():
        c = merge(runs)
        if c["cycles"]:
            cases[case] = (len(runs), c)
        else:
            print(f"note: {case}: no perfmon summary line found")
    if not cases:
        return 1
    print_counters(cases)

    if args.map and args.elf:
        loc = Locator(load_map(args.map), load_funcs(args.nm, args.elf))
        print_profile(cases, loc, args.top)
        if args.hot:
            listed = hot_list(args.ld_script) if args.ld_script else set()
            print_hot(cases, loc, args.hot, listed, args.min_share)
    elif args.hot:
        ap.error("--hot needs --map and --elf")
    return 0


if __name__ == "__main__":
    sys.exit(main())
