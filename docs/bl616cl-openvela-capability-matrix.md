# BL616CL OpenVela 能力矩阵

本文是 BL616CL OpenVela 能力状态的唯一结论入口。实施记录、测试原始日志和
临时探测不在本文复制；每个能力完成后回写“状态”和“验证”列。

最后核对：2026-08-28

- `vendor/bouffalolab`: `7e42593764163555ca34902472b2095b248ee765`
- `nuttx`: `e987a81c32cab008d1a8521669e5488d00271322`
- 板卡：Ai-M64L-32S-Kit
- 配置：`bl616cl/ai-m64l-32s-kit/configs/nsh`

## 判定口径

| 状态 | 含义 |
|---|---|
| 当前已用 | 已进入当前生成配置、archive 和运行路径 |
| 可直接开启 | BL616CL 无需新增 lower-half；仍需独立构建和实板验证 |
| 需要适配 | 硬件和 OpenVela 均有依据，但缺少 adapter、pinmux、资源或注册路径 |
| 延后 | 有依据，但前置多、验证设备缺失或当前收益不足 |
| 不支持 | 当前 BL616CL 硬件、工具链或移植模型没有支持依据 |

“可直接开启”不等于默认开启。高开销诊断项应放入独立配置；每个实施项均需
Kconfig 控制，关闭时不进入目标 archive 或运行路径。

## 当前基线

| 子系统 | 当前能力 | 证据入口 |
|---|---|---|
| RISC-V | RV32IMAF+C、FPU、软件错位访问、stack dump、backtrace、CPU/TCB info | `chips/bl616cl/Kconfig`；`nuttx/arch/risc-v/src/common/` |
| 时钟 | 1 kHz OS tick；MTimer alarm lower-half；通用 `up_perf_*` 纳秒接口 | `bl616cl_timerisr.c`；`nuttx/drivers/timers/arch_alarm.c` |
| 内存 | 内部 SRAM 单 heap、默认 allocator、procfs meminfo | `bl616cl_allocateheap.c`；`nuttx/mm/` |
| 启动 | chip early init、board late init、ROMFS/NSH | `chips/bl616cl/bl616cl_start.c`；`boards/bl616cl/common/` |
| 外设 | UART0、GPIO、timer0、TIMER1 oneshot、watchdog | `chips/bl616cl/`；`cmake/bl616cl_lhal.cmake` |
| 诊断 | assertions、stack dump、dumpstack、procfs | `configs/nsh/defconfig` |

当前 `nsh` 已选择 `CONFIG_SCHED_CPULOAD_SYSCLK=y`，并用独立的
`CONFIG_SYSTEM_CPULOAD=y` 编入验证负载命令。后者只用于测试，不是 CPU load
统计的产品依赖。启用前生成配置默认选择 `CONFIG_SCHED_CPULOAD_NONE=y`；
`ostest` defconfig 中遗留的旧 `CONFIG_SCHED_CPULOAD=y` 不再对应当前 Kconfig
符号。

## 架构与 CPU

| ID | 能力与目标选项 | 状态 | 裁剪和依赖 | 验证要求 |
|---|---|---|---|---|
| A01 | frame pointer：`FRAME_POINTER` | 已验证（ST002） | 通用编译选项；配合现有 `SCHED_BACKTRACE` | USB2 实测当前线程 4 层、阻塞线程 12 层符号回溯；ELF/镜像增量已记录 |
| A02 | 栈高水位：`STACK_COLORATION`、`STACKCHECK_MARGIN`、`SYSTEM_STACKMONITOR` | 已验证（ST003） | RISC-V 通用实现；BL616CL 在开中断前补齐 IRQ 栈染色 | USB2 实测 `ps` 高水位、monitor 启停及 GPIO/timer/oneshot/WDT 回归；关闭态裁剪成立 |
| A03 | 编译器 stack canary：`STACK_CANARIES` | 可直接开启，P1 诊断 | 工具链加入 `-fstack-protector-all`；独立诊断配置 | 正常回归；受控栈溢出必须进入 `__stack_chk_fail`/panic |
| A04 | 静态栈报告：`STACK_USAGE`、`STACK_USAGE_WARNING` | 可直接开启，P2 工具 | 仅构建期，不作为运行保护 | 生成 `.su`；阈值负测能使构建告警 |
| A05 | lazy FPU：`ARCH_LAZYFPU` | 可直接开启，P1 优化 | 当前已具备 FPU 和 RISC-V lazy 实现 | 双浮点任务高频切换加 IRQ；数值隔离正确；比较上下文开销 |
| A06 | CLIC threshold 上下文：`ARCH_RV_HAVE_CLIC` | 需要适配，P2 | 当前自定义 CLIC 仍用全局 `mstatus` 屏蔽；开启会改变 trap frame 和 `up_irq_save()` 语义 | 嵌套/优先级/上下文切换专项；不能只做编译验证 |
| A07 | NuttX range cache API：`ARCH_ICACHE`/`ARCH_DCACHE` 能力 | 需要适配，P1 | 当前仅有启动期全 cache clean/invalidate；DMA 前置 | cacheable/non-cacheable DMA buffer 一致性；XIP 和启动回归 |
| A08 | cycle/HPM perf events：`ARCH_HAVE_PERF_EVENTS`、`ARCH_PERF_EVENTS` | 需要适配，P2 | RISC-V 有参考 `riscv_perf_cycle.c`，当前未编入且未初始化频率 | 与 MTIME 对时；溢出、换算、开销；之后才评估 perf-tools |
| A09 | T-Head `xtheade`/`-mtune=e907` | 延后 | 当前 GCC 拒绝 `-mtune=e907`，汇编器要求扩展版本 | 先完成工具链版本、反汇编和 ABI 矩阵 |
| A10 | MMU、S-mode、SMP、Vector、DPFPU、shadow stack | 不支持 | 当前为单核 M-mode RV32IMAF+C，无对应硬件/移植 select | 无；硬件或工具链依据变化后重审 |

源码入口：`nuttx/Kconfig`、`nuttx/arch/Kconfig`、
`nuttx/arch/risc-v/Kconfig`、`nuttx/arch/risc-v/src/common/`、
`chips/bl616cl/bl616cl_cpu.c`、`chips/bl616cl/bl616cl_cache.c`。

### A01 实测结果（2026-08-28）

- 配置：`CONFIG_FRAME_POINTER=y`、`CONFIG_SCHED_BACKTRACE=y`、
  `CONFIG_SYSTEM_DUMPSTACK=y`。
- 构建：`python3 vendor/bouffalolab/bl_build.py build
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14`，构建目标 `1190/1190` 成功。
- 编译证据：`compile_commands.json` 和 `build.ninja` 均含
  `-fno-omit-frame-pointer`；关闭态基线含 `-fomit-frame-pointer`。
- 制品：`final_nuttx` 为 666,948 字节，`text/data/bss` 为
  `283,974/13,716/9,640`；`nuttx.bin` 为 303,792 字节，whole image
  为 4,194,304 字节。
- 与关闭态对照：`final_nuttx` 增加 13,456 字节，`text` 增加 16,336
  字节，`nuttx.bin` 增加 16,240 字节。
- 烧录：USB2（`/dev/ttyUSB2`，2 Mbps）分区烧录成功；boot、partition、
  app 三段 SHA 校验通过，FlashCube 报告 `All programming completed successfully`。
- 启动：`bl-module-reset` 使用 `standard-dtr-rts`，匹配 `NuttShell (NSH)`
  和 `nsh>`，结果 `status=ok`。
- 当前线程：执行 `dumpstack` 输出 `sched_dumpstack`、`dumpstack_main`、
  `nxtask_startup`、`nxtask_start` 四个符号帧。
- 阻塞线程：执行 `sleep 30 &`，`ps` 显示 PID 5 为 `Waiting/Signal`；
  `dumpstack 5` 输出 `up_switch_context`、`nxsig_clockwait`、
  `clock_nanosleep`、`sleep`、`cmd_sleep` 等 12 个符号帧。
- 反汇编：`dumpstack_main` 序言保存 `s0` 并执行 `addi s0,sp,16`，
  证明启用 frame pointer 链。
- 提交：本项实现提交见 `VELABL616-138` 回写；能力矩阵随该提交更新。

### A02 实测结果（2026-08-28）

- 配置：`CONFIG_STACK_COLORATION=y`、`CONFIG_STACKCHECK_MARGIN=16`、
  `CONFIG_SYSTEM_STACKMONITOR=y`；monitor 默认周期 2 秒、任务栈 2,048 字节，
  只编入命令，不在启动脚本中自启。
- 适配：BL616CL `up_irqinitialize()` 在 `up_irq_enable()` 前调用
  `riscv_color_intstack()`。该接口在关闭 `STACK_COLORATION` 或未配置有效 IRQ
  栈时展开为空，因此不新增 BL 私有开关。
- 构建：开启态 clean build `1199/1199` 成功；最终 ELF 含
  `riscv_color_intstack`、`up_check_tcbstack`、`up_check_intstack`、
  `nxsched_checkstackoverflow`、`stackmonitor_start_main` 和
  `stackmonitor_stop_main`。
- 制品：`final_nuttx` 为 676,976 字节，`text/data/bss` 为
  `290,014/13,908/9,736`，`nuttx.bin` 为 310,032 字节，whole image 为
  4,194,304 字节。
- 关闭态：clean build `1195/1195` 成功；上述 A02 配置、符号和 monitor 命令
  均不在最终制品中。相对关闭态，开启态 `final_nuttx` 增加 10,028 字节，
  `text` 增加 6,040 字节，`data+bss` 增加 288 字节，`nuttx.bin` 增加
  6,240 字节；monitor 启动后另占一个 2,048 字节任务栈。
- 烧录与启动：`/dev/ttyUSB2`、2 Mbps 分区烧录完成四段 SHA 校验；受控复位
  匹配 `NuttShell (NSH)` 和 `nsh>`，未发生 early assert。
- 栈高水位：启动后 `ps` 显示 idle `536/2016`、hpwork `376/1968`、
  nsh `1732/1976`；执行后台 `sleep 30` 后，nsh 高水位达到 `1896/1976`
  （95.9%，剩余 80 字节），后台 shell 初次采样为 `264/1984`。
- 周期监控：`stackmonitor_start` 创建 PID 6，连续周期表显示 monitor 自身栈
  从 `1128/1984` 增至 `1144/1984`；`stackmonitor_stop` 完成后等待 5 秒无
  后续周期输出。停止期间允许已在执行的最后一个周期完成。
- 中断与调度回归：GPIO push-pull 跟随、20 次 GPIO 上升沿 IRQ、5 轮
  100 ms timer、100 ms oneshot 和 WDT 非复位生命周期测试全部通过；串口未出现
  stack margin、assert 或 panic 报告。
- 风险：当前 nsh 栈只余 80 字节高水位空间。16 字节 margin 验证通过不代表
  该栈余量充足，后续增加 NSH 命令深度时需重新测量或提高 init task 栈。
- 提交：本项实现提交见 `VELABL616-139` 回写；能力矩阵随该提交更新。

## MM 与内存保护

| ID | 能力与目标选项 | 状态 | 裁剪和依赖 | 验证要求 |
|---|---|---|---|---|
| M01 | 分配归属：`MM_RECORD_PID`、`MM_RECORD_SEQNO` | 可直接开启，P0 | 默认 allocator 内建；有固定每块开销 | 多 PID 分配/释放后 memdump 归属和序号正确 |
| M02 | 分配回溯：`MM_RECORD_STACK`、`MM_RECORD_STACK_DEFAULT` | 可直接开启，P1 诊断 | 依赖 `LIBC_BACKTRACE_DEPTH>0`；先完成 A01 | 分配栈可符号化；释放后不残留；量化每块开销 |
| M03 | OOM/破坏诊断：`DEBUG_MM`、`MM_DUMP_ON_FAILURE`、`MM_FILL_ALLOCATIONS`、`MM_NODE_GUARDSIZE` | 可直接开启，P1 诊断 | 高日志、内存和性能开销；不进默认产品配置 | OOM、越界、UAF 定向负测；正常 ostest |
| M04 | KASAN generic heap：`MM_KASAN_GENERIC`、`MM_KASAN_INSTRUMENT_ALL` | 可直接尝试，P1 诊断 | GCC 参数探测通过；首轮关闭 `MM_KASAN_GLOBAL` | heap OOB/UAF 均报告；启动、heap shrink、镜像和性能量化 |
| M05 | UBSAN：`MM_UBSAN`、`MM_UBSAN_ALL` | 可直接尝试，P1 诊断 | GCC 参数探测通过；首轮不启用 trap | overflow/alignment 等负测；正常回归和开销量化 |
| M06 | TLSF、mempool、task heap | 延后 | 会改变碎片、时延或隔离语义，缺少目标指标 | allocation latency、碎片、峰值、压力回归后再决策 |
| M07 | PSRAM 第二 heap：`MM_REGIONS>1` | 需要适配，P2 | 需 PSRAM init、cache/TZC、linker region；当前 `riscv_addregion()` 为空 | 探测容量、跨 region 分配、DMA/cache、一致性和压力 |
| M08 | protected/kernel build 双 heap | 不支持当前基线 | 缺少完整 MPU/MMU、用户地址空间和 `up_allocate_kheap()` 适配 | 需另立移植目标，不能作为配置开关开启 |

源码入口：`nuttx/mm/Kconfig`、`nuttx/mm/kasan/`、
`nuttx/arch/risc-v/src/cmake/Toolchain.cmake`、
`chips/bl616cl/bl616cl_allocateheap.c`、`drivers/soc/bl616cl/std/src/bl616cl_psram.c`。

## 故障留证与可观测性

| ID | 能力与目标选项 | 状态 | 裁剪和依赖 | 验证要求 |
|---|---|---|---|---|
| D01 | CPU load：`SCHED_CPULOAD_SYSCLK` | 已验证（ST004） | `SYSTEM_CPULOAD` 只提供可裁剪测试负载；tick 同步采样只能作基础观测 | USB2 实测 idle、单个 50% 和两个 50% 聚合满载；procfs、`ps`、`top` 均随负载变化；关闭态裁剪成立 |
| D02 | IRQ monitor：`SCHED_IRQMONITOR` | 可直接开启，P0 | 依赖 procfs；当前 alarm driver 已提供 `up_perf_*`；关注每 IRQ 两次时间读取开销 | `/proc/irqs` count/rate/max time；读后清零；1 kHz tick 开销和延迟 |
| D03 | critical monitor：`SCHED_CRITMONITOR`、`SYSTEM_CRITMONITOR` | 可直接开启，P1 诊断 | 首轮阈值关闭，只观测；与 trace 分开验证 | `/proc/critmon`；受控长临界区；再设阈值验证告警 |
| D04 | Note RAM trace：instrumentation、`DRIVERS_NOTERAM`、`SYSTEM_TRACE` | 可直接开启，P1 诊断 | 按 switch/IRQ/heap 分批启用；不能记录 csection/spinlock 到 Note RAM | start/stop/dump；时间单调；已知事件顺序；ring overflow 语义 |
| D05 | syslog coredump：`COREDUMP`、`BOARD_COREDUMP_SYSLOG` | 可直接开启，P1 诊断 | RISC-V 已有 TCB info；首轮不开 full/compression，可选 base64 | assert 后完整首尾；匹配 ELF 解码；恢复 PC/SP/触发线程栈 |
| D06 | stack/cpu/resource monitor 工具 | 可直接开启，P1 | 分别依赖 coloration、cpuload、procfs；工具本身也消耗资源 | 启停、周期、输出和自身 CPU/stack 开销 |
| D07 | EXTCLK CPU load | 需要资源重构，P2 | TIMER1 当前由 `/dev/oneshot` 独占；必须用 choice 确定 owner | 与 SYSCLK 对照；异步采样；不能同时注册同一 lower-half |
| D08 | E907 hardware perf/perf-tools | 需要适配，P2 | 通用 `SCHED_PERF_EVENTS` 不等于硬件 PMU 已接入；依赖 A08 | cycle/event 正确性、溢出、用户工具和开销 |

Note RAM 不得与 `SCHED_INSTRUMENTATION_CSECTION` 或 spinlock hook 同时启用，
因为 ring buffer 自身进入 critical section，会形成 instrumentation 递归。

源码入口：`nuttx/sched/Kconfig`、`nuttx/sched/irq/`、
`nuttx/drivers/note/`、`nuttx/sched/misc/coredump.c`、`apps/system/trace/`。

### D01 实测结果（2026-08-28）

- 配置：`CONFIG_SCHED_CPULOAD_SYSCLK=y`、采样频率 100 Hz、time constant
  2 秒；`CONFIG_SYSTEM_CPULOAD=y` 只编入测试命令，不在启动脚本中自启。
- 通路：系统时钟每 10 个 1 kHz tick 采样一次；scheduler 提供统计，procfs
  提供 `/proc/cpuload` 和 `/proc/<pid>/loadavg`，NSH `ps`、`top` 显示 CPU
  列。BL616CL 无需新增 arch hook。
- 构建：关闭态 clean build `1199/1199`、开启态 clean build `1202/1202`
  均成功。关闭态不含 CPU load 统计和测试负载符号；开启态含
  `cpuload_init`、`nxsched_process_cpuload_ticks`、`clock_cpuload`、
  `cpuload_main` 和 `cmd_top`。
- 制品：开启态 `final_nuttx` 为 682,368 字节，`text/data/bss` 为
  `293,630/14,036/9,752`，`nuttx.bin` 为 313,776 字节。相对关闭态
  分别增加 5,392、3,616、128、16 和 3,744 字节。
- 烧录与启动：`nuttx.bin` SHA256 为
  `2d36216aff3446e56aac1ac6f44dda5425753e510b2b7f437303dc01b7b3f767`；
  `/dev/ttyUSB2` 分区烧录校验一致，2 Mbps 复位后匹配 `NuttShell (NSH)`
  和 `nsh>`。
- idle：复位后等待 8 秒，`/proc/cpuload` 为 0.0%，`ps` 中 IDLE 为
  100.0%，不存在 `cpuload` 任务。
- 单负载：执行 `nice -d 19 cpuload -p 50 &` 并等待 8 秒，系统负载为
  52.0%，该线程 `/proc/4/loadavg` 为 51.9%，`ps` 显示 IDLE 48.0%、
  `cpuload` 51.5%。
- 聚合满载：再启动一个相同的 50% 负载并等待 8 秒，系统负载为 100.0%，
  两个线程分别为 51.1% 和 49.3%，IDLE 为 0.0%；`top` 同时显示两个
  `cpuload` 线程约各占一半 CPU。
- 清理限制：当前未启用 `CONFIG_SIG_DEFAULT`，`kill` 和 `kill -9` 不会按
  默认动作终止测试任务；且 `cpuload` 编译优先级为 253，`nice` 不会改变
  `ps` 中的实际优先级。因此不运行可能饿死 NSH 的单个 100% 负载，使用两个
  50% 任务形成可观测的聚合满载，并通过受控模块复位完成清理。
- 恢复与回归：复位并等待 8 秒后负载恢复 0.0%、IDLE 100.0%。随后 GPIO
  edge 64 轮恢复、5 轮 100 ms timer（最大误差 280 us，0.280%）、timer
  异常生命周期、100 ms oneshot 和 WDT 非复位生命周期全部通过；最终
  `/proc/cpuload` 仍为 0.0%。
- 适用边界：SYSCLK 与系统 tick 同源，结果可用于趋势和基础占用观测，不能
  作为同步于 tick 的特殊线程或高精度性能计量依据。

## 启动、调度与低功耗

| ID | 能力与目标选项 | 状态 | 裁剪和依赖 | 验证要求 |
|---|---|---|---|---|
| O01 | tickless：`SCHED_TICKLESS` | 可直接尝试，P2 优化 | BL616CL 已提供 alarm oneshot；不能使用 SYSCLK CPU load | 定时精度、sleep/wakeup、timer/WDT/GPIO 回归和功耗对照 |
| O02 | NuttX PM framework：`PM`、governor、`PM_PROCFS/STAT` | 需要适配，P2 | RISC-V 启用 PM 后要求 `riscv_pminitialize()`；BL616CL 尚未实现 | framework-only 启动、状态统计、设备回调，不先进入 HBN/PDS |
| O03 | PDS/HBN idle、唤醒和低功耗 tickless | 延后 | std 有 PDS/HBN/PM 基础，但缺 board policy、唤醒源和设备 suspend/resume | 功耗仪、RTC/GPIO 唤醒、UART/clock/cache 恢复、多轮稳定性 |
| O04 | priority inheritance、RR 等调度策略 | 可直接开启但不纳入首批 | 与硬件无关，需明确产品调度需求后选择 | 优先级反转、同优先级公平性和实时延迟专项 |

源码入口：`nuttx/sched/init/`、`nuttx/drivers/power/pm/`、
`nuttx/arch/risc-v/src/common/riscv_initialize.c`、
`drivers/soc/bl616cl/std/src/bl616cl_pds.c`、`bl616cl_hbn.c`、`bl616cl_pm.c`。

## 现役外设的未覆盖模式

| ID | 能力 | 状态 | 裁剪和资源边界 | 验证要求 |
|---|---|---|---|---|
| E01 | WDT interrupt/capture、automonitor | 需要适配，P1 | LHAL 支持 interrupt mode；新增 `BL616CL_WDT_CAPTURE`，仅开启时编译 ISR/capture | capture 回调、恢复 reset mode、automonitor、真实复位回归 |
| E02 | TIMER1 普通 timer | 需要适配，P1 | 与 oneshot/EXTCLK 用 Kconfig choice 互斥；独立 `/dev/timer1` | owner 冲突负测、动态 timeout、回调停止和重启 |
| E03 | TIMER0 input capture | 需要适配，P2 | device table 有 capture IRQ；对接 NuttX capture lower-half并配置输入 pin | 频率、占空比、边沿、溢出和无信号超时 |
| E04 | GPIO multipin batch | 需要适配，P2 | 当前 ioexpander multipin ops 为 `NULL`；仅 `IOEXPANDER_MULTIPIN` 时编译 | 跨 pin 原子性边界、输入/输出混合、与 IRQ 并发 |
| E05 | GPIO debounce/wakeup | 延后 | 需要区分硬件能力、软件 worker 和 PDS/HBN 唤醒域 | 抖动波形、丢边沿、睡眠唤醒和功耗 |
| E06 | oneshot 作为精确 CPU load 时钟 | 需要资源重构，P2 | 同 D07；设备节点和 scheduler 不能共享单实例 lower-half | 长时采样、取消/重启、应用访问互斥 |

## LHAL/std 已有但 OpenVela 未覆盖的非无线外设

下表的“硬件依据”来自 BL616CL device table、LHAL 源码和同系列历史实现；它只说明
值得适配，不代替模块 pin 可达性、外部器件或实板结果。

| ID | 外设/能力 | 状态 | 建议裁剪边界 | 主要前置与验证 |
|---|---|---|---|---|
| P01 | UART1/UART2 | 需要适配，P1 | `BL616CL_UART1/2`、独立 pin/baud/buffer | 扩展 serial 实例和 IRQ 名称；回环、并发、console 隔离 |
| P02 | TRNG `/dev/random` | 需要适配，P1 | `BL616CL_TRNG` | 编入 `bflb_sec_trng.c`、对接 random lower-half；健康检查和统计，不用短样本宣称熵质量 |
| P03 | RTC/Alarm | 需要适配，P1 | `BL616CL_RTC`、`BL616CL_RTC_ALARM` | HBN RTC lower-half；走时、跨回绕、alarm、复位保持 |
| P04 | I2C0/I2C1 master | 需要适配，P1 | 每实例选项、SCL/SDA pin、频率 | clock/pinmux/IRQ 或 polling；EEPROM/传感器、NACK、timeout、bus recovery |
| P05 | SPI0/SPI1 master | 需要适配，P1 | 每实例选项、pin、mode、CS policy | controller lower-half和 board select/status；loopback、多 mode/width/frequency |
| P06 | PWM | 需要适配，P1 | controller/channel/pin 选项 | NuttX PWM lower-half；频率/占空比边界、停止电平、逻辑分析仪 |
| P07 | ADC | 需要适配，P2 | ADC、channel/pin、poll/DMA 分层 | analog lower-half；校准、量程、连续采样、溢出；外部基准电压 |
| P08 | DMA0 | 需要适配，P1 公共前置 | controller/channel allocation；调用者各自 Kconfig | IRQ 映射、cache API A07、mem2mem 和外设传输、取消/并发 |
| P09 | AES/SHA/GMAC | 需要适配，P2 | 算法分别裁剪，DMA 可独立 | 对接 OpenVela crypto；标准向量、分块/非对齐、并发和软件对照 |
| P10 | EFUSE/unique ID | 需要适配，P2 | read 与 irreversible program 分离；默认只读 | 明确 NuttX ABI；只读实测、越界/权限；烧写需单独授权和治具 |
| P11 | SPI flash MTD | 需要适配，P2 | `BL616CL_FLASH_MTD`、partition/fs 独立 | XIP 并发、擦写临界区、cache、边界、掉电恢复；不得覆盖 boot/app/MFG |
| P12 | I2S、AUADC、AUDAC | 延后 | controller/方向/DMA/channel 分层 | 依赖 DMA/cache、codec/clock/pin；采样率、欠载/溢出、长时音频 |
| P13 | SDH/SDIO | 延后 | host/bus-width/pin/card-detect | 模组未确认卡座与连线；需 DMA/cache、文件系统和热插拔回归 |
| P14 | 原生 USB device/host | 延后 | role/PHY/class 分层 | `/dev/ttyUSB2` 是 USB-UART；需确认 USB pin/PHY、枚举、重连和吞吐 |
| P15 | EMAC | 延后 | MAC/PHY/RMII pin/clock | device table 有 MAC，但当前板未确认 PHY；需 cache/DMA 和网络压力 |
| P16 | camera、MJPEG/MJDEC、DBI/display | 延后 | pipeline 各块独立选项 | 依赖传感器/面板、DMA/cache 和大内存；图像正确性与吞吐 |
| P17 | touch、PEC、wave output | 延后 | 外设和通道分别裁剪 | OpenVela 上层契约与板上使用场景未明确，先补硬件/接口证据 |

源码入口：`drivers/lhal/config/bl616cl/device_table.c`、`drivers/lhal/src/`、
`cmake/bl616cl_lhal.cmake`、`chips/bl616cl/include/irq.h`。当前 NuttX IRQ 头只
命名现役少数 IRQ；新增外设必须补齐所用 IRQ 名称，不能复制 raw number 到驱动。

## 实施顺序

| 批次 | 独立工作项 | 原因 |
|---|---|---|
| 1 | A01+A02 回溯与栈高水位 | 现有 backtrace 的基础修正，风险低且为后续诊断提供证据 |
| 2 | D01+D02 CPU load 与 IRQ monitor | 修复失效配置，先建立运行观测基线 |
| 3 | M01 轻量 heap 归属 | 低风险补齐内存归属证据 |
| 4 | A03 stack canary | 独立负测，避免和其他 panic 源混淆 |
| 5 | D05 syslog coredump | 为后续 KASAN/UBSAN/驱动异常提供离线证据 |
| 6 | D04 Note RAM trace | 独立诊断配置，按事件域逐批开启 |
| 7 | M04、M05 KASAN/UBSAN | 各自独立配置、负测和 commit |
| 8 | A05 lazy FPU | 在诊断基线稳定后做性能优化 |
| 9 | P02、P03、P04、P05、P06 | TRNG、RTC、I2C、SPI、PWM 逐项适配 |
| 10 | A07+P08，再做 P07/P09/P11 | cache/DMA 是 ADC、crypto、MTD 的公共前置 |

同一行合并的配置只表示一个不可分割的验收闭包；不同表项不得合并成一个
commit。每项完成时至少记录静态、构建、烧录/启动和功能运行四层；故障诊断项
还必须包含可控负测与正常回归。
