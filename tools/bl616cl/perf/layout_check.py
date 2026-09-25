#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check that the Wi-Fi hot list in ld.script still matches the image.

The list names input sections (.text.<function>).  When wl80211, macsw or
the glue renames, inlines or drops a function, its line silently matches
nothing and the layout gain shrinks.  This reads the list between
__bl616cl_wifi_hot_start and __bl616cl_wifi_hot_end in the linker script,
finds the placed sections in the linker map and warns about names that no
longer exist.  The build runs it after linking a Wi-Fi image; see
docs/bl616cl-hot-code-layout.md.
"""

import argparse
import re
import sys

ENTRY = re.compile(r"^\s*\*\(\.text\.([A-Za-z0-9_]+) \.text\.\1\.\*\)")
SECTION = re.compile(r"^ \.text\.(\S+)(?:\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s.*)?$")
SECTION_CONT = re.compile(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s")
SYMBOL = re.compile(r"^\s+0x([0-9a-f]+)\s+(__bl616cl_wifi_hot_(?:start|end)) = ")
ICACHE = 32 * 1024


def hot_list(path):
    names, inside = [], False
    for line in open(path):
        if "__bl616cl_wifi_hot_start" in line:
            inside = True
        elif "__bl616cl_wifi_hot_end" in line:
            break
        elif inside:
            m = ENTRY.match(line)
            if m:
                names.append(m.group(1))
    return names


def placed_sections(path):
    """Return {section suffix: [(addr, size)]} and the hot range."""
    secs, marks, pending = {}, {}, None
    for line in open(path, errors="replace"):
        m = SYMBOL.match(line)
        if m:
            marks[m.group(2)] = int(m.group(1), 16)
            continue
        m = SECTION.match(line)
        if m:
            if m.group(2):
                secs.setdefault(m.group(1), []).append(
                    (int(m.group(2), 16), int(m.group(3), 16)))
                pending = None
            else:
                pending = m.group(1)
            continue
        m = SECTION_CONT.match(line)
        if m and pending:
            secs.setdefault(pending, []).append(
                (int(m.group(1), 16), int(m.group(2), 16)))
        pending = None
    return secs, marks.get("__bl616cl_wifi_hot_start"), marks.get(
        "__bl616cl_wifi_hot_end")


def main():
    ap = argparse.ArgumentParser(
        description="Check the Wi-Fi hot list against the linker map.")
    ap.add_argument("--ld-script", required=True)
    ap.add_argument("--map", required=True)
    ap.add_argument("--strict", action="store_true",
                    help="exit 1 when an entry matches nothing")
    args = ap.parse_args()

    names = hot_list(args.ld_script)
    secs, start, end = placed_sections(args.map)
    if not names or start is None or end is None:
        print("bl616cl layout: no Wi-Fi hot list in the linker script or map")
        return 1 if args.strict else 0

    missing, outside = [], []
    for n in names:
        hits = [(a, s) for k, v in secs.items()
                if k == n or k.startswith(n + ".") for a, s in v if s]
        if not hits:
            missing.append(n)
        elif not all(start <= a < end for a, _ in hits):
            outside.append(n)

    size = end - start
    print(f"bl616cl layout: {len(names) - len(missing)}/{len(names)} Wi-Fi "
          f"hot entries placed at 0x{start:08x}, {size / 1024:.1f} KiB "
          f"(I-cache {ICACHE // 1024} KiB)")
    if missing:
        print("bl616cl layout: WARNING: hot list entries missing from the "
              "image (renamed, inlined or removed?): " + ", ".join(missing))
    if outside:
        print("bl616cl layout: WARNING: placed outside the hot range: " +
              ", ".join(outside))
    return 1 if args.strict and (missing or outside) else 0


if __name__ == "__main__":
    sys.exit(main())
