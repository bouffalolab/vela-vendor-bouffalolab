#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Create a boot2-loadable BL616CL application image."""

from __future__ import annotations

import argparse
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Optional, Sequence

HOST_TO_TOOL = {
    "Linux": "bflb_fw_post_proc-ubuntu",
    "Darwin": "bflb_fw_post_proc-macos",
    "Windows": "bflb_fw_post_proc.exe",
}

BOARD_CONFIG_FILES = (
    "bl_factory_params_IoTKitA_auto.dts",
    "boot2_bl616cl_isp_release_v8.2.1.bin",
    "partition_cfg_4M.toml",
)


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Postprocess a BL616CL NuttX raw binary"
    )
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--board-config", type=Path, required=True)
    return parser.parse_args(argv)


def main(
    argv: Optional[Sequence[str]] = None,
    *,
    system_name: Optional[str] = None,
) -> int:
    args = parse_args(argv)
    host = system_name or platform.system()
    tool_name = HOST_TO_TOOL.get(host)

    if tool_name is None:
        print(f"unsupported host platform: {host}", file=sys.stderr)
        return 1

    if not args.image.is_file():
        print(f"input image does not exist: {args.image}", file=sys.stderr)
        return 1

    if not args.board_config.is_dir():
        print(
            f"board config directory does not exist: {args.board_config}",
            file=sys.stderr,
        )
        return 1

    for config_name in BOARD_CONFIG_FILES:
        config_file = args.board_config / config_name
        if not config_file.is_file():
            print(
                f"required board config does not exist: {config_file}",
                file=sys.stderr,
            )
            return 1

    tool_dir = Path(__file__).resolve().parent.parent / "bflb_fw_post_proc"
    tool = tool_dir / tool_name
    if not tool.is_file():
        print(f"firmware postprocessor does not exist: {tool}", file=sys.stderr)
        return 1

    raw_image = args.image.with_name("nuttx.raw.bin")
    shutil.copy2(args.image, raw_image)

    with tempfile.TemporaryDirectory(prefix="bl616cl-postprocess-") as temp_dir:
        isolated_image = Path(temp_dir) / args.image.name
        shutil.copy2(args.image, isolated_image)
        command = [
            str(tool),
            "--chipname=bl616cl",
            f"--imgfile={isolated_image}",
            "--appkeys=shared",
            f"--brdcfgdir={args.board_config}",
        ]

        try:
            result = subprocess.run(command, check=False)
        except OSError as error:
            print(
                f"failed to execute firmware postprocessor: {error}",
                file=sys.stderr,
            )
            return 1

        if result.returncode != 0:
            return result.returncode

        if not isolated_image.is_file():
            print(
                f"firmware postprocessor removed its input: {isolated_image}",
                file=sys.stderr,
            )
            return 1

        staged_image = args.image.with_name(f".{args.image.name}.postprocessed")
        try:
            shutil.copy2(isolated_image, staged_image)
            staged_image.replace(args.image)
        except OSError as error:
            staged_image.unlink(missing_ok=True)
            print(
                f"failed to install postprocessed image: {error}",
                file=sys.stderr,
            )
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
