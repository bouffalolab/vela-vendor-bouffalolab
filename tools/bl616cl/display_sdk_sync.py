#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Keep components/display in sync with the Bouffalo SDK.

components/display/sdk_sync.txt lists every SDK file under
bsp/common/display and bsp/common/touch with its git blob id at the last
sync and what the port did with it:

  same     copied unchanged; update copies the new version
  patched  copied with local changes; update merges them (git merge-file)
  ported   the logic lives in the listed Vela files; port the SDK diff by
           hand
  skipped  not used by the port

When update leaves a file to finish by hand (a merge conflict or a ported
file), its line records the SDK version it was left at as OLD..NEW. Once
the files are done, resolve moves those lines to NEW, that version and not
the one the SDK may have reached since; it changes nothing while any of
their Vela files is missing or holds conflict markers. update refuses to
run until then.

A line indented by four spaces under an entry is a note on a Vela change
to keep when porting that file (marked "Vela:" in the code); status and
update print it with the SDK change.

Commands (DIR is an SDK git repository, REV any commit or tag in it, best a
release tag; only git objects are read, never the SDK working tree):

  status --sdk DIR [--rev REV]   SDK changes since the last sync
  update --sdk DIR [--rev REV]   copy, merge, and record what is left
  resolve                        the files left by update are done
  check                          the copied files still equal the SDK

update checks all files before writing any. The "synced" header moves only
when an update leaves nothing to do.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

SDK_DIRS = ["bsp/common/display", "bsp/common/touch"]
MANIFEST = "components/display/sdk_sync.txt"
MODES = ("same", "patched", "ported", "skipped")
MARKERS = re.compile(rb"^(<{7} |={7}$|>{7} )", re.MULTILINE)


def git(*args, cwd=None, data=None):
    return subprocess.run(["git", *args], cwd=cwd, input=data,
                          capture_output=True, check=True).stdout


def sdk_tree(sdk, rev):
    out = git("-C", sdk, "ls-tree", "-r", rev, "--", *SDK_DIRS).decode()
    tree = {}
    for line in out.splitlines():
        meta, path = line.split("\t", 1)
        tree[path] = meta.split()[2]
    return tree


def blob(sdk, blob_id):
    return git("-C", sdk, "cat-file", "blob", blob_id)


def has_blob(sdk, blob_id):
    return subprocess.run(["git", "-C", sdk, "cat-file", "-e", blob_id],
                          capture_output=True).returncode == 0


def hash_file(path):
    return git("hash-object", str(path)).decode().strip()


def has_markers(path):
    return MARKERS.search(path.read_bytes()) is not None


def marked(root, e):
    """Vela files of a merged or ported entry that hold conflict markers"""
    if e["mode"] not in ("patched", "ported"):
        return []
    return [v for v in e["vela"]
            if (root / v).is_file() and has_markers(root / v)]


class Manifest:
    def __init__(self, root):
        self.path = root / MANIFEST
        self.header = []
        self.entries = []
        for line in self.path.read_text().splitlines():
            if line.startswith("    ") and self.entries:
                self.entries[-1]["notes"].append(line.strip())
                continue
            if not line.strip() or line.startswith("#"):
                self.header.append(line)
                continue
            mode, blob_id, sdk_path, *vela = line.split()
            if mode not in MODES:
                sys.exit(f"{self.path}: unknown mode in: {line}")
            blob_id, _, pending = blob_id.partition("..")
            self.entries.append({"mode": mode, "blob": blob_id,
                                 "pending": pending, "path": sdk_path,
                                 "vela": vela[0].split(",") if vela else [],
                                 "notes": []})

    def save(self, synced=None):
        lines = [synced if synced and h.startswith("# synced:") else h
                 for h in self.header]
        for e in sorted(self.entries, key=lambda e: e["path"]):
            blob_id = e["blob"] + (".." + e["pending"] if e["pending"]
                                   else "")
            row = f"{e['mode']:<8} {blob_id} {e['path']}"
            if e["vela"]:
                row += " " + ",".join(e["vela"])
            lines.append(row)
            lines += ["    " + n for n in e["notes"]]
        self.path.write_text("\n".join(lines) + "\n")


def compare(manifest, tree):
    known = {e["path"] for e in manifest.entries}
    changed = [(e, tree[e["path"]]) for e in manifest.entries
               if e["path"] in tree and tree[e["path"]] != e["blob"]]
    removed = [e for e in manifest.entries if e["path"] not in tree]
    added = sorted(p for p in tree if p not in known)
    return changed, removed, added


def report(sdk, rev, changed, removed, added):
    print(f"SDK {sdk} at {rev}")
    if not (changed or removed or added):
        print("  no change since the last sync")
    for mode in MODES:
        for e, new in (c for c in changed if c[0]["mode"] == mode):
            print(f"  {mode:<8} {e['path']}")
            for note in e["notes"]:
                print(f"           Vela change to keep: {note}")
            if mode == "ported":
                print(f"           port into {', '.join(e['vela'])}:")
                if has_blob(sdk, e["blob"]):
                    print(f"           git -C {sdk} diff {e['blob'][:12]} "
                          f"{new[:12]}")
                else:
                    print("           the synced version is not in this "
                          "repository; diff it in one that has the synced "
                          "commit")
    for e in removed:
        print(f"  removed  {e['path']} ({e['mode']})")
    for path in added:
        print(f"  new      {path}: classify it and add a line to "
              f"{MANIFEST}")


def cmd_status(args, root):
    manifest = Manifest(root)
    report(args.sdk, args.rev, *compare(manifest, sdk_tree(args.sdk,
                                                           args.rev)))
    return 0


def update_problems(sdk, root, manifest, changed):
    problems = [f"{e['path']}: left at {e['pending'][:12]}; finish it, then "
                f"run resolve" for e in manifest.entries if e["pending"]]
    problems += [f"{root / v}: conflict markers"
                 for e in manifest.entries for v in marked(root, e)]
    for e, _ in changed:
        if e["mode"] not in ("same", "patched"):
            continue
        target = root / e["vela"][0]
        if not target.is_file():
            problems.append(f"{target}: missing")
        elif e["mode"] == "same" and hash_file(target) != e["blob"]:
            problems.append(f"{target}: changed locally; mark it patched "
                            f"or restore it")
        elif e["mode"] == "patched" and not has_blob(sdk, e["blob"]):
            problems.append(f"{target}: the synced version is not in "
                            f"{sdk}; cannot merge")
    return problems


def merge(sdk, e, new, target):
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp) / "base"
        theirs = Path(tmp) / "theirs"
        base.write_bytes(blob(sdk, e["blob"]))
        theirs.write_bytes(blob(sdk, new))
        return subprocess.run(
            ["git", "merge-file", "-L", "vela", "-L", "old sdk", "-L",
             "new sdk", str(target), str(base), str(theirs)]).returncode


def cmd_update(args, root):
    manifest = Manifest(root)
    changed, removed, added = compare(manifest, sdk_tree(args.sdk,
                                                         args.rev))
    report(args.sdk, args.rev, changed, removed, added)
    problems = update_problems(args.sdk, root, manifest, changed)
    if problems:
        print("\n".join(problems))
        print("nothing updated")
        return 1
    left = failed = 0
    for e, new in changed:
        target = root / e["vela"][0] if e["vela"] else None
        if e["mode"] == "same":
            target.write_bytes(blob(args.sdk, new))
        elif e["mode"] == "patched":
            ret = merge(args.sdk, e, new, target)
            if not 0 <= ret < 128:
                print(f"  FAILED   {target}: git merge-file failed")
                failed += 1
                continue
            if ret > 0:
                print(f"  CONFLICT {target}: resolve the markers, then run "
                      f"resolve")
                e["pending"] = new
                left += 1
                continue
        elif e["mode"] == "ported":
            print(f"  PORT     {e['path']}: port it, then run resolve")
            e["pending"] = new
            left += 1
            continue
        e["blob"] = new
    done = not (left or failed or removed or added)
    if changed or done:
        manifest.save(f"# synced: {args.rev}" if done else None)
    if removed or added:
        print("removed/new SDK files: edit the manifest by hand")
    return 0 if done else 1


def cmd_resolve(_args, root):
    manifest = Manifest(root)
    pending = [e for e in manifest.entries if e["pending"]]
    problems = [f"  MISSING  {v}" for e in pending for v in e["vela"]
                if not (root / v).is_file()]
    problems += [f"  CONFLICT {v}: markers left" for e in pending
                 for v in marked(root, e)]
    if problems:
        print("\n".join(problems))
        print("nothing resolved")
        return 1
    for e in pending:
        print(f"  resolved {e['path']} at {e['pending'][:12]}")
        e["blob"], e["pending"] = e["pending"], ""
    if pending:
        manifest.save()
        print("run update again to finish the sync")
    return 0


def cmd_check(_args, root):
    bad = 0
    for e in Manifest(root).entries:
        for vela in e["vela"]:
            if not (root / vela).is_file():
                print(f"missing  {vela} ({e['path']})")
                bad += 1
        if not e["vela"] or not (root / e["vela"][0]).is_file():
            continue
        first = root / e["vela"][0]
        if e["mode"] == "same" and hash_file(first) != e["blob"]:
            print(f"changed  {e['vela'][0]}: differs from the SDK; mark "
                  f"it patched or restore it")
            bad += 1
        for vela in marked(root, e):
            print(f"conflict {vela}: resolve the markers")
            bad += 1
        if e["pending"]:
            print(f"pending  {e['path']}: finish it, then run resolve")
            bad += 1
    print("check: " + ("OK" if not bad else f"{bad} problem(s)"))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--root", type=Path,
                    default=Path(__file__).resolve().parents[2],
                    help="vendor/bouffalolab (default: this tree)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("status", "update"):
        p = sub.add_parser(name)
        p.add_argument("--sdk", required=True, help="SDK git repository")
        p.add_argument("--rev", default="HEAD", help="SDK commit or tag")
    sub.add_parser("resolve")
    sub.add_parser("check")
    args = ap.parse_args()
    if getattr(args, "sdk", None):
        args.sdk = os.path.abspath(args.sdk)
    return {"status": cmd_status, "update": cmd_update,
            "resolve": cmd_resolve,
            "check": cmd_check}[args.cmd](args, args.root.resolve())


if __name__ == "__main__":
    sys.exit(main())
