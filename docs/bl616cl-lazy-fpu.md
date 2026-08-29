# BL616CL RISC-V Lazy FPU 配置与验证

本文说明 BL616CL 如何启用 OpenVela RISC-V lazy FPU context，修复启用前发现的
user context 越界和异步 signal FPU 状态覆盖问题，并给出可重复的上下文布局、双任务
隔离、异步 signal、coredump、正式裁剪和外设回归流程。运行环境为
Ai-M64L-32S-Kit、`/dev/ttyUSB2`、2,000,000 baud。

## 背景

BL616CL 使用 E907 RV32IMAF+C，当前工具链 ABI 为 `ilp32f`。RISC-V 架构已经通过
`ARCH_HAVE_FPU` 自动选择 `ARCH_HAVE_LAZYFPU`，但正式配置原先没有打开
`CONFIG_ARCH_LAZYFPU`。

非 lazy 模式把 33 个 FPU 状态项，也就是 32 个单精度 FPR 和 FCSR，放在每个异常
frame 的整数寄存器之后。BL616CL 当前布局为：

```text
INT_XCPT_SIZE=132
FPU_XCPT_SIZE=132
XCPTCONTEXT_SIZE=264
```

lazy 模式把普通异常 frame 缩为 132 B 整数状态，并在 TCB 中保留独立 FPU 保存区。
异常未引发任务切换时，不需要在异常栈上保存和恢复 132 B FPU 状态。任务切换时，
`riscv_savefpu()` 只在 FS=Dirty 时保存，`riscv_restorefpu()` 只在 FS 为 Clean 或
Dirty 时恢复。

该优化针对“未发生任务切换的普通异常/中断”减少栈占用和 FPU 搬运。本文没有用
cycle counter 测量切换延迟，也不把双任务运行时长作为性能数据。

## 启用前发现的上游缺陷

### `up_saveusercontext()` 缓冲区越界

原实现使用 `XCPTCONTEXT_SIZE` 分配 coredump 和 gdbstub 的当前任务寄存器缓冲区，
但 `up_saveusercontext()` 在启用 FPU 时始终写入整数状态和完整 FPU 状态。lazy 模式
下缓冲区只有 132 B，函数实际写入 264 B，固定越界 132 B。

修复后区分两个尺寸：

```text
XCPTCONTEXT_SIZE=132
SAVEUSERCONTEXT_SIZE=264
```

普通异常和任务整数 frame 继续使用 `XCPTCONTEXT_SIZE`；调用
`up_saveusercontext()` 的 coredump 和 gdbstub 使用 `SAVEUSERCONTEXT_SIZE`。

### 异步 signal 覆盖原任务 FPU 状态

原 lazy TCB 只有一份 `fregs`。异步 signal 只复制整数 frame；如果 handler 使用
FPU 并发生调度，handler 状态会写回同一份 `fregs`，signal 返回后无法恢复被中断
任务原有的 FPR/FCSR。即使 handler 不使用 FPU，原任务处于 Dirty 状态时也可能从
旧保存区恢复。

修复后 signal trampoline 在 TCB 中使用第二份 `saved_fregs`：

```text
up_schedule_sigaction()
  -> 按被中断整数 frame 的 FS 状态保存当前 FPU
  -> fregs 复制到 saved_fregs
  -> 复制 132 B 整数 frame 并进入 signal handler

signal handler
  -> 可以使用 caller-saved FPR
  -> 可以阻塞或被抢占，运行态仍使用 fregs

riscv_sigdeliver()
  -> saved_fregs 复制回 fregs
  -> 恢复 signal 前整数 frame
  -> riscv_fullcontextrestore() 恢复原 FPU 状态
```

OpenVela `rel-4.0` 的 `fcbf642` 已在 signal 调度时把当前任务的 live FPU 状态
保存到 `fregs`，可避免 Dirty 状态从旧保存区恢复。当前 `trunk-5.5` 基线没有该
提交；而且只保存到运行态 `fregs` 仍不足以覆盖 handler 使用 FPU 后发生阻塞或抢占的
场景。因此当前修复保留 live save 语义，并增加独立 `saved_fregs` 快照。

对应 NuttX signed commit 为 `ebf0cb7ddd5`，上游 PR 为
`open-vela/nuttx#356`。正式启用依赖该修复或其上游等价实现，不能只提交 vendor
配置后搭配未修复的 NuttX 使用。

## 正式方案与裁剪边界

### 正式产品配置

正式 `nsh` 只新增：

```text
CONFIG_ARCH_LAZYFPU=y
# CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU is not set
# CONFIG_BL_OS_FEATURE_TESTS_SYSLOG_COREDUMP is not set
```

`ARCH_LAZYFPU` 依赖 RISC-V 已有的 `ARCH_FPU` 和 `ARCH_HAVE_LAZYFPU`，不增加
BL616CL 私有 arch hook。关闭该选项后恢复 264 B eager 异常 frame，TCB 中的
lazy `fregs` 和 signal `saved_fregs` 均被预处理裁掉。

专项测试 app 使用独立开关 `CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU`，默认关闭。
关闭时 CMake 不调用 `nuttx_add_application()`，测试 C/汇编对象、命令和字符串不进入
最终固件。迭代次数、切换间隔、主任务栈和 worker 栈均有独立 Kconfig 参数。

### 临时测试配置

受控测试固件在正式配置上增加：

```text
CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU=y
CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU_PRIORITY=100
CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU_STACKSIZE=3072
CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU_WORKER_STACKSIZE=2048
CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU_ITERATIONS=200
CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU_SLEEP_US=1000
CONFIG_BL_OS_FEATURE_TESTS_SYSLOG_COREDUMP=y
```

测试包含三条独立路径：

1. 两个同优先级线程分别把不同 bit pattern 放入 `fs0-fs11` 和 FCSR；每个线程
   执行 200 次 1 ms sleep，强制阻塞、调度和 MTIMER 中断，结束后逐项比较。
2. 一个 worker 线程把不同 bit pattern 放入全部 32 个 FPR 和 FCSR，并创建
   10 ms POSIX one-shot timer。timer 使用 `SIGEV_SIGNAL | SIGEV_THREAD_ID`，将
   `SIGUSR2` 绑定到该 worker；integer-only handler 记录自身 TID 并结束忙等。测试
   要求 `wait=done` 且 worker/handler TID 相同，signal 返回后逐项比较原 32 个
   FPR 和 FCSR。
3. 一个低优先级目标线程接收 `pthread_kill()` 投递的 `SIGUSR1`。handler 改写全部
   caller-saved FPR 和 FCSR，并执行三次 1 ms sleep；signal 返回后逐项比较原
   32 个 FPR 和 FCSR。

第二、三条路径不能用当前线程 `raise()` 代替，因为当前线程同步投递可能绕过需要
验证的异步 signal trampoline。running 路径还必须显式区分 handler 已在忙等中运行
(`wait=done`) 和有限循环耗尽 (`wait=timeout`)；后者不得计为通过。

## 配置与构建流程

以下命令均在 SDK 根目录执行。`defconfig` 是生成文件，使用 `.config` 和
`savedefconfig` 更新。

```sh
source build/envsetup.sh
OUT=cmake_out/ai-m64l-32s-kit_nsh
TARGET=bl616cl/ai-m64l-32s-kit/configs/nsh
TWEAK=prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak
```

### 1. 建立 eager 对照

```sh
python3 vendor/bouffalolab/bl_build.py build "$TARGET" -j14

"$TWEAK" --file "$OUT/.config" \
  --disable ARCH_LAZYFPU \
  --disable BL_OS_FEATURE_TESTS_LAZY_FPU
cmake --build "$OUT" -t savedefconfig

python3 vendor/bouffalolab/bl_build.py clean "$TARGET"
python3 vendor/bouffalolab/bl_build.py build "$TARGET" -j14
```

完成判据：`.config` 中 `ARCH_LAZYFPU` 和测试 app 均关闭；异常入口反汇编分配
264 B frame；clean build 成功。

### 2. 构建 lazy 专项测试固件

```sh
"$TWEAK" --file "$OUT/.config" \
  --enable ARCH_LAZYFPU \
  --enable BL_OS_FEATURE_TESTS_LAZY_FPU \
  --enable BL_OS_FEATURE_TESTS_SYSLOG_COREDUMP
cmake --build "$OUT" -t savedefconfig

python3 vendor/bouffalolab/bl_build.py clean "$TARGET"
python3 vendor/bouffalolab/bl_build.py build "$TARGET" -j14
```

完成判据：clean build `1224/1224` 成功；异常入口分配 132 B frame；最终 ELF
含两个测试入口、`riscv_savefpu()`、`riscv_restorefpu()` 和 signal 快照路径。

### 3. 恢复正式产品并 clean build

```sh
"$TWEAK" --file "$OUT/.config" \
  --enable ARCH_LAZYFPU \
  --disable BL_OS_FEATURE_TESTS_LAZY_FPU \
  --disable BL_OS_FEATURE_TESTS_SYSLOG_COREDUMP
cmake --build "$OUT" -t savedefconfig

python3 vendor/bouffalolab/bl_build.py clean "$TARGET"
python3 vendor/bouffalolab/bl_build.py build "$TARGET" -j14
```

完成判据：clean build `1219/1219` 成功；正式配置保留 lazy FPU，两个测试命令
关闭；最终 ELF 和聚合 app archive 不含测试对象、入口或字符串。

## 静态制品核查

```sh
OUT=cmake_out/ai-m64l-32s-kit_nsh
TOOL=prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf

grep -E 'CONFIG_ARCH_(HAVE_)?LAZYFPU|BL_OS_FEATURE_TESTS_(LAZY_FPU|SYSLOG_COREDUMP)' \
  "$OUT/.config"

"$TOOL-objdump" -d "$OUT/final_nuttx" | \
  sed -n '/<exception_common>:/,/^$/p'
"$TOOL-objdump" -d "$OUT/final_nuttx" | \
  sed -n '/<riscv_savefpu>:/,/^$/p'
"$TOOL-nm" -S "$OUT/final_nuttx" | \
  grep -E 'g_idletcb|g_running_regs|riscv_(save|restore)fpu'

! grep -E 'lazy_fpu_test\\.c\\.o|lazy_fpu_registers\\.S\\.o' \
  "$OUT/build.ninja"
! strings "$OUT/final_nuttx" | grep -E 'LAZY_FPU_TEST|lazy_fpu_test'

"$TOOL-size" "$OUT/final_nuttx"
sha256sum "$OUT/final_nuttx" "$OUT/nuttx.bin" "$OUT/nuttx.whole.bin"
```

正式反汇编中 `exception_common` 首条指令为 `addi sp,sp,-132`。
`riscv_savefpu()` 只在 FS=Dirty 时执行 32 个 `fsw` 和 FCSR 保存；
`riscv_restorefpu()` 只在 FS 大于 Initial 时执行 32 个 `flw` 和 FCSR 恢复。

## USB2 单 fd 测试流程

Ai-M64L-32S-Kit 打开 USB-UART 时会因 modem line 瞬时变化而重启。运行验证必须
在一个 fd 中完成，流程如下：

1. 以 2,000,000 baud、8N1、raw mode、无流控且无 `HUPCL` 打开 USB2 一次。
2. open 后立即设置运行态 `DTR=1, RTS=0`，等待 `NuttShell (NSH)` 和 `nsh>`。
3. 连续三次执行 `lazy_fpu_test`。每轮分别核对 layout、两个 worker、running
   signal 的 `wait=done tid_match=1`、noncurrent signal 的 `wait=done`、32 个
   FPR/FCSR、非零 MTIMER tick 和最终 PASS；任一 mismatch 或 timeout 立即失败。
4. 执行 `syslog_coredump_test safe`，核对函数正常返回。
5. 执行 `syslog_coredump_test fatal`，等待完整 `Start coredump:` 和
   `Finish coredump. hex formatted`；随后在同一 fd 执行固定硬件复位并重新等待 NSH。
6. 依次执行 GPIO edge、timer 001/002/005、oneshot、WDT 002/003；每条命令等待
   自己的新 `nsh>` 并核对独立 PASS/完成标志。
7. 输出 `ST014_FINAL_ALIVE`，退出前恢复 `DTR=1, RTS=0`，再关闭 fd。
8. 正式产品执行相同外设回归，但先运行 `help`，确认
   `lazy_fpu_test` 和 `syslog_coredump_test` 均不存在。

## Lazy FPU 专项实测

三轮使用相同配置，全部通过。每轮均确认 running signal 由 timer 定向到持有
Dirty FPU 的 worker，第一轮关键输出为：

```text
LAZY_FPU_TEST BEGIN iterations=200 sleep_us=1000
LAZY_FPU_TEST MODE=lazy int=132 fpu=132 frame=132 save=264
LAZY_FPU_TEST TASK id=1 join=0 errors=0
LAZY_FPU_TEST TASK id=2 join=0 errors=0
LAZY_FPU_TEST SIGNAL target=running handler=integer wait=done tid_match=1 count=1 fprs=32 fcsr=00000040 errors=0
LAZY_FPU_TEST SIGNAL target=noncurrent handler=fpu wait=done count=1 fprs=32 fcsr=00000040 errors=0
LAZY_FPU_TEST TIMER ticks=434
LAZY_FPU_TEST RESULT PASS
```

第二、三轮的两个任务和两条 signal 结果仍为 `errors=0`，MTIMER 分别为 435 和
434 ticks，最终均输出 `LAZY_FPU_TEST RESULT PASS`。这三轮同时覆盖：

- `fs0-fs11` 跨 400 次 worker sleep 保持各自 bit pattern；
- running worker 在忙等期间接收一次线程定向异步 signal；
- 非当前目标线程接收一次 `pthread_kill()` 异步 signal；
- handler 改写 caller-saved FPR/FCSR 并发生三次阻塞切换；
- signal 返回后 32 个 FPR 和 FCSR 全部恢复；
- 运行期间持续存在 MTIMER 中断。

## Coredump 与正式裁剪实测

测试固件的 safe 路径正常返回：

```text
SYSLOG_COREDUMP_TEST SAFE begin
SYSLOG_COREDUMP_TEST SAFE returned
nsh>
```

fatal 路径输出触发点、寄存器、线程、内存和完整 HEX 数据，最终出现：

```text
SYSLOG_COREDUMP_TEST FATAL trigger pid=0 thread=d05_coredump
Start coredump:
Finish coredump. hex formatted
```

硬件复位后重新进入 `NuttShell (NSH)` 和 `nsh>`。这证明
`SAVEUSERCONTEXT_SIZE=264` 的当前任务保存缓冲区没有破坏 coredump 路径；本项没有
重新执行 D05 的离线 ELF 解码，离线解码能力仍以 D05 专项验收为准。

正式产品的 `help` 中 Builtin Apps 只包含常规命令，未出现
`lazy_fpu_test` 或 `syslog_coredump_test`。Ninja 中两个 lazy 测试对象不存在，最终
ELF 也没有 `LAZY_FPU_TEST` 字符串。

## 外设回归实测

正式产品按以下顺序执行：

```text
mcu_gpio_test -c edge --out /dev/gpio12 -n 3 -v
mcu_timer_test -c 001 -t 100000 -n 5 -e 5 -v
mcu_timer_test -c 002 -t 500000 -a 39 -b 79 -v
mcu_timer_test -c 005
oneshot -d 100000 /dev/oneshot
mcu_wdt_test -c 002 -t 1000 -p 3000 -i 500 -v
mcu_wdt_test -c 003 -t 1000
echo ST014_FINAL_ALIVE
```

关键数据如下：

```text
[GPIO-edge] PASS rejected invalid operations and recovered for 3 cycles

[TIMER-001] overflow period accuracy timeout=100000us rounds=5 tol=5.00%
  round 1 interval=99084.0us err=-916.0us
  round 2 interval=100072.0us err=+72.0us
  round 3 interval=100009.0us err=+9.0us
  round 4 interval=99989.0us err=-11.0us
  round 5 interval=99998.0us err=-2.0us
  RESULT max_err=916.0us (0.916%) tol=5000.0us (5.00%)
  [TIMER-001] PASS accuracy within tolerance

[TIMER-002] clock prescaler effect (div 39 vs 79)
  div=39 period=0.4992s
  div=79 period=1.0000s
  ratio period_b/period_a=2.003 (expected 2.000, tol +/-5%)
  [TIMER-002] PASS prescaler takes effect

[TIMER-005] PASS rejected requests preserved state; live update fired; lifecycle recovered

Starting oneshot timer with delay 100000 microseconds
Finished

[WDT-002] Periodic keepalive, no reset
  fed #6 elapsed=3026ms timeleft=1000ms
  PASS: fed 6 times over 3026ms, no reset; watchdog stopped

[WDT-003] Lifecycle and rejected requests, no reset
  PASS: invalid/live changes rejected; duplicate lifecycle preserved state

ST014_FINAL_ALIVE
```

全部命令返回 NSH，未出现 FPU mismatch、assert、panic 或意外复位。

## 制品与资源开销

eager 和正式 lazy 固件使用相同 vendor 配置，只切换 `ARCH_LAZYFPU`；两者均关闭
专项测试 app。

| 制品或资源 | Eager | 正式 Lazy | Lazy - Eager |
|---|---:|---:|---:|
| `final_nuttx` | 833,792 B | 833,792 B | 0 B |
| text | 440,028 B | 440,116 B | +88 B |
| data | 15,040 B | 15,040 B | 0 B |
| bss | 20,092 B | 20,364 B | +272 B |
| linker raw heap | 269,696 B | 269,424 B | -272 B |
| `g_idletcb` | 268 B | 532 B | +264 B |
| exception frame | 264 B | 132 B | -132 B |

signals 开启时，每个 TCB 增加两份 132 B 状态：运行态 `fregs` 和 signal 快照
`saved_fregs`，合计 264 B。静态 IDLE TCB 因此增长 264 B；最终 bss 和 raw heap 的
272 B 差异还包含链接对齐。动态任务的 TCB 从 heap 分配，也会分别承担 264 B；不能
只看静态 bss 判断总 RAM 代价。

当前测试固件（lazy + 两个测试 app）制品为：

```text
final_nuttx  853972 B  sha256 1f2aa2b51dd08fe90af7e485e3ddb960521e7f3a225aeedd697832b03e7fc8e4
nuttx.bin    474672 B  sha256 9cbe83abdb3b825a17c8193987a5b4217189876906ec783e06cf84ddf49ff5e9
whole image 4194304 B  sha256 b2772b7abbb52c8bfa065638b573f41ecb89562d1e00949e25aa009c9f54333c
```

测试固件的 text/data/bss 为 `453644/15384/20676 B`。USB2 分区烧录时 boot2、
双 partition 和 app 均完成设备端 SHA 校验，app 设备端 SHA 与上表一致。

最终正式产品制品为：

```text
final_nuttx  833792 B  sha256 5c4d53606ae0b8051f6625dee65dc89cb2a25067ca51cf96662895c03215e2ce
nuttx.bin    460800 B  sha256 4841469c99d06200644249b4ac9b1b7f5f3dc0e78e1bd7d71f67482d07422491
whole image 4194304 B  sha256 93f23babcb279fd5871dc7b8c3f79e45921b720910193849f1b4129f842233ba
```

最终 `nuttx.bin` 烧录时 host/device SHA 均为
`4841469c99d06200644249b4ac9b1b7f5f3dc0e78e1bd7d71f67482d07422491`，
FlashCube 输出 `Verification succeeded`。随后 2 Mbps 启动和本节外设回归均通过。

## 限制与判定边界

- 正式配置依赖 NuttX PR `#356` 的 user context 和 signal 修复；未合入该修复的
  NuttX 不能安全启用本配置。
- 本次验证对象是 RV32 单精度 FPU、flat build、单核 M-mode 和 signals enabled；
  不能外推到 RV64、双精度、vector、protected/kernel build 或 SMP。
- 两线程用 `fs0-fs11` 验证 ABI callee-saved 状态；异步 signal 路径单独验证全部
  32 个 FPR 和 FCSR。两者合起来覆盖当前修复边界，但不是任意 signal 嵌套测试。
- 当前 RISC-V signal 实现只允许一层 active signal trampoline；signal handler
  执行期间收到的其他 signal 仍受既有单层模型限制。
- 测试用 `pthread_kill()` 投递软件 signal，并用 MTIMER tick 证明运行窗口内有硬件
  中断；没有在硬件 ISR 内执行浮点。OpenVela 的前提仍是 kernel/ISR 不使用 FPU。
- 本项没有 cycle/HPM 数据，只证明 frame、状态隔离和功能回归。若需要量化时延，
  应在 A08 硬件 perf event 接入后单独比较异常和任务切换成本。
- 测试 app 直接读写 RISC-V FPR，只用于受控验收固件，正式产品保持关闭。
