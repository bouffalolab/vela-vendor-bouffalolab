# BL616CL OpenVela 能力矩阵

本文是 BL616CL OpenVela 能力状态的唯一结论入口。实施记录、测试原始日志和
临时探测不在本文复制；每个能力完成后回写“状态”和“验证”列。

最后核对：2026-08-30

- `vendor/bouffalolab`: `6a2925a`（P03 RTC/Alarm 远端基线）
- `nuttx`: `27b42a91d71`（基于 `e987a81c32c`）
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
| 外设 | UART0、GPIO、timer0、TIMER1 oneshot、watchdog、TRNG `/dev/random` | `chips/bl616cl/`；`cmake/bl616cl_lhal.cmake` |
| 诊断 | assertions、stack/backtrace、CPU/IRQ/critical monitor、MM record、stack canary、coredump、Note RAM trace、generic KASAN、UBSAN runtime | `configs/nsh/defconfig`；`vendor/bouffalolab/docs/` |

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
| A03 | 编译器 stack canary：`STACK_CANARIES` | 已验证（ST009） | 工具链全局加入 `-fstack-protector-all`；负测 app 默认关闭 | USB2 实测精确一字节覆盖进入 `__stack_chk_fail`/panic，普通任务终止后 NSH 存活，冷启动及外设回归通过；关闭态对照和测试 app 裁剪成立 |
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

### A03 实测结果（2026-08-29）

- 正式配置只启用 `CONFIG_STACK_CANARIES=y`；受控负测命令
  `CONFIG_BL_OS_FEATURE_TESTS_STACK_CANARY` 默认关闭，不进入产品固件。
- 通路：RISC-V CMake 对 1,161/1,161 条 C 编译命令加入
  `-fstack-protector-all`；通用 libc 提供弱 guard/fail，BL616CL 不需要私有
  arch hook。guard 位于 flash，可供 section load 前的早期 C 函数读取。
- 反汇编门禁：32 B buffer 位于 `s0-52..s0-21`，canary 位于 `s0-20`；safe
  offset 31 合法写最后一字节，corrupt offset 32 只覆盖 canary 首字节，函数
  epilogue 比较失败后调用 `__stack_chk_fail`。
- 构建与裁剪：关闭态、测试开启态和最终产品开启态 clean build 均通过。最终
  产品 build 为 `1207/1207`，含 `__stack_chk_guard`/`__stack_chk_fail`，不含
  `stack_canary_test_main` 和测试 archive。
- 制品：关闭态 `final_nuttx` 为 692,816 B，`text/data/bss` 为
  `302,174/14,292/11,128`，`nuttx.bin` 为 322,576 B；开启态分别为
  738,048 B、`349,550/14,292/11,128` 和 369,568 B。增量为 ELF 45,232 B、
  text 47,376 B、bin 46,992 B，data/bss 不变。
- USB2 负测：safe 写 offset 31 后正常返回；corrupt 写 offset 32 后在
  `lib_stackchk.c:57` 触发 panic，回溯含 `run_canary_test` 和
  `stack_canary_test_main`，未打印 corrupt returned。
- 恢复语义：当前 flat builtin 普通任务由 `abort()` 终止，NSH 随后执行
  `echo alive` 成功；内核线程或 IRQ 中的失败会进入系统 panic，不能外推为同样
  可恢复。受控模块复位后 2 Mbps 启动再次匹配 NSH。
- 最终产品固件：`nuttx.bin` SHA256 为
  `3d458b8455e3deb579c8d8deea6ba7c3b0b00a5c995deef40a5970c1ffdfc61c`；USB2
  四段烧录校验一致，2 Mbps 启动匹配 NSH，`help` 不含测试命令。
- 冷启动回归：GPIO edge 3 个恢复周期通过；TIMER-001 10 ms 最大误差 284 us
  （2.840%），TIMER-002 divider 比例 2.001，TIMER-005 生命周期通过；100 ms
  oneshot 完成；WDT-002 在 3,006 ms 内喂狗 6 次且无复位，WDT-003 生命周期
  通过，最终 `echo final_alive` 正常返回。
- 能力边界：通用 guard 是固定自地址值，可检测非定向覆盖，不是高熵抗攻击
  canary。完整配置、命令、反汇编门禁、流程和判定标准见
  [BL616CL 编译器栈保护与受控负测](bl616cl-stack-canary.md)。

## MM 与内存保护

| ID | 能力与目标选项 | 状态 | 裁剪和依赖 | 验证要求 |
|---|---|---|---|---|
| M01 | 分配归属：`MM_RECORD_PID`、`MM_RECORD_SEQNO` | 已验证（ST007） | 默认 allocator 内建；RV32 每个已分配块增加 8 B，最小 chunk 从 16 B 增至 32 B | USB2 实测多线程归属、realloc 归属变化、sequence 窗口、释放清除和重复实例；关闭态裁剪成立 |
| M02 | 分配回溯：`MM_RECORD_STACK`、`MM_RECORD_STACK_DEFAULT` | 可直接开启，P1 诊断 | 依赖 `LIBC_BACKTRACE_DEPTH>0`；先完成 A01 | 分配栈可符号化；释放后不残留；量化每块开销 |
| M03 | OOM/破坏诊断：`DEBUG_MM`、`MM_DUMP_ON_FAILURE`、`MM_FILL_ALLOCATIONS`、`MM_NODE_GUARDSIZE` | 可直接开启，P1 诊断 | 高日志、内存和性能开销；不进默认产品配置 | OOM、越界、UAF 定向负测；正常 ostest |
| M04 | KASAN generic heap：`MM_KASAN_GENERIC`、`MM_KASAN_INSTRUMENT_ALL` | 已验证（ST012） | 全镜像插桩；正式关闭测试 app 和 `MM_KASAN_GLOBAL`；启动 early stop 处理 warm reset | USB2 实测 heap 左/右越界和 UAF 精确报告、合法访问、连续 warm reset、裁剪、开销和外设回归 |
| M05 | UBSAN：`MM_UBSAN`、目标局部插桩 | 已验证（ST013） | runtime 正式启用但无引用零链接开销；`MM_UBSAN_ALL` 因 RAM 和 handler 闭包不可用；测试 app 默认关闭 | USB2 实测 signed overflow、shift 越界、三次合法对照、裁剪、开销和外设回归 |
| M06 | TLSF、mempool、task heap | 延后 | 会改变碎片、时延或隔离语义，缺少目标指标 | allocation latency、碎片、峰值、压力回归后再决策 |
| M07 | PSRAM 第二 heap：`MM_REGIONS>1` | 需要适配，P2 | 需 PSRAM init、cache/TZC、linker region；当前 `riscv_addregion()` 为空 | 探测容量、跨 region 分配、DMA/cache、一致性和压力 |
| M08 | protected/kernel build 双 heap | 不支持当前基线 | 缺少完整 MPU/MMU、用户地址空间和 `up_allocate_kheap()` 适配 | 需另立移植目标，不能作为配置开关开启 |

源码入口：`nuttx/mm/Kconfig`、`nuttx/mm/kasan/`、
`nuttx/arch/risc-v/src/cmake/Toolchain.cmake`、
`chips/bl616cl/bl616cl_allocateheap.c`、`drivers/soc/bl616cl/std/src/bl616cl_psram.c`。

### M01 实测结果（2026-08-29）

- 正式配置：`CONFIG_MM_RECORD_PID=y`、`CONFIG_MM_RECORD_SEQNO=y`；测试命令
  `CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD` 默认关闭，不进入产品固件。
- 通路：BL616CL 仍只提供 heap 边界；通用 allocator 在 allocation node 中记录
  TID 和 sequence，procfs 提供 `/proc/<tid>/heap` 与 `/proc/memdump`，无需
  BL616CL 或 RISC-V 私有 hook。
- 构建与裁剪：关闭态和开启态 clean build 均通过。关闭态不含
  `g_mm_seqno`、PID heap procfs 操作和 M01 配置；正式开启态保留这些能力，
  但不含 `mm_record_test` 命令符号。
- 制品：关闭态 `final_nuttx` 为 688,528 B，`text/data/bss` 为
  `300,734/14,292/11,128`，`nuttx.bin` 为 321,136 B；开启态分别为
  692,816 B、`302,174/14,292/11,128` 和 322,576 B。增量为 ELF 4,288 B、
  text 1,440 B、bin 1,440 B，data/bss 不变。
- 堆开销：RV32 上 PID 与 sequence 合计使每个已分配块增加 8 B；allocator
  对齐使最小 chunk 从 16 B 增至 32 B，小对象不能只按 8 B 估算实际增量。
- USB2 运行：测试 controller/worker TID 为 4/5/6。controller 将 worker0
  的 64 B 块 realloc 为 2,000 B 后，sequence 窗口 `[85,99]` 只在 PID 4
  下出现一个 2,016 B 块，PID 5/6 为 0，证明 realloc 归属为执行线程。
- 释放与复用：free 后 PID 4 同一窗口为 0，worker procfs 节点随线程退出消失；
  后续两轮实例 controller TID 从 11 变为 15，均能创建和完整释放。
- 栈前置：全量 memdump 在 2,048 B init 栈上触发栈断言，因此独立提高
  `CONFIG_INIT_STACKSIZE` 和 `CONFIG_SYSTEM_NSH_STACKSIZE` 到 4,096 B。
  实测 `nsh_main` 可用 4,032 B、已用 2,328 B，高水位 57.7%。
- 回归：GPIO edge 3 个恢复周期、TIMER-001/002/005、WDT-002/003 均通过；
  TIMER-001 10 ms 最大误差 233 us（2.330%），TIMER-002 divider 比例 2.000，
  WDT-002 在 1,000 ms timeout 下每 500 ms 喂狗并持续 3,006 ms，无复位。
- 完整配置、命令、流程和判定标准见
  [BL616CL 堆分配归属与序号观测](bl616cl-mm-record.md)。

### M04 实测结果（2026-08-29）

- 正式配置启用 generic、instrument-all、2 个 region、读写检测和 fatal panic，
  关闭 null pointer、global 和 `TESTING_KASAN`；GCC 13.4.0 编译参数为
  `-fsanitize=kernel-address`、`asan-stack=0`、threshold 0 和
  `asan-globals=0`。
- BL616CL `__start` 在 gp/sp/mtvec 建立后、普通 C 和 `riscv_fpuconfig()` 前调用
  未插桩的 `kasan_stop()`；同一 USB2 fd 连续三次 warm reset 均匹配 NSH，未出现
  early report/assert。
- 测试态 clean build `1220/1220` 成功。合法 heap 访问前后两次无误报；heap
  左越界 `0x60fcdcff`、右越界 `0x60fcdd14` 和 UAF `0x60fcdd00` 均产生地址匹配的
  1 B write report，backtrace 命中对应测试函数和 `run_child`。预期 fault 未触发而
  正常返回会被判 FAIL。
- 最终产品 clean build `1218/1218` 成功，`help`、archive、符号和字符串均无
  `kasantest` 残留；`nuttx.bin` SHA256 为
  `81a56dc3580a4b535f0a98290d2a6251797e4ebeb624b0ae09bd1702b244ea37`。
- 正式产品相对关闭态：`final_nuttx` 增加 75,380 B，text 增加 75,126 B，
  `nuttx.bin` 增加 75,232 B。raw heap 从 274,192 B 降至 269,696 B，再扣除
  8,440 B descriptor/shadow，allocator region 为 261,256 B。
- 最终产品 USB2 回归通过 GPIO edge、TIMER-001/002/005、100 ms oneshot、
  WDT-002/003 和最终 NSH 存活；TIMER-001 100 ms 最大误差 806 us（0.806%），
  TIMER-002 周期比例 2.003，WDT-002 在 3,026 ms 内喂狗 6 次且无复位。
- 完整配置、命令、原始关键输出、裁剪门禁、开销和限制见
  [BL616CL Generic KASAN 配置与验证](bl616cl-kasan.md)。

### M05 实测结果（2026-08-29）

- GCC 13.4.0 默认 `undefined` 与当前 NuttX runtime handler 集不闭包；全镜像
  UBSAN 与现有 KASAN 基线叠加后 cacheable RAM 溢出 67,920 B。正式方案因此使用
  `MM_UBSAN=y`、`MM_UBSAN_ALL=n`，只给独立验收 app 添加
  `signed-integer-overflow,shift`。
- 测试固件 clean build `1221/1221` 成功，最终 ELF 定义
  `__ubsan_handle_add_overflow` 和 `__ubsan_handle_shift_out_of_bounds`，且没有未定义
  UBSAN 符号。
- USB2 recover 负测中，add overflow 准确报告 `ubsan_test.c:39:16`，32 位移位指数
  32 准确报告 `shift-out-of-bounds` 和 `ubsan_test.c:44:16`；两类各一条报告，均
  返回 NSH。负测前、中、后三次合法加法均为 42 且无误报。
- 最终产品 clean build `1219/1219` 成功；runtime handler 保留在 `libmm.a`，但测试
  archive、命令、字符串、插桩和最终 ELF handler 均被裁掉。正式产品与 UBSAN
  关闭态的 ELF、text/data/bss、bin 和 linker raw heap 尺寸完全相同。
- 正式产品 USB2 回归通过 GPIO edge、TIMER-001/002/005、100 ms oneshot、
  WDT-002/003 和最终 NSH 存活；TIMER-001 最大误差 827 us（0.827%），TIMER-002
  周期比例 2.000，WDT-002 在 3,026 ms 内喂狗 6 次且无复位。
- 完整配置、命令、运行流程、实测输出、制品数据和限制见
  [BL616CL UBSAN 配置与验证](bl616cl-ubsan.md)。

## 故障留证与可观测性

| ID | 能力与目标选项 | 状态 | 裁剪和依赖 | 验证要求 |
|---|---|---|---|---|
| D01 | CPU load：`SCHED_CPULOAD_SYSCLK` | 已验证（ST004） | `SYSTEM_CPULOAD` 只提供可裁剪测试负载；tick 同步采样只能作基础观测 | USB2 实测 idle、单个 50% 和两个 50% 聚合满载；procfs、`ps`、`top` 均随负载变化；关闭态裁剪成立 |
| D02 | IRQ monitor：`SCHED_IRQMONITOR` | 已验证（ST005） | 依赖 procfs；当前 alarm driver 已提供 `up_perf_*`；每 IRQ 两次时间读取开销已在实板观察 | USB2 实测 `irqinfo`/`/proc/irqs` 的 count/rate/max time、连续窗口重计数、GPIO/timer/oneshot IRQ 变化和外设回归；关闭态裁剪成立 |
| D03 | critical monitor：`SCHED_CRITMONITOR`、`SYSTEM_CRITMONITOR` | 已验证（ST006） | 正式配置只统计，不设告警阈值；阈值和 panic 使用临时配置独立验证 | USB2 实测全局/线程统计、读后新窗口、monitor 启停、告警与 panic 边界及外设回归；关闭态裁剪成立 |
| D04 | Note RAM trace：instrumentation、`DRIVERS_NOTERAM`、`SYSTEM_TRACE` | 已验证（ST011） | 正式只开 switch/IRQ，默认停止、8 KiB overwrite；测试 app 默认关闭；禁止 csection/spinlock 回写 Note RAM | USB2 实测 start/stop/dump、时间单调、事件顺序、过滤、严格 overflow、裁剪和外设回归 |
| D05 | syslog coredump：`COREDUMP`、`BOARD_COREDUMP_SYSLOG` | 已验证（ST010） | 单触发线程、无 full/compression/base64；零长度 memory range 哨兵；关闭彩色 syslog；负测 app 默认关闭 | USB2 完整 HEX 转换为 ELF32 RISC-V CORE；准确负测 ELF 恢复 LWP 9、PC/SP 和触发栈；最终产品裁剪与外设回归成立 |
| D06 | stack/cpu/resource monitor 工具 | 可直接开启，P1 | 分别依赖 coloration、cpuload、procfs；工具本身也消耗资源 | 启停、周期、输出和自身 CPU/stack 开销 |
| D07 | EXTCLK CPU load | 需要资源重构，P2 | TIMER1 当前由 `/dev/oneshot` 独占；必须用 choice 确定 owner | 与 SYSCLK 对照；异步采样；不能同时注册同一 lower-half |
| D08 | E907 hardware perf/perf-tools | 需要适配，P2 | 通用 `SCHED_PERF_EVENTS` 不等于硬件 PMU 已接入；依赖 A08 | cycle/event 正确性、溢出、用户工具和开销 |

Note RAM 不得与 `SCHED_INSTRUMENTATION_CSECTION` 或 spinlock hook 同时启用，
因为 ring buffer 自身进入 critical section，会形成 instrumentation 递归。D04 的
完整配置、命令、流程和实测数据见
[BL616CL Note RAM Trace 配置与验证](bl616cl-noteram-trace.md)。

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

### D02 实测结果（2026-08-28）

- 配置：在 D01 基线上通过 Kconfig 选择 `CONFIG_SCHED_IRQMONITOR=y`，保持
  `CONFIG_FS_PROCFS=y`、1 kHz tick 和 BL616CL MTIME `up_perf_*` 时间源；
  未启用 EXTCLK、tickless、PMU、CLIC threshold 或嵌套 IRQ。
- 通路：`riscv_doirq()` 进入通用 `irq_dispatch()`，每个 IRQ 前后读取
  MTIME 并更新 count/max time；`irqinfo` 和 `/proc/irqs` 复用该快照。
- 构建：D02 关闭态 clean build `1202/1202`，开启态 clean build
  `1204/1204` 均成功。关闭态 `/proc/irqs` 不存在且 `irqinfo` 未注册；
  开启态 ELF 含 `cmd_irqinfo`、`irq_foreach`、`g_irq_operations` 和
  `irq_dispatch`，并生成 `sched/irq/irq_foreach.c.o`、`irq_procfs.c.o`。
- 制品：开启态 `final_nuttx` 为 683,008 字节，`text/data/bss` 为
  `295,086/14,164/10,952`，`nuttx.bin` 为 315,360 字节。相对 D02 关闭态
  增加 ELF 640 字节、text 1,456 字节、data 128 字节、bss 1,200 字节、
  bin 1,584 字节；`g_irq_operations` 符号大小为 40 字节。
- 烧录与启动：`nuttx.bin` SHA256 为
  `61709c3f9eb5eb9c878372bcef80513be07ec8b7143b5c63ca6a08ccc320b0ea`；
  `/dev/ttyUSB2` 分区烧录的 host/device SHA 校验一致，2 Mbps 复位后匹配
  `NuttShell (NSH)` 和 `nsh>`。
- 基础统计：`irqinfo` 首次显示 MTIMER IRQ 23；随后 `/proc/irqs` 显示
  IRQ 23 约 1000.000 Hz、TIMER0 IRQ 60 和其他已挂接 IRQ。等待 8 秒的
  稳定窗口中 IRQ 23 为 44,606 次、rate 1000.000，`ps` IDLE 99.1%。
- 清零窗口：读取一次后再次读取会从新的窗口计数；`sleep 2` 后首个窗口
  IRQ 23 为 12,388 次、rate 1000.000，紧接读取的新窗口为 2 次、rate
  1000.000。由于 MTIMER 和 UART 在读取期间仍产生中断，不能期待字面 0，
  该结果证明的是快照重新计数而非静止硬件。
- 外设事件：timer 5 轮 100 ms 测试后 IRQ 60 计数 56；100 ms oneshot
  后新增 IRQ 69 计数 1。GPIO edge 64 轮恢复后 IRQ 76 计数 756；最终
  `irqinfo`/`/proc/irqs` 均能看到对应事件，GPIO、timer、oneshot、WDT
  回归全部通过。
- 开销：monitor 开启态稳定窗口 IDLE 约 99.1%，外设回归期间 IDLE 98.8%；
  统计含每 IRQ 两次 MTIME 读取及 count/max 更新，数值作为本固件窗口的
  观察结果，不外推为固定百分比性能损失。
- 语义边界：`TIME` 是窗口内最大 ISR 时间，单位微秒，不是平均耗时；读取
  会快照并清零，边界 IRQ 可能落在任一窗口。procfs 小缓冲多次读取的输出
  完整性依赖调用方式，本板 NSH 512 字节读取已完整显示现役 IRQ。

### D03 实测结果（2026-08-29）

- 正式配置：`CONFIG_SCHED_CRITMONITOR=y`、
  `CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION=0` 和
  `CONFIG_SYSTEM_CRITMONITOR=y`。生成配置中 thread、preemption、csection、
  IRQ 和 WDOG 阈值均为 0，只统计不告警；work queue 和 busy-wait 为 -1，
  不进入对应统计路径。monitor 只编入命令，不在启动脚本中自启。
- 通路修复：UP 配置在 csection monitor 开启时也用 `g_schedlock` 维护嵌套
  深度；`break_critical_section()` 只结算真实持锁区间，
  `restore_critical_section(0)` 不启动伪计时。结算前清除活动 caller，任务退出
  时在 TCB 仍为 current task 的阶段完成结算，避免阈值日志重入和 syslog
  semaphore 断言。
- 构建与裁剪：关闭态 clean build `1204/1204`、开启态 clean build
  `1207/1207` 均成功。关闭态不生成 `sched_critmonitor.c.o`、critmon app
  archive，也不含 `nxsched_critmon*`、`g_crit_max` 和三个 critmon 命令符号。
- 制品：开启态 `final_nuttx` 为 688,528 字节，`text/data/bss` 为
  `300,742/14,292/11,128`，`nuttx.bin` 为 321,136 字节。相对关闭态分别
  增加 5,520、5,656、128、176 和 5,776 字节；后台 monitor 启动后另占
  一个 2,048 字节任务栈。
- 烧录与启动：最终 `nuttx.bin` SHA256 为
  `24c7ca1ef01b57fc21a1b3f33923d27e5df79b072342bbdebc1e1e0b55ded8d4`；
  `/dev/ttyUSB2` 分区烧录的 host/device SHA 校验一致，2 Mbps 复位匹配
  `NuttShell (NSH)` 和 `nsh>`。
- 基础统计：启动窗口 `/proc/critmon` 为 preemption 3.127576 秒、csection
  117 us；`sleep 2` 后的新窗口降为 47 us 和 96 us。`/proc/3/critmon`
  显示 `nsh_main` 的 preemption/csection 最大值为 103/117 us，不再把普通
  调度间隔误记为秒级 critical section。
- 命令与周期任务：`critmon` 能输出 CPU 和各任务的 preemption、csection、
  run max/total；`critmon_start` 创建 `Csection_Monitor`，按 2 秒周期输出；
  `critmon_stop` 完成在途周期后停止，后续不再产生周期输出。
- 告警负测：临时设置 csection 阈值 150,000 tick、IRQ 阈值 0、panic 关闭。
  `critmon` 和 GPIO/timer/oneshot/WDT 任务退出分别观测到 193,000、161,000、
  197,000、165,000 和 172,000 tick 告警；全部任务正常返回，未再触发
  `sem_post.c:63` 断言。临时阈值未进入正式 defconfig。
- panic 负测：在同一临时 csection 阈值上单独启用
  `SCHED_CRITMONITOR_MAXTIME_PANIC`，`critmon` 退出时以 187,000 tick 命中，
  在 `sched_critmonitor.c:300` 断言；回溯包含
  `nxsched_critmon_csection -> nxtask_exit -> up_exit`。当前 assert 策略终止
  触发任务而不复位整机，随后 `echo alive` 正常返回。
- 外设回归：GPIO edge 64 轮恢复通过；timer 5 轮 100 ms 最大误差 479 us
  （0.479%，门限 0.50%），异常生命周期通过；100 ms oneshot 完成；WDT
  非复位生命周期通过。最终 `/proc/critmon` 的 preemption/csection 最大值为
  92/189 us，全程无意外 assert、panic 或复位。
- 语义边界：procfs 读取会返回并清零最大值，线程累计运行时间继续保留；启动
  窗口的秒级 preemption 值包含 bringup 阶段，不能当作稳态延迟。阈值单位是
  `perf_gettime()` tick，本板 MTIME 为 1 MHz，因此一个 tick 为 1 us。

### D04 实测结果（2026-08-29）

- 正式配置只启用 task switch 和 IRQ handler，filter default mode 为 `0x1`，
  Note RAM buffer 为 8,192 B，overwrite 默认开启；trace 开机保持停止。
- 通路：scheduler switch 和通用 IRQ dispatch 经 Note filter 写入 RAM backend；
  时间源复用 `arch_alarm`、BL616CL oneshot 和 RISC-V MTIMER，无私有 trace hook。
- 构建与裁剪：D04 关闭基线、验收 app 开启态和最终产品均 clean build 成功；
  最终产品为 `1217/1217`，保留 Note/trace archive 和产品符号，不含测试对象、
  archive、命令或 main。
- 制品：最终 `final_nuttx` 为 758,412 B，`text/data/bss` 为
  `364,894/14,848/20,076`，`nuttx.bin` 为 385,472 B。相对关闭基线增加
  ELF 14,888 B、text 10,888 B、data 364 B、bss 8,468 B 和 bin 11,248 B。
- 命令语义：非法 duration 不改变状态；普通 start 清空，`start -c` 追加，
  `start 1` 自动停止；overwrite、switch、IRQ 总开关和 99 个 IRQ mask 均可恢复。
- 事件证据：dump 观察到 task switch、software IRQ 11、MTIMER IRQ 23 和
  TIMER/UART IRQ 60，entry/exit 顺序正确，时间戳单调；全屏蔽 IRQ 后只剩 switch。
- overflow：no-overwrite 下 4,000 次 yield 后 mode=2、unread=8,164，clear 后
  mode=0/unread=0，恢复 overwrite 后 mode=1；overwrite 保留停止前最近窗口。
- 工具修复：`trace` 总览的 `NOTE_GETIRQFILTER` 改为传完整命名结构，USB2 对
  0/99/0 个屏蔽 IRQ 的总览和独立命令显示一致。
- 回归：最终产品 GPIO edge 64 轮、TIMER-001/002/005、100 ms oneshot、
  WDT-002/003 和最终 NSH 存活全部通过；100 ms timer 最大误差 299 us
  （0.299%），prescaler 比例 2.001。
- 边界：buffer 易失且有限；1 kHz switch 可快速覆盖旧数据。2 Mbps 大量 ASCII
  dump 出现过少数字符缺失，不能声明逐字节完整；其他事件域和 crash dump 未验证。

### D05 实测结果（2026-08-29）

- 正式配置启用 `COREDUMP`、`BOARD_COREDUMP_SYSLOG` 和零长度
  `BOARD_MEMORY_RANGE` 哨兵，关闭 full/compression/base64 与彩色 syslog；
  `BL_OS_FEATURE_TESTS_SYSLOG_COREDUMP` 默认关闭。
- 通路：bringup 在 board late initialize 后初始化通用 coredump stream；受控
  `d05_coredump` kernel thread 调用 `PANIC()`，进入系统 panic 并导出单个触发
  线程。普通 builtin task assert 只终止任务，不生成 coredump。
- 构建与裁剪：关闭态 clean build `1207/1207`、负测 `1211/1211`、最终产品
  `1209/1209` 均成功。最终产品保留 coredump 符号，不含测试命令、main 或 archive。
- 制品：最终产品 `final_nuttx` 为 743,524 B，`text/data/bss` 为
  `354,006/14,484/11,608`，`nuttx.bin` 为 374,224 B。相对关闭态增加 ELF
  5,476 B、text 4,456 B、data 192 B、bss 480 B 和 bin 4,656 B。
- 运行：USB2 的 safe 和命令边界正常返回；fatal 输出唯一完整
  `Start coredump:`/`Finish coredump. hex formatted`。首抓因一个 127 字符 HEX
  行被拒绝；有效重抓全部为偶数长度 HEX。
- 离线解码：24,731 B 原始数据转换为 6,348 B ELF32 little-endian RISC-V CORE，
  3 个 program headers 的末端等于文件大小；准确负测 ELF 恢复 LWP 9、
  `pc=__assert+82`、有效 SP/RA/FP 和
  `__assert -> coredump_fatal_thread -> nxtask_start`。
- 恢复与回归：同一串口 fd 复位后重新进入 2 Mbps NSH。最终产品 `help` 不含
  测试命令；GPIO edge、TIMER-001/002/005、100 ms oneshot、WDT-002/003 和
  最终存活检查全部通过。
- 语义边界：core 无 build-id，必须冻结并校验准确 ELF/bin/config 身份；当前
  `DEBUG_SYMBOLS=n` 只承诺函数符号栈。kernel-thread panic 保存 coredump 期间的
  PC/SP；early bringup、full dump、真实 RAM region、压缩和 base64 未覆盖。

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
| P02 | TRNG `/dev/random` | 已验证（ST015） | `BL616CL_TRNG`；测试 app 独立关闭；可选 `DEV_URANDOM_ARCH` | chip adapter 直接实现 `devrandom_register()`；USB2 已验证任意长度、非对齐、标准 API、基本统计、多线程、裁剪和外设回归 |
| P03 | RTC/Alarm | 已验证（ST016）；upper-half ioctl 补全（ST017） | `BL616CL_RTC`、`BL616CL_RTC_ALARM`；ioctl 测试 app 独立关闭；RC32K/DIG32K 二选一 | 48 位 HBN RTC lower-half 与 `/dev/rtc0`；UTC/亚秒、absolute/relative Alarm、取消/替换/re-arm、回绕、warm reset、unlink 和裁剪已验证；九个标准 ioctl 的 NULL/ID/ENOSYS 合同已在 debug/release fake lower 实测 |
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

### P02 TRNG 实测结论

- `CONFIG_BL616CL_TRNG` 条件编入 `bl616cl_rng.c` 并选择 `ARCH_HAVE_RNG`；正式
  配置注册只读 `/dev/random`，测试 app 默认关闭。
- adapter 没有编入或修改同步仓的 `bflb_sec_trng.c`，而是复用 device table、SEC
  时钟和寄存器定义，补齐两阶段 timeout、`HT_ERROR`、严格清理、任意长度和整次
  read mutex。
- 关闭、测试和正式 clean build 分别为 1219/1219、1222/1222、1220/1220；正式
  相对关闭态 `nuttx.bin +1648 B`、bss `+0 B`。
- USB2 上 `0/1/3/4/31/32/33/255/256/257` B 非对齐读全部 PASS；4096 B 四轮
  bit 千分比为 496/497/498/501，固定块和重复块均为 0；4 线程 x 32 次共六轮
  全部完成。
- 正式固件无 `mcu_trng_test`，`/dev/random` 权限为 `cr--r--r--`，128 B `dd`
  返回 NSH；GPIO、timer、oneshot、WDT 和最终存活回归通过。
- timeout、`HT_ERROR` cleanup 未做故障注入；单核 FIFO 多线程结果不确定性证明
  mutex 真实竞争；短样本不用于熵或合规声明。

### P03 RTC/Alarm 实测结论

- `CONFIG_BL616CL_RTC` 条件编入基础 RTC、arch bridge 和 `/dev/rtc0`；
  `CONFIG_BL616CL_RTC_ALARM` 再启用单路 compare、HBN_OUT0 IRQ 和 Alarm
  callback。adapter 位于 `arch/libarch.a`，不进入 `libbl_std.a`，测试 app
  正式配置关闭。
- RTC off、RTC on/Alarm off、RC32K 验收、DIG32K 验收和 DIG32K 正式产品的
  clean build 分别为 1220/1220、1224/1224、1226/1226、1226/1226、
  1224/1224；archive、ELF 和 builtin 命令裁剪与配置一致。
- RTC on/Alarm off/test 的 clean build 为 1226/1226；RTC-001/004 正常执行，
  RTC-002/003 明确 SKIP，`all` 汇总为 `PARTIAL (0 failures, 2 skipped)`，
  RTC-005 单独报告统一 SKIP；两条命令在 NSH 中均返回 1，runner 同时解析汇总
  文本以区分 SKIP/PARTIAL 与 FAIL，未把未编译的 Alarm 能力误报为 PASS。
- RTC-001~003 验证 UTC 闰日、连续亚秒跨秒单调、非法日期、2038 边界、系统时钟同步、
  absolute/relative Alarm、active query、cancel、replace、re-arm、settime
  重编程、0.5 ms 短 absolute Alarm 和 200 ms 重复通知静默检查。RC32K 常规 Alarm 为
  1986~1987 ms，DIG32K 为 1998~2000 ms；DIG32K 短 Alarm 实测 2 ms。
- RTC-004 五轮以 MTimer `CLOCK_MONOTONIC` 为参考：RC32K 相对误差为
  6378~6409 ppm，最新 DIG32K 为 2~8 ppm；软件验证 48 位回绕差值 16 ticks。
  该结果是相对比较而非绝对校准，正式默认据此选择 DIG32K。
- 两种时钟各完成三轮 active Alarm warm reset：每轮复位后 3 秒无旧通知，
  `RTC_HAVE_SET_TIME=0`、Alarm inactive，并可重新布防。RTC-005 验证当前
  `SIGEV_SIGNAL` 下双 fd unlink/最后关闭取消流程；正式固件的 `/dev/rtc0`、
  TRNG、GPIO、timer、oneshot、WDT 和 NSH 回归通过。
- 最终 DIG32K/no-test clean build 为 1224/1224，`nuttx.bin` 为 479888 B，
  SHA256 为 `c999b23d1e84ba3683ac1c1806627fde3d881d9c7c1520790c2b8dec7ed7aa85`；
  app 分区烧录的 host/device SHA256 一致，产品回归脚本为 `failures=[]`。
- ST017 在独立 NuttX 分支对九个标准 ioctl 增加 release 可用的 NULL/ID 检查，
  scalar cancel 先校验原始 `unsigned long arg` 再转换，避免宽 ABI 截断。vendor
  fake lower-half 覆盖所有标准方法的 `ENOSYS`、正常调用方法/顺序/参数、upper
  active 状态保持和私有 ioctl 转发；debug 与 `DEBUG_ASSERTIONS=off` 实板矩阵
  均为 `cases=39 failures=0`，x86-64 sim 的 wrapped-ID 矩阵为
  `cases=41 failures=0`。
- 当前未启用 `SYSTEM_TIME64`、`RTC_PERIODIC`、`SIGEV_THREAD`、PM/HBN
  wakeup 或 HBN_OUT0 公共 demux；warm reset 结果不外推到断电保持。NuttX RTC
  upper-half 参数校验已提交 PR `#357`；并发生命周期问题仍由 ST018/ST019
  独立闭环。

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
| 9 | P02、P03 已完成；继续 P04、P05、P06 | I2C、SPI、PWM 逐项适配；RTC 扩展能力另按独立子任务补全 |
| 10 | A07+P08，再做 P07/P09/P11 | cache/DMA 是 ADC、crypto、MTD 的公共前置 |

同一行合并的配置只表示一个不可分割的验收闭包；不同表项不得合并成一个
commit。每项完成时至少记录静态、构建、烧录/启动和功能运行四层；故障诊断项
还必须包含可控负测与正常回归。
