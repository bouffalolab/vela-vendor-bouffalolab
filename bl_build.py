#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build a BouffaloLab board through openvela's CMake path."""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
from pathlib import Path
from typing import Dict

EXTRA_FLAGS = "-Wno-cpp -Wno-deprecated-declarations"
TOOLCHAIN_ARCHES = (
    "xtensa",
    "arm",
    "aarch64",
    "riscv",
    "x86_64",
    "tc32",
    "tricore",
)


def find_root(start: Path) -> Path:
    for current in (start, *start.parents):
        if (current / "build.sh").is_file() and (current / "nuttx").is_dir():
            return current

    raise SystemExit("could not find openvela root")


def build_dir(root: Path, defconfig: str) -> Path:
    path = Path(defconfig)
    config_name = path.name
    board_name = path.parents[1].name

    return root / "cmake_out" / f"{board_name}_{config_name}"


def cmake_board_config(root: Path, defconfig: str) -> str:
    path = Path(defconfig)

    if path.is_absolute():
        return str(path)

    if (root / path).is_dir():
        return f"../{path.as_posix().removeprefix('./')}"

    return defconfig


def build_env(root: Path) -> Dict[str, str]:
    env = dict(os.environ)
    system = platform.system().lower()
    machine = platform.machine().replace("arm64", "aarch64")
    host = f"{system}-{machine}"
    paths = [root]
    kconfig_bin = root / "prebuilts" / "kconfig-frontends" / "bin"
    python_paths = []

    paths.append(root / "prebuilts" / "tools" / "python" / "bin")
    paths.append(kconfig_bin)

    for arch in TOOLCHAIN_ARCHES:
        paths.append(
            root / "prebuilts" / "gcc" / host / f"{arch}-none-linux-gnu" / "bin"
        )
        for abi in ("eabi", "elf"):
            paths.append(root / "prebuilts" / "gcc" / host / f"{arch}-{abi}" / "bin")
            paths.append(
                root / "prebuilts" / "gcc" / host / f"{arch}-none-{abi}" / "bin"
            )

    paths.extend(
        [
            root / "prebuilts" / "build-tools" / host / "bin",
            root / "prebuilts" / "tools" / system / machine,
            root / "prebuilts" / "tools" / "cmake" / "bin",
            root / "prebuilts" / "tools" / "ninja" / "bin",
        ]
    )

    for package in (
        "pyelftools",
        "cxxfilt",
        "Mako",
        "ply",
        "jsonpath",
        "kconfiglib",
        "construct",
    ):
        python_paths.append(
            root / "prebuilts" / "tools" / "python" / "dist-packages" / package
        )

    existing_path = env.get("PATH", "")
    env["PATH"] = os.pathsep.join(str(path) for path in paths if Path(path).is_dir())
    env["PATH"] = f"{env['PATH']}{os.pathsep}{existing_path}"

    existing_pythonpath = env.get("PYTHONPATH", "")
    env["PYTHONPATH"] = os.pathsep.join(
        str(path) for path in python_paths if Path(path).exists()
    )
    env["PYTHONPATH"] = f"{env['PYTHONPATH']}{os.pathsep}{existing_pythonpath}"

    return env


def cmake_configure(root: Path, out_dir: Path, defconfig: str, use_lib: str) -> int:
    lib_components = ";".join(
        component.strip() for component in use_lib.split(",") if component.strip()
    )
    cmd = [
        "cmake",
        "-B",
        str(out_dir),
        "-S",
        str(root / "nuttx"),
        f"-DBOARD_CONFIG={cmake_board_config(root, defconfig)}",
        f"-DCUSTOM_MODULE_PATH={root / 'build' / 'cmake'}",
        f"-DEXTRA_FLAGS={EXTRA_FLAGS}",
        f"-DBL_USE_LIB_COMPONENTS={lib_components}",
        "-GNinja",
    ]

    if shutil.which("ccache"):
        cmd.extend(
            [
                "-DCMAKE_C_COMPILER_LAUNCHER=ccache",
                "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache",
            ]
        )

    return subprocess.run(cmd, cwd=root, env=build_env(root), check=False).returncode


def cmake_build(root: Path, out_dir: Path, jobs: str) -> int:
    cmd = ["cmake", "--build", str(out_dir), f"-j{jobs}", "--"]
    return subprocess.run(cmd, cwd=root, env=build_env(root), check=False).returncode


def cmake_menuconfig(root: Path, out_dir: Path) -> int:
    cmd = ["cmake", "--build", str(out_dir), "-t", "menuconfig"]
    return subprocess.run(cmd, cwd=root, env=build_env(root), check=False).returncode


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("defconfig")
    parser.add_argument("-j", "--jobs", default="14")
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--menuconfig", action="store_true")
    parser.add_argument("--use-lib", default="")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = find_root(Path(__file__).resolve())
    defconfig = args.defconfig
    out_dir = build_dir(root, defconfig)

    if args.clean:
        if out_dir.exists():
            shutil.rmtree(out_dir)
        return 0

    ret = cmake_configure(root, out_dir, defconfig, args.use_lib)
    if ret != 0:
        return ret

    if args.menuconfig:
        return cmake_menuconfig(root, out_dir)

    return cmake_build(root, out_dir, args.jobs)


if __name__ == "__main__":
    raise SystemExit(main())
