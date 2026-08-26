#!/usr/bin/env python3
"""Validate a Bouffalo OpenVela porting knowledge package."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re


REQUIRED_HEADINGS = {
    "BASELINE.md": (
        "## 目标",
        "## 范围",
        "## 边界",
        "## 交付物和验收对象",
        "## 基线组合",
    ),
    "STATUS.md": (
        "## 门禁状态",
        "## 阻断知识缺口",
        "## 下一知识动作",
    ),
    "JOURNAL.md": ("知识演进日志",),
    "DECISIONS.md": ("## 决策记录",),
    "DESIGN.md": (
        "## 职责矩阵",
        "## 严格阶段与约束图",
        "## OpenVela 执行契约",
        "## 跨边界产物契约",
        "## 验证设计",
    ),
    "MAP.md": ("## 工作项", "## 交接条件"),
    "evidence/INDEX.md": ("## 证据索引",),
}
ALLOWED_KNOWLEDGE_STATES = {"gathering", "blocked", "ready-for-handoff"}
ALLOWED_MAP_STATES = {"not-ready", "draft", "frozen"}
ALLOWED_DESIGN_STATES = {"not-ready", "draft", "frozen"}
ALLOWED_GATE_STATES = {"pending", "passed", "blocked", "not-applicable"}
ALLOWED_SOURCE_MODES = {
    "unknown",
    "declared-present",
    "declared-absent",
    "confirmed-present",
    "confirmed-absent",
}
REQUIRED_PASSED_GATES = {"G0", "G1", "G3", "G4", "G5", "G6"}


def metadata_value(content: str, field: str) -> str | None:
    match = re.search(rf"(?m)^- {re.escape(field)}: ([^\n]+)$", content)
    return match.group(1).strip() if match else None


def gate_states(content: str) -> dict[str, str]:
    states = {}
    for gate, state in re.findall(
        r"(?m)^\| (G[0-6]) \| [^|]+ \| ([a-z-]+) \|", content
    ):
        states[gate] = state
    return states


def validate(package: Path, require_frozen: bool) -> list[str]:
    errors: list[str] = []
    contents: dict[str, str] = {}
    for relative, headings in REQUIRED_HEADINGS.items():
        path = package / relative
        if not path.is_file():
            errors.append(f"missing required file: {relative}")
            continue
        content = path.read_text(encoding="utf-8")
        contents[relative] = content
        for heading in headings:
            if heading not in content:
                errors.append(f"{relative} missing heading: {heading}")
        if re.search(r"\{\{[A-Z0-9_]+\}\}|\b(?:TODO|TBD)\b", content):
            errors.append(f"{relative} contains unresolved template text")

    status = contents.get("STATUS.md")
    if status is None:
        return errors

    knowledge_state = metadata_value(status, "Knowledge state")
    map_state = metadata_value(status, "Map state")
    blocking_gaps = metadata_value(status, "Blocking gaps")
    baseline_source_mode = metadata_value(contents.get("BASELINE.md", ""), "Source mode")
    design_state = metadata_value(contents.get("DESIGN.md", ""), "State")
    design_source_mode = metadata_value(contents.get("DESIGN.md", ""), "Source mode")
    map_file_state = metadata_value(contents.get("MAP.md", ""), "State")
    frozen_baseline = metadata_value(contents.get("MAP.md", ""), "Frozen baseline")
    if knowledge_state not in ALLOWED_KNOWLEDGE_STATES:
        errors.append(f"invalid Knowledge state: {knowledge_state}")
    if map_state not in ALLOWED_MAP_STATES:
        errors.append(f"invalid Map state: {map_state}")
    if design_state not in ALLOWED_DESIGN_STATES:
        errors.append(f"invalid Design state: {design_state}")
    if map_file_state not in ALLOWED_MAP_STATES:
        errors.append(f"invalid MAP.md State: {map_file_state}")
    if map_state != map_file_state:
        errors.append("STATUS.md Map state and MAP.md State differ")
    if blocking_gaps not in {"yes", "no"}:
        errors.append(f"invalid Blocking gaps value: {blocking_gaps}")
    if baseline_source_mode not in ALLOWED_SOURCE_MODES:
        errors.append(f"invalid BASELINE.md Source mode: {baseline_source_mode}")
    if design_source_mode not in ALLOWED_SOURCE_MODES:
        errors.append(f"invalid DESIGN.md Source mode: {design_source_mode}")
    if baseline_source_mode != design_source_mode:
        errors.append("BASELINE.md and DESIGN.md Source mode differ")
    if knowledge_state == "blocked" and blocking_gaps != "yes":
        errors.append("blocked package must report blocking gaps")

    gates = gate_states(status)
    if set(gates) != {f"G{number}" for number in range(7)}:
        errors.append("STATUS.md must contain exactly G0-G6")
    for gate, state in gates.items():
        if state not in ALLOWED_GATE_STATES:
            errors.append(f"invalid gate state for {gate}: {state}")

    frozen_state = (
        knowledge_state == "ready-for-handoff"
        or map_state == "frozen"
        or design_state == "frozen"
    )
    if require_frozen or frozen_state:
        if knowledge_state != "ready-for-handoff":
            errors.append("frozen package must be ready-for-handoff")
        if map_state != "frozen":
            errors.append("frozen package must have Map state frozen")
        if design_state != "frozen":
            errors.append("frozen package must have Design state frozen")
        if frozen_baseline in {None, "not-frozen"}:
            errors.append("frozen package must identify its frozen baseline")
        if blocking_gaps != "no":
            errors.append("frozen package must not have blocking gaps")
        for gate in sorted(REQUIRED_PASSED_GATES):
            if gates.get(gate) != "passed":
                errors.append(f"frozen package requires {gate}=passed")
        if gates.get("G2") not in {"passed", "not-applicable"}:
            errors.append("frozen package requires G2=passed or not-applicable")
        if baseline_source_mode not in {"confirmed-present", "confirmed-absent"}:
            errors.append("frozen package requires a confirmed Source mode")
        for relative in ("BASELINE.md", "DESIGN.md", "MAP.md", "evidence/INDEX.md"):
            content = contents.get(relative, "")
            if "尚未" in content or "当前没有" in content:
                errors.append(f"frozen package contains unresolved content: {relative}")
        if "冻结" not in contents.get("JOURNAL.md", ""):
            errors.append("frozen package JOURNAL.md must record the freeze")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--require-frozen", action="store_true")
    args = parser.parse_args()
    package = args.package.expanduser().resolve()
    if not package.is_dir():
        errors = [f"package is not an existing directory: {package}"]
    else:
        errors = validate(package, args.require_frozen)
    print(
        json.dumps(
            {"package": str(package), "valid": not errors, "errors": errors},
            ensure_ascii=False,
        )
    )
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
