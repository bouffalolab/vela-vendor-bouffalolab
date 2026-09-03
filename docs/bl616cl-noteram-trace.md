# BL616CL Note RAM Trace 配置与验证

本文说明 BL616CL 如何使用 OpenVela 通用 Note RAM 记录任务切换和 IRQ 事件，
并给出配置、构建、运行、过滤、overflow 和回归的完整流程。烧录沿用 SDK 通用
流程，本文的固件运行步骤从 `/dev/ttyUSB2` 已进入 2 Mbps NSH 开始。

## 背景

BL616CL 已接入通用 scheduler、IRQ dispatch、alarm oneshot 和 MTIME。发生调度
抖动、阻塞或异常前事件时，普通统计只能给出次数或最大值，不能还原事件先后关系。
OpenVela Note driver 可以在 scheduler 和 IRQ 热路径编码事件，Note RAM 将事件写入
内存环形 buffer，`trace` 命令负责控制、过滤和导出。

本次只纳入两个能直接解释现役系统行为的事件域：

- task switch：任务创建、唤醒和切换；
- IRQ handler：中断进入和退出。

syscall、heap、watchdog、dump、function、preemption 等事件尚未完成独立开销和
递归审计，不随本次正式配置打开。csection 和 spinlock hook 明确排除：Note RAM
写入自身进入 critical section，将这两类事件回写同一 backend 会形成递归。

## 正式配置与裁剪边界

`nsh` defconfig 显式保存：

```text
CONFIG_SCHED_INSTRUMENTATION_FILTER_DEFAULT_MODE=0x1
CONFIG_SCHED_INSTRUMENTATION_SWITCH=y
CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER=y
CONFIG_DRIVERS_NOTERAM_BUFSIZE=8192
CONFIG_SYSTEM_TRACE=y
```

Kconfig 依赖自动得到：

```text
CONFIG_SCHED_INSTRUMENTATION=y
CONFIG_SCHED_INSTRUMENTATION_FILTER=y
CONFIG_DRIVERS_NOTE=y
CONFIG_DRIVERS_NOTERAM=y
CONFIG_DRIVERS_NOTECTL=y
```

以下选项保持关闭：

```text
# CONFIG_SCHED_INSTRUMENTATION_PREEMPTION is not set
# CONFIG_SCHED_INSTRUMENTATION_CSECTION is not set
# CONFIG_SCHED_INSTRUMENTATION_SPINLOCKS is not set
# CONFIG_SCHED_INSTRUMENTATION_SYSCALL is not set
# CONFIG_SCHED_INSTRUMENTATION_HEAP is not set
# CONFIG_SCHED_INSTRUMENTATION_WDOG is not set
# CONFIG_SCHED_INSTRUMENTATION_DUMP is not set
# CONFIG_SCHED_INSTRUMENTATION_FUNCTION is not set
# CONFIG_DRIVERS_NOTERAM_DEFAULT_NOOVERWRITE is not set
# CONFIG_DRIVERS_NOTERAM_CRASH_DUMP is not set
# CONFIG_BL_OS_FEATURE_TESTS_NOTERAM is not set
```

`FILTER_DEFAULT_MODE=0x1` 的 bit 0 表示全局默认停止。固件启动后 backend 和设备
节点已经注册，但不会静默采集事件；必须显式执行 `trace start`。默认使用 overwrite
模式，buffer 满后保留最近事件窗口。

关闭两个事件域后，scheduler/IRQ hook 可裁掉；关闭 `DRIVERS_NOTERAM` 后，RAM
backend 和 8192 B 静态 buffer 可裁掉；关闭 `SYSTEM_TRACE` 后，NSH 控制命令可
裁掉。验收 app 有独立 Kconfig，正式产品不包含它的命令、对象或 archive。

## 调用链与时间源

初始化链：

```text
nx_start()
  -> drivers_initialize()
  -> note_initialize()
  -> noteram_register()       -> /dev/note/ram
  -> notectl_register()       -> /dev/notectl
```

任务切换链：

```text
nxsched_switch_context()
  -> sched_note_suspend()/sched_note_resume()
  -> sched_note_add()
  -> Note filter
  -> noteram_add()
```

IRQ 链：

```text
irq_dispatch()
  -> sched_note_irqhandler(enter)
  -> ISR
  -> sched_note_irqhandler(exit)
  -> sched_note_add()
  -> Note filter
  -> noteram_add()
```

`note_common()` 使用通用 performance time。当前配置由 `arch_alarm` 的
`up_perf_gettime()` 调用 BL616CL oneshot lower-half `current()`，最终读取
RISC-V MTIMER，并由 `up_perf_convert()` 转为秒和纳秒。时间链已有
`noinstrument` 边界，不需要新增 BL616CL 私有 trace 时钟。

## 构建与制品检查

正式产品构建：

```sh
vendor/bouffalolab/vela clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
vendor/bouffalolab/vela build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

完成判据：

- clean build 完成 `1217/1217`；
- `drivers/libdrivers.a` 含 `note_driver.c.o`、`noteram_driver.c.o` 和
  `notectl_driver.c.o`；
- `apps/system/trace/libapps_trace.a` 含 `trace.c.o` 和 `trace_dump.c.o`；
- ELF 含 `g_ramnote_buffer`、`noteram_register`、`notectl_register`、
  `sched_note_irqhandler` 和 `trace_main`；
- 验收 app 目录下没有 `.o` 或 `.a`，ELF 不含其 main，NSH 不注册
  `noteram_test`。

核对命令：

```sh
prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf-ar t \
  cmake_out/ai-m64l-32s-kit_nsh/drivers/libdrivers.a | grep note
prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf-ar t \
  cmake_out/ai-m64l-32s-kit_nsh/apps/system/trace/libapps_trace.a
prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf-nm \
  cmake_out/ai-m64l-32s-kit_nsh/final_nuttx | \
  grep -E 'g_ramnote_buffer|noteram_register|notectl_register|sched_note_irqhandler|trace_main'
```

本次最终产品制品：

| 制品 | 大小 | SHA256 |
|---|---:|---|
| `final_nuttx` | 758,412 B | `5e5e1eb097e135fbeca1f5d9cc484f6600c2fbe7b0b9fb91e9e18c6416daaf06` |
| `nuttx.bin` | 385,472 B | `f483cd5a2cfef20e46e5b0b6ce1d04e03ea2e168a6a6d9e7ac0fdf79a1c608ff` |
| `nuttx.whole.bin` | 4,194,304 B | `b1f49c6e19e7b20b6f21f701eb9a64aa74599caedbe7155671640dd1f6853357` |

ELF 的 text/data/bss 为 `364894/14848/20076` B。相对 D04 关闭基线，正式
能力增加 ELF 14,888 B、text 10,888 B、data 364 B、bss 8,468 B 和 bin
11,248 B；bss 增量主要是 8,192 B Note RAM buffer。临时验收 app 另增加
text 1,256 B、data 64 B 和 bin 1,328 B，不属于正式产品开销。

## 正式产品运行流程

### 1. 建立单一串口会话

```sh
picocom --baud 2000000 --lower-rts --noreset /dev/ttyUSB2
```

看到 `NuttShell (NSH)` 和 `nsh>` 后保持 fd 打开，后续命令复用同一会话。
不要并发打开另一个串口工具。

### 2. 检查默认状态和设备节点

```text
trace
trace irq
ls /dev/note
ls /dev/notectl
```

本次实测关键输出：

```text
Task trace mode(ram):
 Trace                   : disabled
 Trace filter mode       : 0000000000000001
 Overwrite               : on  (+o)
 Switch trace            : on (+w)
 IRQ trace               : on  (+i)
  Filtered IRQs          : 0
Filtered IRQs: 0
/dev/note:
 ram
 /dev/notectl
```

完成判据：trace 默认停止、overwrite/switch/IRQ 已配置，两个设备节点存在。

### 3. 采集并导出最小窗口

```text
trace start
trace stop
trace dump
```

`trace start` 默认先清空旧 buffer，再开启记录；`trace stop` 关闭全局采集；
`trace dump` 以 ASCII 输出并消费当前 unread 数据。本次最终产品的最小窗口为：

```text
CPU0 IDLE-0 [0] 2.438100816: irq_handler_entry: irq=11 name=riscv_swint+0/0xb2
nsh_main-3 [0] 2.438115816: irq_handler_exit: irq=11 ret=handled
nsh_main-3 [0] 2.438125816: sched_switch: ... next_comm=nsh_main next_pid=3
nsh_main-3 [0] 2.438163816: irq_handler_entry: irq=23 name=riscv_mtimer_interrupt+0/0x64
nsh_main-3 [0] 2.438245816: irq_handler_exit: irq=23 ret=handled
```

完成判据：IRQ entry/exit 成对，switch 和 IRQ 顺序可解释，时间戳单调递增。

### 4. 定时采集、追加和过滤

```text
trace start 1          # 采集 1 秒并自动停止
trace start -c         # 不清空，追加到现有 buffer
trace mode -w +i +o   # 只保留 IRQ，overwrite on
trace mode +w +i +o   # 恢复 switch 和 IRQ
trace irq -*           # 屏蔽全部 IRQ
trace irq +*           # 恢复全部 IRQ
```

`trace start 0` 和 `trace start x` 均实测返回 `invalid argument`，trace 保持停止，
buffer 不变。已有 8,172 B 数据时再次普通 `start` 后立即 `stop`，unread 降为
388 B，证明默认清空；在 388 B 上执行 `start -c` 和 10 次 yield 后，unread
增至 1,308 B，证明追加。

全部 99 个 IRQ 屏蔽后，1 秒 dump 只出现 `sched_switch`；恢复后关闭 switch 的
最小窗口只出现 IRQ 11 和 IRQ 23 entry/exit。`trace` 总览与 `trace irq` 对 0 和
99 个屏蔽项均显示一致。

## Overflow 验收流程

严格 overflow 需要读取 Note RAM ioctl 状态，使用默认关闭的验收 app。临时开启：

```sh
prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak \
  --file cmake_out/ai-m64l-32s-kit_nsh/.config \
  --enable BL_OS_FEATURE_TESTS_NOTERAM
cmake --build cmake_out/ai-m64l-32s-kit_nsh -t savedefconfig
vendor/bouffalolab/vela clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
vendor/bouffalolab/vela build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

固件运行命令：

```text
trace mode -o +w +i
trace start
noteram_test burst 4000
trace stop
noteram_test status
noteram_test clear
noteram_test status
trace mode +o
noteram_test status
```

本次实测：

```text
NOTERAM_TEST STATUS mode=2 unread=8164
NOTERAM_TEST CLEAR complete
NOTERAM_TEST STATUS mode=0 unread=0
NOTERAM_TEST STATUS mode=1 unread=0
```

`mode=2` 是 `NOTE_MODE_OVERWRITE_OVERFLOW`，证明 no-overwrite 在 buffer 满后
停止写入，而不是覆盖旧数据。clear 后 mode 回到 0；重新开启 overwrite 后 mode
为 1。overwrite 模式持续 1 秒后 unread 为 8,160~8,172 B，dump 保留停止前的
最近窗口。

验收完成后必须关闭测试 app，通过 `savedefconfig` 生成正式配置并再次 clean
build。最终产品实测执行 `noteram_test status` 返回：

```text
nsh: noteram_test: command not found
```

## 最终产品回归

在同一 USB2 串口 fd 执行：

```text
mcu_gpio_test -c edge --out /dev/gpio12 -n 64 -r
mcu_timer_test -c 001 -t 100000 -n 5 -e 0.5 -v
mcu_timer_test -c 002 -t 500000 -a 39 -b 79 -v
mcu_timer_test -c 005
oneshot -d 100000 /dev/oneshot
mcu_wdt_test -c 002 -t 3000 -p 9000 -i 1000 -v
mcu_wdt_test -c 003 -t 3000
echo ST011_FINAL_ALIVE
```

实测结果：

| 用例 | 关键数据 | 结果 |
|---|---|---|
| GPIO edge | 64 轮非法操作拒绝与状态恢复 | PASS |
| TIMER-001 | 100 ms、5 轮，最大误差 299 us（0.299%），门限 500 us | PASS |
| TIMER-002 | divider 39/79 周期 0.4997/1.0000 s，比例 2.001 | PASS |
| TIMER-005 | 五类非法/重复请求被拒绝，live update 后恢复 | PASS |
| oneshot | 100,000 us，输出 `Finished` | PASS |
| WDT-002 | 3,000 ms timeout，9,009 ms 内喂狗 9 次，停止后无复位 | PASS |
| WDT-003 | 非法/live 修改和重复生命周期保持状态，停止后无复位 | PASS |
| 最终存活 | 输出 `ST011_FINAL_ALIVE` | PASS |

trace 默认停止时 `/proc/cpuload` 为 0.0%，采集中为 0.8%，停止后为 0.0%；
对应 critical monitor 窗口仍能读取，`ps` 的 NSH 栈高水位保持 54.6%。这些数值
只描述本次短窗口，不作为固定性能损失。最终回归结束后 CPU load 为 0.6%，系统
仍在 NSH，无意外 assert、panic 或复位。

## 工具修复

运行中发现 `trace irq` 显示 0 个屏蔽 IRQ，而 `trace` 总览错误显示 62。原因是
总览调用 `NOTE_GETIRQFILTER` 时传入 `irq_mask` 子字段地址；driver 按
`note_filter_named_irq_s` 完整结构解释参数，导致读取位置错误并存在越界写风险。
修复后总览传入完整结构地址，USB2 已对 0/99/0 三个状态逐项回读一致。

## 适用边界

- Note RAM 位于易失 RAM，不跨复位保存；本次不启用 panic 自动 dump。
- 8,192 B 是有限窗口。1 kHz tick 下 switch 事件可在约 0.3 秒内填满 buffer；
  分析前应缩小事件域或及时停止，不能假设覆盖整个故障过程。
- overwrite 保留最近窗口，no-overwrite 保留开始窗口；两者用途不同。
- ASCII dump 在 2 Mbps 大流量下出现过少数字符缺失。事件类型和时间单调性已
  验证，但大量串口文本不能作为逐字节完整记录；需要完整原始数据时使用 binary
  dump、文件或更可靠 host 传输，并另做校验。
- SYSCLK CPU load、critical monitor 和 Note instrumentation 都进入热路径；本次
  只证明组合可运行，短窗口 CPU 数值不能外推为通用性能基准。
- syscall、heap、watchdog、dump、function、preemption、csection、spinlock、
  crash dump 和跨复位 trace 均未在本次启用或验证。
