#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests of display_sdk_sync.py on a throwaway SDK repository.

A manifest line must move to a new SDK version only when its file is done,
and to the version the file was done against: a conflict, a port left to
do, a failed merge or a local edit must not lose the place where the next
sync has to start. Run: python3 display_sdk_sync_test.py
"""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parent / "display_sdk_sync.py"
D = "bsp/common/display"
SAME = "same.c"
PATCHED = "patched.h"
PORTED = "ported.c"
V1_PATCHED = "a\nb\nc\nd\ne\nf\n"
CONFLICT = V1_PATCHED.replace("b\n", "b sdk\n")
RESOLVED = V1_PATCHED.replace("b\n", "b sdk vela\n")


def sh(*args, cwd):
    return subprocess.run(args, cwd=cwd, check=True, capture_output=True,
                          text=True).stdout.strip()


class SyncTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        top = Path(self.tmp.name)
        self.sdk = top / "sdk"
        self.root = top / "vendor"
        (self.sdk / D).mkdir(parents=True)
        sh("git", "init", "-q", cwd=self.sdk)
        self.commit({SAME: "one\n", PATCHED: V1_PATCHED, PORTED: "x\n"},
                    "v1")
        comp = self.root / "components/display"
        comp.mkdir(parents=True)
        (comp / SAME).write_text("one\n")
        (comp / PATCHED).write_text(V1_PATCHED.replace("b\n", "b vela\n"))
        (comp / "port.c").write_text("x, vela\n")
        lines = ["# synced: v1"]
        for name, mode, vela in ((SAME, "same", SAME),
                                 (PATCHED, "patched", PATCHED),
                                 (PORTED, "ported", "port.c")):
            lines.append(f"{mode} {self.blob('v1', name)} {D}/{name} "
                         f"components/display/{vela}")
            if name == PATCHED:
                lines.append("    keep the vela line")
        (comp / "sdk_sync.txt").write_text("\n".join(lines) + "\n")

    def tearDown(self):
        self.tmp.cleanup()

    def commit(self, files, tag):
        for name, text in files.items():
            (self.sdk / D / name).write_text(text)
        sh("git", "add", "-A", cwd=self.sdk)
        sh("git", "-c", "user.name=t", "-c", "user.email=t@t", "commit",
           "-q", "-m", tag, cwd=self.sdk)
        sh("git", "tag", tag, cwd=self.sdk)

    def blob(self, tag, name):
        return sh("git", "rev-parse", f"{tag}:{D}/{name}", cwd=self.sdk)

    def tool(self, *args, rev="v2"):
        cmd = [sys.executable, str(TOOL), "--root", str(self.root), *args]
        if args[0] in ("status", "update"):
            cmd += ["--sdk", str(self.sdk), "--rev", rev]
        return subprocess.run(cmd, capture_output=True, text=True)

    def manifest(self):
        return (self.root / "components/display/sdk_sync.txt").read_text()

    def vela(self, name):
        return self.root / "components/display" / name

    def test_clean_update_moves_baseline(self):
        self.commit({SAME: "two\n", PATCHED: V1_PATCHED + "g\n"}, "v2")
        self.assertEqual(self.tool("update").returncode, 0)
        self.assertEqual(self.vela(SAME).read_text(), "two\n")
        self.assertIn("b vela\n", self.vela(PATCHED).read_text())
        self.assertIn("g\n", self.vela(PATCHED).read_text())
        self.assertIn("# synced: v2", self.manifest())
        self.assertIn("    keep the vela line\n", self.manifest())
        self.assertIn("no change", self.tool("status").stdout)

    def test_conflict_keeps_baseline_until_resolved(self):
        self.commit({PATCHED: CONFLICT}, "v2")
        old, new = self.blob("v1", PATCHED), self.blob("v2", PATCHED)
        self.assertNotEqual(self.tool("update").returncode, 0)
        self.assertIn(f"{old}..{new} {D}/{PATCHED}", self.manifest())
        status = self.tool("status").stdout
        self.assertIn(PATCHED, status)
        self.assertIn("Vela change to keep: keep the vela line", status)
        self.assertNotEqual(self.tool("check").returncode, 0)
        # A second update must not merge into the conflicted file again
        conflicted = self.vela(PATCHED).read_text()
        self.assertNotEqual(self.tool("update").returncode, 0)
        self.assertEqual(self.vela(PATCHED).read_text(), conflicted)
        self.vela(PATCHED).write_text(RESOLVED)
        self.assertEqual(self.tool("resolve").returncode, 0)
        self.assertEqual(self.tool("update").returncode, 0)
        self.assertIn("# synced: v2", self.manifest())
        self.assertIn("no change", self.tool("status").stdout)
        self.assertEqual(self.tool("check").returncode, 0)

    def test_resolve_records_the_version_merged(self):
        # Resolved against v2 while the SDK moved on to v3: the line must
        # say v2, so that the next update brings in what v3 added.
        self.commit({PATCHED: CONFLICT}, "v2")
        self.tool("update")
        self.vela(PATCHED).write_text(RESOLVED)
        self.commit({PATCHED: CONFLICT + "g\n"}, "v3")
        self.assertEqual(self.tool("resolve").returncode, 0)
        self.assertIn(f"{self.blob('v2', PATCHED)} {D}/{PATCHED}",
                      self.manifest())
        self.assertIn(PATCHED, self.tool("status", rev="v3").stdout)
        self.assertEqual(self.tool("update", rev="v3").returncode, 0)
        self.assertEqual(self.vela(PATCHED).read_text(), RESOLVED + "g\n")
        self.assertIn("no change", self.tool("status", rev="v3").stdout)

    def test_markers_left_write_nothing(self):
        self.commit({SAME: "two\n", PATCHED: CONFLICT}, "v2")
        self.tool("update")
        self.commit({SAME: "three\n"}, "v3")
        before = self.manifest()
        self.assertNotEqual(self.tool("resolve").returncode, 0)
        self.assertEqual(self.manifest(), before)
        self.assertNotEqual(self.tool("update", rev="v3").returncode, 0)
        self.assertEqual(self.vela(SAME).read_text(), "two\n")
        self.assertEqual(self.manifest(), before)

    def test_resolve_checks_every_file_left(self):
        # A resolved patched file and an unfinished ported one: neither
        # line may move while the ported target is missing or marked.
        self.commit({PATCHED: CONFLICT, PORTED: "y\n"}, "v2")
        self.tool("update")
        self.vela(PATCHED).write_text(RESOLVED)
        before = self.manifest()
        port = self.vela("port.c")
        port.write_text("<<<<<<< vela\nx, vela\n=======\ny\n>>>>>>> sdk\n")
        self.assertNotEqual(self.tool("resolve").returncode, 0)
        self.assertEqual(self.manifest(), before)
        self.assertIn("conflict components/display/port.c",
                      self.tool("check").stdout)
        self.assertNotEqual(self.tool("update").returncode, 0)
        self.assertEqual(self.manifest(), before)
        port.unlink()
        self.assertNotEqual(self.tool("resolve").returncode, 0)
        self.assertEqual(self.manifest(), before)
        port.write_text("y, vela\n")
        self.assertEqual(self.tool("resolve").returncode, 0)
        self.assertEqual(self.tool("update").returncode, 0)
        self.assertEqual(self.tool("check").returncode, 0)

    def test_ported_file_waits_for_resolve(self):
        self.commit({PORTED: "y\n"}, "v2")
        self.assertNotEqual(self.tool("update").returncode, 0)
        self.assertIn(PORTED, self.tool("status").stdout)
        self.assertNotEqual(self.tool("check").returncode, 0)
        self.assertEqual(self.tool("resolve").returncode, 0)
        self.assertEqual(self.tool("update").returncode, 0)
        self.assertIn("no change", self.tool("status").stdout)

    def test_missing_target_writes_nothing(self):
        self.commit({SAME: "two\n", PATCHED: V1_PATCHED + "g\n"}, "v2")
        self.vela(PATCHED).unlink()
        before = self.manifest()
        result = self.tool("update")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing", result.stdout)
        self.assertEqual(self.vela(SAME).read_text(), "one\n")
        self.assertEqual(self.manifest(), before)

    def test_local_edit_of_same_file_is_kept(self):
        self.commit({SAME: "two\n"}, "v2")
        self.vela(SAME).write_text("one, edited\n")
        before = self.manifest()
        result = self.tool("update")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("changed locally", result.stdout)
        self.assertEqual(self.vela(SAME).read_text(), "one, edited\n")
        self.assertEqual(self.manifest(), before)


if __name__ == "__main__":
    unittest.main()
