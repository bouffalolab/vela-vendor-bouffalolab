#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build and flash a BouffaloLab board through openvela's CMake path."""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, Optional, Sequence

EXTRA_FLAGS = "-Wno-cpp -Wno-deprecated-declarations"
FLASH_SIZE = 0x400000
MFG_OFFSET = 0x210000
MFG_SIZE = 0x1F0000
WHOLE_IMAGE_MAGICS = {
    0x000000: bytes.fromhex("42464e50"),
    0x00E000: bytes.fromhex("42465054"),
    0x00F000: bytes.fromhex("42465054"),
    0x010000: bytes.fromhex("42464e50"),
}
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


def validate_whole_image(image: Path) -> None:
    if not image.is_file():
        raise FileNotFoundError(f"whole image does not exist: {image}")

    if image.stat().st_size != FLASH_SIZE:
        raise ValueError(f"whole image must be exactly 4 MiB: {image}")

    with image.open("rb") as stream:
        for offset, expected in WHOLE_IMAGE_MAGICS.items():
            stream.seek(offset)
            if stream.read(len(expected)) != expected:
                raise ValueError(f"whole image layout is invalid: {image}")

        stream.seek(MFG_OFFSET)
        remaining = MFG_SIZE
        while remaining:
            chunk = stream.read(min(remaining, 64 * 1024))
            if any(byte != 0xFF for byte in chunk):
                raise ValueError(
                    f"whole image MFG partition is not erased: {image}"
                )
            remaining -= len(chunk)


def flash_tool_name(
    system: Optional[str] = None, machine: Optional[str] = None
) -> str:
    system = system or platform.system()
    machine = (machine or platform.machine()).lower()
    if system.startswith("Linux"):
        if machine in ("x86_64", "amd64"):
            return "BLFlashCommand-ubuntu"
        if machine == "arm" or machine.startswith("armv"):
            return "BLFlashCommand-arm"
        raise RuntimeError(f"unsupported Linux architecture: {machine}")
    if system.startswith("Darwin"):
        return "BLFlashCommand-macos"
    if system.startswith(("Windows", "MINGW", "MSYS", "CYGWIN")):
        return "BLFlashCommand.exe"
    raise RuntimeError(f"unsupported host platform: {system}")


def flash_whole_image(
    root: Path, image: Path, port: str, baudrate: int
) -> int:
    image = image.resolve()
    validate_whole_image(image)

    flash_tool_dir = (
        root
        / "vendor"
        / "bouffalolab"
        / "tools"
        / "bouffalo_flash_cube"
    )
    tool_name = flash_tool_name()
    flash_tool = flash_tool_dir / tool_name
    required_files = (
        flash_tool,
        flash_tool_dir
        / "chips"
        / "bl616cl"
        / "eflash_loader"
        / "eflash_loader_cfg.conf",
        flash_tool_dir
        / "chips"
        / "bl616cl"
        / "efuse_bootheader"
        / "efuse_bootheader_cfg.conf",
        flash_tool_dir
        / "chips"
        / "bl616cl"
        / "efuse_bootheader"
        / "flash_para.bin",
    )
    for path in required_files:
        if not path.is_file():
            raise FileNotFoundError(
                f"required flash runtime asset does not exist: {path}"
            )

    with tempfile.TemporaryDirectory(prefix="bl616cl-flash.") as temp_dir:
        stage = Path(temp_dir)
        staged_tool = stage / tool_name
        shutil.copy2(flash_tool, staged_tool)
        shutil.copytree(flash_tool_dir / "chips", stage / "chips")
        staged_tool.chmod(staged_tool.stat().st_mode | 0o100)

        cmd = [
            str(staged_tool),
            "--interface=uart",
            "--chipname=bl616cl",
            f"--port={port}",
            f"--baudrate={baudrate}",
            f"--firmware={image}",
            "--reset",
        ]
        return subprocess.run(
            cmd, cwd=root, env=build_env(root), check=False
        ).returncode


def select_flash_image(
    root: Path, defconfig: Optional[str], image: Optional[Path]
) -> Path:
    if image is not None:
        return image

    if defconfig is not None:
        return build_dir(root, defconfig) / "nuttx.whole.bin"

    candidates = sorted(
        path
        for path in (root / "cmake_out").glob("*/nuttx.whole.bin")
        if path.is_file()
    )
    if not candidates:
        raise FileNotFoundError(
            f"no flash image found under {root / 'cmake_out'}; "
            "build first or use --image"
        )
    if len(candidates) > 1:
        listing = "\n".join(f"  {path}" for path in candidates)
        raise ValueError(
            "multiple flash images found; specify defconfig or --image:\n"
            f"{listing}"
        )

    return candidates[0]


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("defconfig", nargs="?")
    parser.add_argument("-j", "--jobs", default="14")
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--menuconfig", action="store_true")
    parser.add_argument("--use-lib", default="")
    parser.add_argument("--flash", action="store_true")
    parser.add_argument("--image", type=Path)
    parser.add_argument("--port")
    parser.add_argument("--baudrate", type=int, default=2_000_000)
    args = parser.parse_args(argv)

    if not args.flash and args.defconfig is None:
        parser.error("defconfig is required unless --flash is used")
    if args.image is not None and not args.flash:
        parser.error("--image requires --flash")
    if args.flash and not args.port:
        parser.error("--port is required with --flash")
    if args.flash and (args.clean or args.menuconfig):
        parser.error("--flash cannot be used with --clean or --menuconfig")

    return args


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    root = find_root(Path(__file__).resolve())

    if args.flash:
        try:
            image = select_flash_image(root, args.defconfig, args.image)
            return flash_whole_image(root, image, args.port, args.baudrate)
        except (OSError, RuntimeError, ValueError) as error:
            print(f"bl_build: {error}", file=sys.stderr)
            return 1

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
