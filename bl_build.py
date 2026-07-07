#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build a BouffaloLab board through openvela's CMake path."""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path


def find_root(start: Path) -> Path:
    for current in (start, *start.parents):
        if (current / "build.sh").is_file() and (current / "nuttx").is_dir():
            return current

    raise SystemExit("could not find openvela root")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("defconfig")
    parser.add_argument("-j", "--jobs", default="14")
    parser.add_argument("--clean", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = find_root(Path.cwd())
    defconfig = args.defconfig
    config_name = Path(defconfig).name
    board_name = Path(defconfig).parents[1].name
    out_dir = root / "cmake_out" / f"{board_name}_{config_name}"

    if args.clean and out_dir.exists():
        shutil.rmtree(out_dir)
        return 0

    cmd = [
        "./build.sh",
        defconfig,
        "--cmake",
        f"-j{args.jobs}",
    ]

    return subprocess.run(cmd, cwd=root, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
