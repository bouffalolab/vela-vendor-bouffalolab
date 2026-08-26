#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build and flash a BouffaloLab board through openvela's CMake path."""

from __future__ import annotations

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, Optional, Sequence

EXTRA_FLAGS = "-Wno-cpp -Wno-deprecated-declarations"
BOARD_ROOT = "vendor/bouffalolab/boards"
COMMANDS = ("build", "clean", "menuconfig", "flash", "completion", "__complete")
COMMAND_OPTIONS = {
    "build": ("--help", "-j", "--jobs", "--use-lib"),
    "clean": ("--help",),
    "menuconfig": ("--help",),
    "flash": (
        "--help",
        "--addr",
        "--config",
        "--board",
        "--image",
        "--port",
        "--baudrate",
    ),
    "completion": ("--help",),
}
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


def default_jobs() -> str:
    """Default parallelism: half of the machine's cores, at least 1."""
    return str(max(1, (os.cpu_count() or 1) // 2))


def find_root(start: Path) -> Path:
    for current in (start, *start.parents):
        if (current / "build.sh").is_file() and (current / "nuttx").is_dir():
            return current

    raise SystemExit("could not find openvela root")


def resolve_board(root: Path, target: str) -> Path:
    """Resolve a board target to its configs directory (absolute path).

    Accepted forms (only the first two carry the full prefix):
      <absolute path>
      vendor/bouffalolab/boards/<chip>/<board>/configs/<name>  (legacy full path)
      <chip>/<board>/configs/<name>                            (prefix omitted)
      <board>/configs/<name> or <board>/<name>                 (chip omitted)
      <name>                                                   (config name only)

    The shorter forms are accepted only when they match exactly one board.
    """
    path = Path(target)
    if path.is_absolute():
        if path.is_dir():
            return path.resolve()
        raise ValueError(f"board target not found: {target}")

    if not re.fullmatch(r"[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*", target):
        raise ValueError(f"invalid board target: {target!r}")

    # Strip a redundant full prefix so all forms match uniformly.
    if target.startswith(BOARD_ROOT + "/"):
        path = Path(target[len(BOARD_ROOT) + 1 :])

    boards_root = Path(BOARD_ROOT)
    patterns = [boards_root / path, boards_root / "*" / path]
    if "/" not in target:
        patterns.append(boards_root / "*" / "*" / "configs" / target)
    parts = path.parts
    if len(parts) == 2 and parts[-1] != "configs":
        patterns.append(boards_root / "*" / parts[0] / "configs" / parts[1])

    candidates = sorted(
        {
            match.resolve()
            for pattern in patterns
            for match in root.glob(str(pattern))
            if match.is_dir()
        }
    )
    if not candidates:
        raise ValueError(
            f"board target not found: {target} " f"(looked under {BOARD_ROOT}/)"
        )
    if len(candidates) > 1:
        listing = "\n".join(f"  {path.relative_to(root)}" for path in candidates)
        raise ValueError(f"ambiguous board target {target!r}, matches:\n{listing}")
    return candidates[0]


def build_dir(root: Path, board_dir: Path) -> Path:
    path = board_dir
    config_name = path.name
    board_name = path.parents[1].name

    return root / "cmake_out" / f"{board_name}_{config_name}"


def cmake_board_config(root: Path, board_dir: Path) -> str:
    try:
        return f"../{board_dir.relative_to(root).as_posix()}"
    except ValueError:
        return str(board_dir)


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


def cmake_configure(root: Path, out_dir: Path, board: Path, use_lib: str) -> int:
    lib_components = ";".join(
        component.strip() for component in use_lib.split(",") if component.strip()
    )
    cmd = [
        "cmake",
        "-B",
        str(out_dir),
        "-S",
        str(root / "nuttx"),
        f"-DBOARD_CONFIG={cmake_board_config(root, board)}",
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
                raise ValueError(f"whole image MFG partition is not erased: {image}")
            remaining -= len(chunk)


def flash_tool_name(system: Optional[str] = None, machine: Optional[str] = None) -> str:
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


def stage_flash_tool(root: Path, stage: Path) -> Path:
    """Stage the FlashCube binary and chip assets into the given directory."""
    flash_tool_dir = root / "vendor" / "bouffalolab" / "tools" / "bouffalo_flash_cube"
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
        flash_tool_dir / "chips" / "bl616cl" / "efuse_bootheader" / "flash_para.bin",
    )
    for path in required_files:
        if not path.is_file():
            raise FileNotFoundError(
                f"required flash runtime asset does not exist: {path}"
            )

    staged_tool = stage / tool_name
    shutil.copy2(flash_tool, staged_tool)
    shutil.copytree(flash_tool_dir / "chips", stage / "chips")
    staged_tool.chmod(staged_tool.stat().st_mode | 0o100)
    return staged_tool


def flash_bin_image(
    root: Path, image: Path, addr: str, port: str, baudrate: int
) -> int:
    """Burn a single binary to a flash address (default 0x0)."""
    image = image.resolve()
    if not image.is_file():
        raise FileNotFoundError(f"image does not exist: {image}")
    if image.stat().st_size == FLASH_SIZE:
        # A 4 MiB file is a whole image: keep the layout checks.
        validate_whole_image(image)

    with tempfile.TemporaryDirectory(prefix="bl616cl-flash.") as temp_dir:
        stage = Path(temp_dir)
        staged_tool = stage_flash_tool(root, stage)

        cmd = [
            str(staged_tool),
            "--interface=uart",
            "--chipname=bl616cl",
            f"--port={port}",
            f"--baudrate={baudrate}",
            f"--firmware={image}",
            f"--addr={addr}",
            "--reset",
        ]
        return subprocess.run(
            cmd, cwd=root, env=build_env(root), check=False
        ).returncode


def flash_config_image(root: Path, config: Path, port: str, baudrate: int) -> int:
    """Flash per-partition firmware described by a FlashCube config ini."""
    config = config.resolve()
    if not config.is_file():
        raise FileNotFoundError(f"flash config does not exist: {config}")

    with tempfile.TemporaryDirectory(prefix="bl616cl-flash.") as temp_dir:
        stage = Path(temp_dir)
        staged_tool = stage_flash_tool(root, stage)

        cmd = [
            str(staged_tool),
            "--interface=uart",
            "--chipname=bl616cl",
            f"--config={config}",
            f"--port={port}",
            f"--baudrate={baudrate}",
            "--reset",
        ]
        return subprocess.run(
            cmd, cwd=root, env=build_env(root), check=False
        ).returncode


def select_flash_config(root: Path) -> Path:
    """Auto-discover the single flash_prog_cfg.ini under cmake_out."""
    candidates = sorted(
        path
        for path in (root / "cmake_out").glob("*/flash_prog_cfg.ini")
        if path.is_file()
    )
    if not candidates:
        raise FileNotFoundError(
            f"no flash config found under {root / 'cmake_out'}; "
            "build first or pass a board / --config / --image"
        )
    if len(candidates) > 1:
        listing = "\n".join(f"  {path}" for path in candidates)
        raise ValueError(
            "multiple flash configs found; specify a board:\n" f"{listing}"
        )

    return candidates[0]


def board_candidates(root: Path) -> list:
    """Short board targets (prefix omitted) for shell completion."""
    boards = root / BOARD_ROOT
    return sorted(
        f"{d.parents[2].name}/{d.parents[1].name}/configs/{d.name}"
        for d in boards.glob("*/*/configs/*")
        if (d / "defconfig").is_file()
    )


def serial_ports() -> list:
    ports = []
    for pattern in ("ttyUSB*", "ttyACM*"):
        ports.extend(str(path) for path in Path("/dev").glob(pattern))
    return sorted(ports)


def complete_candidates(root: Path, words: Sequence[str]) -> list:
    """Completion candidates for the words typed so far (last word is partial)."""
    commands = [command for command in COMMANDS if command != "__complete"]
    if not words:
        return [*commands, "--help", "-h"]

    first = words[0]
    if first not in COMMANDS:
        # The first word is still being typed (partial command prefix).
        if len(words) == 1:
            return [*commands, "--help", "-h"]
        return []
    if len(words) == 1:
        return [*commands, "--help", "-h"]

    opts = list(COMMAND_OPTIONS[first])
    prev = words[-2]
    if prev == "--addr":
        return ["0x0"]
    if prev == "--config":
        return sorted(str(path) for path in root.glob("cmake_out/*/flash_prog_cfg.ini"))
    if prev == "--board":
        return board_candidates(root)
    if prev in ("-j", "--jobs"):
        return [default_jobs()]
    if prev == "--port":
        return serial_ports()
    if prev in ("--baudrate", "--use-lib", "--image"):
        return []
    if first == "completion":
        return ["bash", "zsh", "fish"]
    if first == "flash":
        if words[-1].startswith("-") or len(words) >= 3:
            return opts
        return [*board_candidates(root), *opts]
    if first in ("build", "clean", "menuconfig"):
        if words[-1].startswith("-") or len(words) >= 3:
            return opts
        return [*board_candidates(root), *opts]
    return []


def complete_main(root: Path, words: Sequence[str]) -> int:
    """Internal: print completion candidates, one per line."""
    candidates = complete_candidates(root, words)
    prefix = words[-1] if words else ""
    for candidate in candidates:
        if candidate.startswith(prefix):
            print(candidate)
    return 0


def completion_script(shell: str) -> str:
    script = Path(__file__).resolve()
    if shell == "bash":
        return f"""# bash completion for bl_build.py — install with:
#   bl_build.py completion bash > ~/.local/share/bash-completion/completions/bl_build.py
_bl_build_complete() {{
    local IFS=$'\\n'
    COMPREPLY=( $({script} __complete "${{COMP_WORDS[@]:1}}" 2>/dev/null) )
}}
complete -F _bl_build_complete bl_build.py
"""
    if shell == "zsh":
        return f"""#compdef bl_build.py
# zsh completion for bl_build.py — install with:
#   bl_build.py completion zsh > ${{fpath[1]}}/_bl_build
_bl_build() {{
    local -a candidates
    candidates=("${{(@f)$({script} __complete "${{words[@]:1}}" 2>/dev/null)}}")
    _describe 'bl_build' candidates
}}
compdef _bl_build bl_build.py
"""
    return f"""# fish completion for bl_build.py — install with:
#   bl_build.py completion fish > ~/.config/fish/completions/bl_build.py.fish
complete -c bl_build.py -f -a '({script} __complete (commandline -opc) 2>/dev/null)'
"""


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="bl_build.py",
        description="Build, configure and flash BouffaloLab boards on openvela "
        "(CMake + Ninja).",
        epilog=(
            "examples:\n"
            "  bl_build.py build bl616cl/ai-m64l-32s-kit/configs/nsh -j8\n"
            "  bl_build.py menuconfig ai-m64l-32s-kit/nsh\n"
            "  bl_build.py flash --port /dev/ttyUSB0\n"
            "  bl_build.py completion bash   # print a shell completion script\n"
            "\n"
            "board targets are matched under vendor/bouffalolab/boards/; the "
            "prefix, the chip layer and even 'configs/' may be omitted when "
            "the target stays unambiguous.\n"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = parser.add_subparsers(dest="command", metavar="<command>")

    p_build = sub.add_parser("build", help="configure and build a board")
    p_build.add_argument(
        "board", help="board target, e.g. bl616cl/ai-m64l-32s-kit/configs/nsh"
    )
    p_build.add_argument(
        "-j",
        "--jobs",
        default=default_jobs(),
        help=f"parallel jobs (default: {default_jobs()})",
    )
    p_build.add_argument(
        "--use-lib",
        default="",
        help="comma-separated components linked from prebuilt libs",
    )

    p_clean = sub.add_parser("clean", help="remove the board's build output directory")
    p_clean.add_argument(
        "board", help="board target, e.g. bl616cl/ai-m64l-32s-kit/configs/nsh"
    )

    p_menuconfig = sub.add_parser(
        "menuconfig", help="configure the board and open the Kconfig menu"
    )
    p_menuconfig.add_argument(
        "board", help="board target, e.g. bl616cl/ai-m64l-32s-kit/configs/nsh"
    )

    p_flash = sub.add_parser("flash", help="flash firmware over UART (never builds)")
    source = p_flash.add_mutually_exclusive_group()
    source.add_argument(
        "board",
        nargs="?",
        help="board target; flashes its flash_prog_cfg.ini " "(same as --board)",
    )
    source.add_argument(
        "--board", dest="board_opt", help="board target to flash by config"
    )
    source.add_argument(
        "--config",
        type=Path,
        help="FlashCube config ini describing per-partition firmware",
    )
    source.add_argument(
        "--image",
        type=Path,
        help="single binary to burn at --addr (whole images checked " "when 4 MiB)",
    )
    p_flash.add_argument(
        "--addr",
        default=None,
        help="flash address for --image (default: 0x0)",
    )
    p_flash.add_argument("--port", required=True, help="UART port")
    p_flash.add_argument(
        "--baudrate", type=int, default=2_000_000, help="UART baudrate"
    )

    p_completion = sub.add_parser(
        "completion", help="print a shell completion script (bash/zsh/fish)"
    )
    p_completion.add_argument(
        "shell",
        nargs="?",
        choices=("bash", "zsh", "fish"),
        default="bash",
        help="target shell (default: bash)",
    )

    return parser


def highlight(text: str) -> str:
    """Highlight a path when stderr is a terminal."""
    if sys.stderr.isatty():
        return f"\033[1;36m{text}\033[0m"
    return text


def info(message: str) -> None:
    """Print an informational line to stderr."""
    print(f"bl_build: {message}", file=sys.stderr)


def resolve_or_none(root: Path, target: str) -> Optional[Path]:
    """Resolve a board target, printing a friendly error on failure."""
    try:
        return resolve_board(root, target)
    except ValueError as error:
        print(f"bl_build: {error}", file=sys.stderr)
        return None


def main(argv: Optional[Sequence[str]] = None) -> int:
    root = find_root(Path(__file__).resolve())
    words = list(sys.argv[1:] if argv is None else argv)

    if not words:
        make_parser().print_help()
        return 0
    if words[0] == "__complete":
        return complete_main(root, words[1:])
    if words[0] in ("-h", "--help"):
        make_parser().print_help()
        return 0

    parser = make_parser()
    args = parser.parse_args(words)

    if args.command == "completion":
        sys.stdout.write(completion_script(args.shell))
        return 0

    if args.command == "flash":
        try:
            if args.addr is not None and args.image is None:
                raise ValueError("--addr requires --image")
            if args.image is not None:
                addr = args.addr or "0x0"
                info(f"flashing {highlight(str(args.image))} to {highlight(addr)}")
                return flash_bin_image(
                    root,
                    args.image,
                    addr,
                    args.port,
                    args.baudrate,
                )
            if args.config is not None:
                info(f"flashing via config: {highlight(str(args.config))}")
                return flash_config_image(root, args.config, args.port, args.baudrate)
            board_target = args.board or args.board_opt
            if board_target is None:
                config = select_flash_config(root)
                info(
                    "auto-detected flash config: "
                    f"{highlight(config.relative_to(root))}"
                )
            else:
                board = resolve_board(root, board_target)
                config = build_dir(root, board) / "flash_prog_cfg.ini"
                info(
                    f"flashing board {highlight(board_target)} via config: "
                    f"{highlight(config.relative_to(root))}"
                )
            return flash_config_image(root, config, args.port, args.baudrate)
        except (OSError, RuntimeError, ValueError) as error:
            print(f"bl_build: {error}", file=sys.stderr)
            return 1

    board = resolve_or_none(root, args.board)
    if board is None:
        return 1
    out_dir = build_dir(root, board)
    board_rel = highlight(board.relative_to(root))

    if args.command == "clean":
        info(f"cleaning output: {highlight(out_dir.relative_to(root))}")
        if out_dir.exists():
            shutil.rmtree(out_dir)
        return 0

    info(f"configuring board {board_rel}")
    ret = cmake_configure(root, out_dir, board, getattr(args, "use_lib", ""))
    if ret != 0:
        return ret

    if args.command == "menuconfig":
        return cmake_menuconfig(root, out_dir)

    return cmake_build(root, out_dir, args.jobs)


if __name__ == "__main__":
    raise SystemExit(main())
