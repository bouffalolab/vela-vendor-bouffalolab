# BL616CL Syslog Coredump 配置与离线解码

本文说明 BL616CL 如何启用 OpenVela 通用 syslog coredump，并给出从受控
kernel panic、完整串口抓取到 ELF core 和 GDB 回溯的可重复流程。烧录流程由 SDK
通用工具文档统一维护，本文从配置和构建开始，运行流程从固件已经进入 NSH 开始。

## 背景

BL616CL 已具备 RISC-V TCB info、frame pointer、backtrace、寄存器转储和串口
syslog。普通 panic 日志能显示即时回溯，但串口现场不便保存和重复分析。OpenVela
coredump 可以把触发线程的寄存器、任务信息和栈封装为 ELF core，经串口输出后用
匹配的 `final_nuttx` 离线分析。

首轮目标是最小、可裁剪且可稳定抓取的单线程方案：

- 输出无压缩 HEX，不启用 base64；
- 只保存触发线程，不启用 full dump；
- 不额外导出全局 RAM、heap 或 MMIO；
- 受控测试 app 默认关闭，不改变全局 assert 策略；
- 负测停机后复位恢复，再执行 GPIO、timer、oneshot 和 WDT 回归。

## 方案与裁剪边界

正式 `nsh` 配置启用：

```text
CONFIG_COREDUMP=y
CONFIG_BOARD_COREDUMP_SYSLOG=y
CONFIG_BOARD_MEMORY_RANGE="{0x0,0x0,0x0}"
# CONFIG_BOARD_COREDUMP_FULL is not set
# CONFIG_BOARD_COREDUMP_COMPRESSION is not set
# CONFIG_BOARD_COREDUMP_BASE64STREAM is not set
# CONFIG_SYSLOG_COLOR_OUTPUT is not set
```

`CONFIG_COREDUMP` 依赖 RISC-V 已提供的 `ARCH_HAVE_TCBINFO`，并选择 ELF 支持。
`BOARD_COREDUMP_SYSLOG` 复用现有 UART0 syslog，不需要 BL616CL 私有
`board_crashdump()` hook。

零长度 `BOARD_MEMORY_RANGE` 是当前 OpenVela 版本的兼容哨兵。空配置会使
`g_regions` 为 `NULL`，而当前 `coredump_dump()` 在开始输出前仍会遍历 region；
`{0x0,0x0,0x0}` 只终止遍历，不导出地址 0 或任何额外 RAM。后续若上游允许空
region，应重新评估并移除该哨兵。

彩色 syslog 必须关闭。当前 `coredump.py` 不清理 ANSI 转义序列，开启颜色会把
控制字符插入 HEX，导致 core 无法可靠转换。这意味着正式产品的普通 syslog 也不再
带颜色；若要恢复颜色，需要先让 host 解码工具明确过滤并测试 ANSI，而不是接受受
污染的 dump。

关闭 `CONFIG_COREDUMP` 后，通用 coredump 源文件、syslog dump stream 和调用路径
可被裁掉。正式配置还保持测试命令关闭：

```text
# CONFIG_BL_OS_FEATURE_TESTS_SYSLOG_COREDUMP is not set
```

受控负测时临时启用：

```text
CONFIG_BL_OS_FEATURE_TESTS_SYSLOG_COREDUMP=y
```

测试 app 依赖 `BUILD_FLAT`、`BUILTIN`、`DEBUG_ASSERTIONS`、`COREDUMP` 和
`BOARD_COREDUMP_SYSLOG`，只提供手工 `safe|fatal` 命令，不加入启动脚本。关闭
后测试 archive、命令注册和 `syslog_coredump_test_main` 均不进入最终固件。

## 实现和触发语义

正常启动后的初始化链为：

```text
nx_bringup()
  -> board_late_initialize()
  -> coredump_initialize()
  -> syslog coredump stream ready
```

受控 fatal 链为：

```text
syslog_coredump_test fatal
  -> kthread_create("d05_coredump", ...)
  -> coredump_fatal_thread()
  -> PANIC()
  -> _assert()
  -> dump_fatal_info()
  -> dump_core_info()
  -> coredump_dump(trigger_pid)
  -> coredump_dump_syslog()
  -> coredump()
```

测试程序位于 `apps/os_feature_tests/syslog_coredump/`。`safe` 只打印并正常
返回；`fatal` 创建 kernel thread 后在该线程调用 `PANIC()`。这里不能直接在普通
builtin app 中调用 `PANIC()`：当前 `CONFIG_BOARD_RESET_ON_ASSERT=0` 下，普通
任务 assert 只终止该任务，不进入系统 panic，也不会生成 coredump。kernel thread、
IRQ 或 RISC-V exception 才进入系统 panic。

本次不修改 `BOARD_RESET_ON_ASSERT`。kernel thread 完成 coredump 后停机，不会
自动回到 NSH，必须受控复位。coredump 初始化发生在 board late initialize 之后；
在此之前的 early startup 或 board late initialize 故障不能由本方案保存。

任务态 panic 的寄存器在生成 core 期间由 `up_saveusercontext()` 保存，因此 core
顶层 PC/SP 表示 coredump 路径中的有效现场，PC 不承诺精确等于 `PANIC()` 指令。
验收要求 PC/SP 有效，且回溯包含受控触发函数。真正的 RISC-V exception 会使用
异常保存的 `running_regs()`，其原始 fault PC/SP 语义与本次 kernel panic 不同。

## 配置与构建

### 1. 正式产品构建

```sh
vendor/bouffalolab/vela clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
vendor/bouffalolab/vela build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

完成判据：

- clean build 退出码为 0，并生成 `final_nuttx`、`nuttx.bin` 和
  `flash_prog_cfg.ini`；
- `.config` 含正式配置，且测试 app 明确关闭；
- ELF 含 `coredump`、`coredump_dump` 和 `coredump_initialize`；
- ELF 不含 `syslog_coredump_test_main`，`help` 不列出测试命令。

核对命令：

```sh
grep -E 'CONFIG_(COREDUMP|BOARD_COREDUMP|BOARD_MEMORY_RANGE|SYSLOG_COLOR|BL_OS_FEATURE_TESTS_SYSLOG)' \
  cmake_out/ai-m64l-32s-kit_nsh/.config
prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf-nm \
  cmake_out/ai-m64l-32s-kit_nsh/final_nuttx | \
  grep -E 'coredump|syslog_coredump_test'
```

本次关闭态 clean build 为 `1207/1207`，测试 app 开启的负测 clean build 为
`1211/1211`，最终产品 clean build 为 `1209/1209`，三次均成功。

### 2. 临时负测构建

先完成一次正式 configure/build，再临时打开测试 app，通过 `savedefconfig` 让后续
clean configure 复现同一负测配置：

```sh
prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak \
  --file cmake_out/ai-m64l-32s-kit_nsh/.config \
  --enable BL_OS_FEATURE_TESTS_SYSLOG_COREDUMP
cmake --build cmake_out/ai-m64l-32s-kit_nsh -t savedefconfig
vendor/bouffalolab/vela clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
vendor/bouffalolab/vela build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

运行负测前冻结该次 `final_nuttx`、`.config`、`nuttx.raw.bin`、`nuttx.bin` 和
`flash_prog_cfg.ini`，并执行 `sha256sum`。core 不含能唯一绑定 ELF 的 build-id；
不能用负测后重新构建的 ELF 代替。

负测结束后恢复正式配置并重新 clean build：

```sh
prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak \
  --file cmake_out/ai-m64l-32s-kit_nsh/.config \
  --disable BL_OS_FEATURE_TESTS_SYSLOG_COREDUMP
cmake --build cmake_out/ai-m64l-32s-kit_nsh -t savedefconfig
vendor/bouffalolab/vela clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
vendor/bouffalolab/vela build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

## 固件运行测试

### 1. 建立单一串口会话

串口参数为 `/dev/ttyUSB2`、`2000000` baud。打开串口后立即恢复运行态
`DTR=1, RTS=0`，负测、抓取、fatal 后复位和回归全部复用同一 fd。使用交互终端时
可执行：

```sh
picocom --baud 2000000 --lower-rts \
  --logfile <raw-log> /dev/ttyUSB2
```

保持会话打开，看到 `NuttShell (NSH)` 和 `nsh>` 后继续。避免另一个终端或复位
脚本并发打开同一串口。

### 2. 验证命令边界和 safe 路径

依次执行：

```text
syslog_coredump_test
syslog_coredump_test invalid
syslog_coredump_test safe extra
syslog_coredump_test safe
echo D05_PRE_ALIVE
```

前三条实测均输出：

```text
Usage: syslog_coredump_test <safe|fatal>
```

safe 路径实测输出：

```text
SYSLOG_COREDUMP_TEST SAFE begin
SYSLOG_COREDUMP_TEST SAFE returned
D05_PRE_ALIVE
```

完成判据：全部命令返回 NSH，不出现 assert、panic 或 `Start coredump:`。

### 3. 触发受控 kernel panic

```text
syslog_coredump_test fatal
```

本次实测关键输出：

```text
SYSLOG_COREDUMP_TEST FATAL create
SYSLOG_COREDUMP_TEST FATAL created pid=9
SYSLOG_COREDUMP_TEST FATAL trigger pid=0 thread=d05_coredump
Assertion failed panic: ... syslog_coredump_test.c:48 task: d05_coredump process: Kernel
Start coredump:
7F454C46...
Finish coredump. hex formatted
```

完成判据：

- 只出现一组、且顺序正确的 start/finish marker；
- 两个 marker 之间每行均为偶数字符 HEX，无 ANSI 或非 HEX 字符；
- finish 后系统按当前 assert 策略停机，不要求出现 NSH；
- 不把普通任务终止或自动复位误判为本场景成功。

第一次抓取中曾出现一行 127 字符 HEX，因此整次抓取被判无效，没有用于解码结论。
重新抓取后原始数据为 24,731 B，所有 HEX 行长度均为偶数，且只有一组完整 marker。

### 4. 同一 fd 复位和运行回归

保持串口 fd 打开，执行固定时序：`DTR up`、等待 50 ms、`RTS up`、等待
50 ms、`RTS down`、等待 100 ms，然后等待 2 Mbps 启动输出。实测重新出现：

```text
NuttShell (NSH)
nsh>
```

随后依次执行：

```text
mcu_gpio_test -c edge --out /dev/gpio12 -n 3 -v
mcu_timer_test -c 001 -t 10000 -n 5 -e 5 -v
mcu_timer_test -c 002 -t 500000 -a 39 -b 79 -v
mcu_timer_test -c 005
oneshot -d 100000 /dev/oneshot
mcu_wdt_test -c 002 -t 1000 -p 3000 -i 500 -v
mcu_wdt_test -c 003 -t 1000
echo D05_FINAL_ALIVE
```

负测固件复位后的实测结果：

| 用例 | 关键数据 | 结果 |
|---|---|---|
| GPIO edge | 非法操作被拒绝，3 个恢复周期 | PASS |
| TIMER-001 | 10 ms、5 轮，最大误差 284 us（2.840%），门限 500 us | PASS |
| TIMER-002 | divider 39/79 周期 0.4999/0.9999 s，比例 2.000 | PASS |
| TIMER-005 | 非法请求、运行中更新和重复生命周期均保持状态 | PASS |
| oneshot | 100,000 us，输出 `Finished` | PASS |
| WDT-002 | 1,000 ms timeout，每 500 ms 喂狗，3,006 ms 内 6 次 | PASS |
| WDT-003 | 非法/live 修改和重复生命周期被拒绝 | PASS |
| 最终存活 | 输出 `D05_FINAL_ALIVE` | PASS |

## Host 转换与离线分析

### 1. 转换原始串口记录

```sh
python3 nuttx/tools/coredump.py <raw-log> -o <dump.core>
```

当前未安装 `python-lzf` 时工具会打印提示，但本方案没有启用压缩，不影响 HEX
转换。工具只提取第一段 dump，且不校验 CRC 或 ELF 完整性；退出码为 0 不能替代
后续结构检查。

本次有效原始记录 SHA256 为
`e7b66e886ca44d0d98e6d00b58e88785458a6c9d869a814d95ef48be97c16c7d`。
转换后的 core 为 6,348 B，SHA256 为
`8e7731cd59341c6366cd061287eb6fdfc98bf66b64328060edd0d67cd4d48bbf`。

### 2. 检查 ELF core 完整性

```sh
file <dump.core>
readelf -h -l -n <dump.core>
```

实测结果：

```text
ELF32, little endian, RISC-V, CORE
Number of program headers: 3
NOTE offset=0x000094 filesz=0x0001a0
LOAD offset=0x000400 filesz=0x001040
NOTE offset=0x001800 filesz=0x0000cc
Owner: d05_coredump, LWP: 9
```

最后一个 segment 结束于 `0x1800 + 0xcc = 0x18cc`，十进制为 6,348，与文件
大小完全相等，没有尾部截断或额外残片。

### 3. 使用准确负测 ELF 解码

```sh
gdb-multiarch -q <negative-final_nuttx> \
  -c <dump.core> -batch \
  -ex 'set pagination off' \
  -ex 'info threads' \
  -ex 'thread apply all info registers pc sp ra fp' \
  -ex 'thread apply all bt'
```

实测恢复结果：

```text
LWP 9
pc = 0x80006e58 <__assert+82>
sp = 0x60fcfd80
ra = 0x8003bc0e <syslog_coredump_test_main>
fp = 0x60fcfda0

#0 __assert
#1 coredump_fatal_thread
#2 nxtask_start
```

GDB 会提示 core 可能与 executable 不匹配，因为板级关闭 build-id。该提示不能仅凭
GDB 能打开而忽略；必须用烧录前冻结的 SHA256 确认 ELF、bin、配置和 flash 配置
属于同一次负测构建。本次负测 `final_nuttx` SHA256 为
`e335502ad9ef7c820c5099b94fc1db0744e327b8811a213f3a831557d4518404`，
`nuttx.bin` SHA256 为
`ae356cf5fc594e348bf8217190eccff3ff6915a38e4fe158117329e4198916f5`。

当前 `CONFIG_DEBUG_SYMBOLS` 关闭，因此可以验收函数符号栈，不承诺源码行号、局部
变量或完整类型信息。

## 最终产品回归

恢复测试 app 关闭的正式配置并 clean build 后，最终产品实测：

- `help` 不含 `syslog_coredump_test`；
- ELF 不含 `syslog_coredump_test_main`，测试 archive 不存在；
- coredump 产品符号仍存在；
- `/dev/ttyUSB2`、2 Mbps 受控复位匹配 `NuttShell (NSH)` 和 `nsh>`。

在同一串口 fd 重新执行完整外设序列，结果为：

| 用例 | 关键数据 | 结果 |
|---|---|---|
| GPIO edge | 非法操作被拒绝，3 个恢复周期 | PASS |
| TIMER-001 | 10 ms、5 轮，最大误差 280 us（2.800%），门限 500 us | PASS |
| TIMER-002 | divider 39/79 周期 0.4997/0.9999 s，比例 2.001 | PASS |
| TIMER-005 | 五类非法/重复请求均被拒绝，live update 后恢复 | PASS |
| oneshot | 100,000 us，输出 `Finished` | PASS |
| WDT-002 | 1,000 ms timeout，每 500 ms 喂狗，3,006 ms 内 6 次 | PASS |
| WDT-003 | 非法/live 修改和重复生命周期被拒绝 | PASS |
| 最终存活 | 输出 `D05_FINAL_ALIVE` | PASS |

最终产品 `final_nuttx` SHA256 为
`7e5ed08b1d0214fe9c3a32084c8c0afa2cc120bc6e18c4b43c36217677b3d754`，
`nuttx.bin` SHA256 为
`074e33b65903afb929c012d3716b9a980b6d551cd9c5b2b51fedfc14b1208fef`。
分区烧录时 boot2、双 partition 和 app 的主机/设备校验一致。

## 固件开销

关闭态和最终产品态均不包含测试 app：

| 制品 | coredump 关闭 | 正式开启 | 增量 |
|---|---:|---:|---:|
| `final_nuttx` | 738,048 B | 743,524 B | +5,476 B |
| text | 349,550 B | 354,006 B | +4,456 B |
| data | 14,292 B | 14,484 B | +192 B |
| bss | 11,128 B | 11,608 B | +480 B |
| `nuttx.bin` | 369,568 B | 374,224 B | +4,656 B |

临时加入测试 app 后，`final_nuttx` 为 747,820 B，`text/data/bss` 为
`354,894/14,484/11,608`，`nuttx.bin` 为 375,104 B；这些测试增量不属于正式
coredump 能力。

## 适用边界

- 首轮只保存触发线程，不包含其他任务、全部 heap 或全局 RAM。
- 当前 zero-length memory range 是针对现有通用实现的兼容配置，不代表地址 0
  可读，也不能替代真实 memory region 设计。
- syslog 抓取必须有唯一完整首尾、偶数长度 HEX 和无 ANSI 污染；任何丢字节、
  奇数行或缺尾都使整次 core 无效。
- `coredump.py` 转换成功不证明 core 完整，也不证明 ELF 身份匹配。
- kernel-thread panic 保存的是 coredump 期间的 PC/SP；真正异常的 fault register
  语义需要单独验证。
- `BOARD_RESET_ON_ASSERT=0` 下 fatal 完成后停机；生产系统是否自动复位属于独立
  安全与可用性策略，本次没有改变。
- syslog stream 在 board late initialize 之后才可用，不能覆盖更早的启动故障。
- full、compression、base64、真实 RAM region、KASAN 和 UBSAN 均未在本次启用或
  验证，不能从本结果外推。
