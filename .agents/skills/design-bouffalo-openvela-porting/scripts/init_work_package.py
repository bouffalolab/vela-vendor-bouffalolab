#!/usr/bin/env python3
"""Initialize a Bouffalo OpenVela porting knowledge package."""

from __future__ import annotations

import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import shutil
import tempfile


SKILL_DIR = Path(__file__).resolve().parent.parent
TEMPLATE_DIR = SKILL_DIR / "assets" / "templates"
TASK_NAME_RE = re.compile(r"^[a-z0-9](?:[a-z0-9-]{0,62}[a-z0-9])?$")
TEMPLATES = {
    "BASELINE.md": "BASELINE.md",
    "STATUS.md": "STATUS.md",
    "JOURNAL.md": "JOURNAL.md",
    "DECISIONS.md": "DECISIONS.md",
    "DESIGN.md": "DESIGN.md",
    "MAP.md": "MAP.md",
    "evidence/INDEX.md": "evidence/INDEX.md",
}


def bullet_list(values: list[str], fallback: str) -> str:
    items = [value.strip() for value in values if value.strip()]
    if not items:
        items = [fallback]
    return "\n".join(f"- {item}" for item in items)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-root", required=True, type=Path)
    parser.add_argument("--task-name", required=True)
    parser.add_argument("--title", required=True)
    parser.add_argument("--goal", required=True)
    parser.add_argument("--scope", action="append", required=True)
    parser.add_argument("--boundary", action="append", required=True)
    parser.add_argument("--deliverable", action="append", required=True)
    parser.add_argument("--acceptance", action="append", required=True)
    parser.add_argument(
        "--source-mode",
        choices=("declared-present", "declared-absent", "unknown"),
        default="unknown",
    )
    return parser.parse_args()


def render_template(path: Path, replacements: dict[str, str]) -> str:
    content = path.read_text(encoding="utf-8")
    for key, value in replacements.items():
        content = content.replace("{{" + key + "}}", value)
    unresolved = sorted(set(re.findall(r"\{\{[A-Z0-9_]+\}\}", content)))
    if unresolved:
        raise ValueError(f"unresolved template fields in {path.name}: {unresolved}")
    return content


def main() -> int:
    args = parse_args()
    task_name = args.task_name.strip()
    if not TASK_NAME_RE.fullmatch(task_name):
        raise SystemExit(
            "--task-name must be 1-64 lowercase letters, digits, or hyphens"
        )

    work_root = args.work_root.expanduser().resolve()
    if not work_root.is_dir():
        raise SystemExit(f"work root is not an existing directory: {work_root}")

    title = args.title.strip()
    goal = args.goal.strip()
    if not title or not goal:
        raise SystemExit("--title and --goal must not be empty")

    container = work_root / ".porting"
    package_dir = container / task_name
    if package_dir.exists():
        raise SystemExit(f"refusing to overwrite existing package: {package_dir}")

    now = datetime.now().astimezone().isoformat(timespec="minutes")
    source_gate = "pending"
    if args.source_mode == "declared-present":
        initial_gap = "用户已声明有源；对应实现身份及有效行为尚未完成核实。"
        next_action = "锁定观察基线并核实源实现的构建、调用链和历史。"
    elif args.source_mode == "declared-absent":
        initial_gap = "用户已声明无源；该声明或受控检索证据尚未完成核实。"
        next_action = "核实无源依据，再收集 OpenVela 公共契约与芯片约束。"
    else:
        initial_gap = "是否存在与任务对应的有效 Bouffalo SDK 源实现尚未确认。"
        next_action = "锁定观察基线并确认是否存在有效源实现。"

    replacements = {
        "TITLE": title,
        "TASK_NAME": task_name,
        "CREATED_AT": now,
        "WORK_ROOT": str(work_root),
        "GOAL": goal,
        "SCOPE_ITEMS": bullet_list(args.scope, "未声明"),
        "BOUNDARY_ITEMS": bullet_list(args.boundary, "未声明"),
        "DELIVERABLE_ITEMS": bullet_list(args.deliverable, "未声明"),
        "ACCEPTANCE_ITEMS": bullet_list(args.acceptance, "未声明"),
        "SOURCE_MODE": args.source_mode,
        "SOURCE_GATE": source_gate,
        "INITIAL_GAP": initial_gap,
        "NEXT_ACTION": next_action,
    }
    rendered = {
        relative: render_template(TEMPLATE_DIR / template, replacements)
        for relative, template in TEMPLATES.items()
    }

    container.mkdir(exist_ok=True)
    temp_dir = Path(tempfile.mkdtemp(prefix=f".{task_name}.", dir=container))
    try:
        for relative, content in rendered.items():
            output = temp_dir / relative
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(content, encoding="utf-8")
        temp_dir.replace(package_dir)
    except Exception:
        shutil.rmtree(temp_dir, ignore_errors=True)
        raise

    print(
        json.dumps(
            {"package": str(package_dir), "status": "initialized"},
            ensure_ascii=False,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
