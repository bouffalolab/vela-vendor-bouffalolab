---
name: design-bouffalo-openvela-porting
description: 审查并设计 Bouffalo SDK 到 OpenVela 的持续移植、上游同步、优化或无源新功能任务；锁定源端与目标端基线，研究源设计及历史，映射 OpenVela 职责，生成本地 .porting 工程知识包、执行 Map 和风险驱动的验收要求，并停在实现之前。仅在用户显式调用 $design-bouffalo-openvela-porting 时使用，不得主动触发。
---

# Bouffalo OpenVela Porting Design

把外部给出的任务目标转成可审查、可执行、可验收的移植设计。只发现事实、
形成设计和冻结执行 Map；不要修改产品代码、构建、烧录或操作硬件。

## 开始前

1. 读取 [workflow.md](references/workflow.md) 和
   [knowledge-contract.md](references/knowledge-contract.md)。
2. 确认用户已经给出初始目标、已知范围、禁止或暂停边界、期望交付物和验收
   对象。缺少时一次只询问当前最高优先级的问题，不自行补造目标。
3. 要求任务上下文明确 `work_root` 和 `task-name`。不要从当前目录、仓库布局或
   本机存在的 `.tasks`、Wayfinder、Jira、Wiki 等系统推断 `work_root`。
4. 检查本次是否同时调用其他会写入工程知识的 Skill。若存储位置、文件语义或
   权威源契约冲突，在任何写入前报告双方要求并等待用户决策；不要静默双写。
5. 检查 `<work_root>/.porting/<task-name>/`：存在时先恢复并校验，不存在时才
   使用 `scripts/init_work_package.py` 初始化。不得覆盖同名工作包。

初始化示例：

```bash
python3 SKILL_DIR/scripts/init_work_package.py \
  --work-root /path/to/work-root \
  --task-name sync-bl616cl-sdk-2-3-31 \
  --title "同步 BL616CL SDK 2.3.31" \
  --goal "将目标版本涉及的有效行为同步到 OpenVela" \
  --scope "BL616CL NSH baseline" \
  --boundary "不新增 Wi-Fi 和 PM 能力" \
  --deliverable "经确认的设计和执行 Map" \
  --acceptance "每个声明能力都有对应证据要求"
```

`SKILL_DIR` 是本文件所在目录的绝对路径。初始化只创建知识包，不修改
`.gitignore`，也不把 `.porting` 加入 Git。

## 主流程

始终使用同一条主流程，不按芯片移植、上游同步、优化或新功能拆成多套方法。
是否存在 Bouffalo SDK 源实现只控制源研究步骤是否执行。

### 1. 固定输入和观察基线

- 将用户确认的目标、范围、边界、交付物和验收对象写入 `BASELINE.md`。
- 发现相关 workspace、repo/project、manifest、源码、配置、工具、预编译资产、
  芯片和板卡边界。
- 分开记录观察快照、历史参考基线和拟交付冻结基线。dirty 或 floating 状态可
  用于只读研究，但不得伪装成冻结基线。
- 把命令、时间、commit、dirty paths、版本和原始输出写入 `evidence/` 并登记
  `evidence/INDEX.md`。

### 2. 条件化研究源设计

- 先验证是否存在与任务对应的有效 Bouffalo SDK 源实现，不要因一次搜索无结果
  就判定“无源”。只有用户明确声明原生新功能无源，或锁定源基线后对相关模块、
  配置、历史和同义行为完成有范围的检索并能排除隐藏/改名实现，G2 才能标为
  `not-applicable`；否则保持 `source-mode: unknown` 并阻断依赖分支。
- 存在源实现时，研究真实 build/link/runtime 选择、调用链、引入提交、后续
  修复、分支差异、严格顺序、不变量、失败路径和外部消费者。
- 没有源实现时，在 `BASELINE.md` 和 `DESIGN.md` 明确标为不适用；改用已确认
  需求、当前 OpenVela 公共契约、芯片资料、已有 HAL 和同代实现。相似芯片只能
  提供候选设计，不能证明当前芯片行为。
- 只研究到能够解释“为何这样设计、哪些语义必须保留”；不要无边界遍历历史。

### 3. 建立 OpenVela 目标约束

- 读取目标分支的公共接口、真实调用者、构建接入、命名风格和代码规范。
- 识别调用条件、执行阶段、线程或中断 context、scheduler/IRQ/cache/clock/heap
  前置状态、阻塞限制、可重入性、并发、幂等、重试、恢复和错误传播。
- 发生模式冲突时，选择当前公共契约、真实调用路径和同代已验证实现共同支持的
  模式，记录另一模式及清理风险；不要折中拼接。

### 4. 形成职责和设计闭包

执行本阶段前读取 [design-model.md](references/design-model.md)。

- 把每项行为或资产拆入芯片硬件不变量、OpenVela 执行契约、跨边界产物契约或
  任务策略，不能笼统写成双方共同负责。
- 对范围内每项内容给出 `复用 / 适配 / 替换 / 省略 / 延期` 的唯一处置，记录
  理由、依赖、失败语义、重新进入条件、风险和验证入口。
- 对严格行为建立阶段与约束图，追踪顺序、依赖和不变量；不要要求目标文件、
  函数或目录与源 SDK 同名。
- 从源端有效行为和 OpenVela 目标职责两个方向检查闭包。设计冻结后发现矩阵外
  影响项时重新打开设计，不要直接塞入实现 Map。

### 5. 设计风险驱动的验证

- 从要证明的结论、失败模式、后果、不确定性、边界跨度、硬件依赖和历史复发
  推导证据层，不从现成命令或固定测试清单反推。
- 将 V0-V8 视为互不替代的证据类型。compile-only、ELF、NSH 启动或旧基线
  结果只能证明各自覆盖的结论。
- V5 默认使用烧录输入身份、目标设备、地址/分区、工具结果和受控复位证据；
  不把 flash readback 设为同类移植任务的默认门禁。只有用户明确要求，或任务
  目标本身需要证明 Flash 实际字节、擦除区或产测内容时，才设计 readback。
- 为每个 Map 条目写明精确基线、前置条件、步骤、通过/失败判据、run 有效性、
  原始证据位置、失败回退和必要的独立复审或重复要求。
- 缺少必需证据时输出待执行验证项；不要把未运行、无效运行或风险接受写成通过。

### 6. 通过门禁并冻结 Map

- 按 [workflow.md](references/workflow.md) 逐项检查 G0-G6。
- 会改变范围、职责、ABI、外部格式、关键时序、安全/失败策略或验收方法的未知
  项必须先转成 research、prototype 或专项验证项。门禁未通过时只输出当前事实、
  缺口和下一项调查，不生成伪完整 Map。
- 需要省略、延期、改变源行为、在多个有效架构间取舍或接受残留风险时，一次只
  向用户提出一个决策，并给出推荐答案。
- 门禁全部闭合后，将 `DESIGN.md` 更新为当前有效设计，将 `MAP.md` 标记为
  `frozen`，将 `STATUS.md` 标记为 `ready-for-handoff`，并在 `JOURNAL.md` 追加
  冻结记录。
- 固定核心文件和必查契约，但允许任务增加适合自身的章节、矩阵或图。契约确实
  不适用时保留检查项并写明排除依据；冻结包不得残留初始化文本或空白占位。
- 涉及严格时序、linker/镜像、ABI/预编译库、烧录/产测、安全边界，或具有高
  后果、高不确定性、跨多个边界特征时，G6 必须由未参与主设计的 reviewer 从
  工作包、源码和原始证据独立重建判断。没有 reviewer 时保持 `blocked`；低风险
  且公共契约唯一的局部任务可以用结构化的冷启动复审轮次完成 G6。
- 顶层 `Knowledge state` 只使用 `gathering / blocked / ready-for-handoff`。
  有独立工作可继续时保持 `gathering`；全部剩余工作均等待外部输入时才用
  `blocked`；G0-G6 闭合且 Design/Map 冻结后才用 `ready-for-handoff`。执行反馈
  推翻设计时回到 `gathering` 并重开受影响门禁，不发明模糊的中间完成状态。

## 禁止项

- 不为保持源 SDK 外形而增加同名文件、空 hook、默认关闭的空 Kconfig、无调用
  wrapper、平行 provider、未接入模板或 speculative abstraction。
- 不以文件数、函数数、SDK 同名率、编译通过或进入 NSH 代替职责完整性。
- 不吞掉失败，不隐藏省略、延期、兼容偏离、无效证据或尚未确认的假设。
- 不自动清理 dirty tree、reset 仓库、改写用户文件或删除原始证据。
- 不在本 Skill 内执行 `MAP.md`。用户要求继续实现时，明确交给外部任务流程，
  并以冻结工作包作为唯一设计输入。

## 执行反馈

- 外部任务系统跟踪执行进度、人员和排期；执行产生的构建、ELF、镜像和实机
  证据仍登记到同一 `.porting` 工作包。
- 符合冻结设计的新证据只追加 `evidence/INDEX.md` 和 `JOURNAL.md`，不改变设计。
- 发现基线漂移、职责遗漏、错误假设或 Map 外影响项时，停止依赖分支并重新显式
  调用本 Skill，重新打开受影响门禁。外部流程不得静默修改冻结的 `DESIGN.md`
  或 `MAP.md`。

## 完成与交接

运行结构校验：

```bash
python3 SKILL_DIR/scripts/validate_work_package.py \
  <work_root>/.porting/<task-name> --require-frozen
```

只有校验通过且所有必要人工决策已确认，才能声称设计完成。最终回复必须给出：

- 工作包路径和冻结的 source/target baseline。
- 已通过、阻塞和不适用的门禁。
- 执行 Map 入口、能力边界和残留风险。
- 明确声明本 Skill 未修改产品代码、未构建、未烧录、未做硬件验证。
