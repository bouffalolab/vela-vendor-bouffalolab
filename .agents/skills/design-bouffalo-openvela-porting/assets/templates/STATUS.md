# {{TITLE}} - 知识状态

- Knowledge state: gathering
- Map state: not-ready
- Blocking gaps: yes
- Updated: {{CREATED_AT}}

## 门禁状态

| Gate | Name | State | Evidence or gap |
|---|---|---|---|
| G0 | 输入契约 | passed | 初始目标、范围、边界、交付物和验收对象已记录 |
| G1 | 基线 | pending | 观察、历史和拟交付基线尚未闭合 |
| G2 | 源设计 | {{SOURCE_GATE}} | 初始 source mode: {{SOURCE_MODE}}；需由 agent 基于证据确认 |
| G3 | OpenVela 目标契约 | pending | 尚未完成目标规范和调用链研究 |
| G4 | 设计闭包 | pending | 尚未形成职责矩阵和约束图 |
| G5 | 验证设计 | pending | 尚未形成结论到证据的映射 |
| G6 | 冻结复审 | pending | 前置门禁未完成 |

## 阻断知识缺口

- {{INITIAL_GAP}}
- 观察、历史和拟交付基线尚未分开确认。

## 下一知识动作

{{NEXT_ACTION}}

## 当前证据入口

- `evidence/INDEX.md`
