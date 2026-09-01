# BL616CL UART1 测试

## 背景

BL616CL UART1 已接入 NuttX standard serial upper-half，板级注册节点为
`/dev/ttyS1`。UART0 继续作为 `/dev/console` 和 `/dev/ttyS0`，通过开发板 USB2
连接承载 NSH；UART1 不允许成为 console，也不允许启用尚未适配的 flow control
和 DMA。

本板 UART1 固定使用 GPIO14（TX）和 GPIO15（RX）。数据测试前必须用导线短接
GPIO14 和 GPIO15。USB2 只能证明 UART0 console 与 UART1 压力隔离，不能替代
GPIO14/15 的 UART1 回环证据。未短接时，依赖回环的 case 必须报告 `PARTIAL`，
不得记为通过。

## 方案与边界

`mcu_uart_test` 是一个测试 main，通过参数选择 UART-001..012。运行时 case 仅使用
POSIX/standard serial ABI，不访问 lower-half 私有数据。构建 case 在主机侧检查
Kconfig、archive、ELF、map 和失败配置。

- UART1：非 console、FIFO RX/TX IRQ、默认 115200 8N1，RX ring 为 1024 字节。
- termios：5..8 data bits、none/odd/even parity、1/2 stop bits、合法 baud。
- 非法配置：必须返回 `-1` 和指定 `errno`，并保持旧硬件 termios 状态。
- queue ABI：`FIONWRITE` 是软件 TX ring 已排队字节数，`FIONSPACE` 是可用槽位；
  `POLLOUT` 只表示软件队列有空间，不表示硬件 FIFO 已发送完成。
- `TCSANOW` 不承诺切换时在途数据无损。影响输出格式的切换应使用 `TCSADRAIN`，
  或先执行 `tcdrain()`。
- UART-009 只观测无流控突发下的接收数量、溢出和恢复，不承诺 RX 无损。
- UART-007 通过 blocking fd 进入 last-close drain 路径；`close()==0` 本身不被
  当作硬件已排空证明，reopen 后的独立回环用于验证设备恢复。

## 配置与构建

以下命令均在 SDK 根目录执行。烧录不属于本文流程。

```text
# 关闭态：无 UART1、无 UART test
python3 vendor/bouffalolab/bl_build.py clean nsh
python3 vendor/bouffalolab/bl_build.py build nsh -j14

# 产品态：UART1 + termios，无 UART test
python3 vendor/bouffalolab/bl_build.py clean nsh-uart
python3 vendor/bouffalolab/bl_build.py build nsh-uart -j14

# 测试态：UART1 + termios + mcu_uart_test
python3 vendor/bouffalolab/bl_build.py clean nsh-uart-test
python3 vendor/bouffalolab/bl_build.py build nsh-uart-test -j14
```

`nsh-uart` 显式开启 `CONFIG_AI_M64L_KIT_UART1=y`、
`CONFIG_SERIAL_TERMIOS=y` 和 `CONFIG_UART1_RXBUFSIZE=1024`。1024 字节 RX ring
用于吸收 UART0 console 长输出调度期间的 UART1 无流控突发，不改变 UART1 TX ring
默认值。`nsh-uart-test` 另开启
`CONFIG_BL_MCU_PERIPHERAL_TESTS_UART=y`。产品配置不依赖测试 app。

## 实物准备与总流程

1. 确认测试板为 Ai-M64L-32S-Kit，运行控制台为 `/dev/ttyUSB2`、2000000 baud。
2. 确认 UART0 console 仍使用板级默认 GPIO34 TX、GPIO35 RX。
3. 用导线短接 GPIO14（UART1 TX）和 GPIO15（UART1 RX）。
4. 启动 `nsh-uart-test` 固件，在 USB2 NSH 中确认 `/dev/ttyS1` 存在。
5. 依次执行 UART-001..009；每个 case 完成后记录完整 stdout、返回码和接线状态。
6. 用 `mcu_uart_test 010 &` 在后台运行 UART-010；在同一 USB2 NSH 中持续执行
   `help`、`ls /dev` 等命令，记录命令仍可交互，再等待后台任务打印最终结果；不要把
   这些输出写成 UART1 回环证据。
7. 在主机侧执行 UART-011 的三态 clean build、archive/ELF/map 门禁。
8. 在主机侧执行 UART-012 的失败配置构建，确认构建失败且命中指定 `#error`。
9. 删除所有 `.st035-neg-*` 临时配置和对应 `cmake_out`，恢复标准产品配置。

运行时命令：

```text
mcu_uart_test 001
mcu_uart_test 002
mcu_uart_test 003
mcu_uart_test 004
mcu_uart_test 005
mcu_uart_test 006
mcu_uart_test 007
mcu_uart_test 008
mcu_uart_test 009
mcu_uart_test 010 &
mcu_uart_test all
```

UART-011/012 是主机侧构建 case；在固件中执行对应编号只会提示需要主机验证。
测试进程返回码为：`0` 表示 `PASS`，`1` 表示 `FAIL`，`2` 表示 `PARTIAL`。因此未接
GPIO14/15、UART-010 缺少同期 USB2 人工交互证据，或 `all` 尚未合并 UART-011/012
主机门禁时，自动化不得按成功处理。

## 逐项测试流程

### UART-001：注册、默认配置和 console 隔离

前置条件：测试固件已启动，不要求 GPIO14/15 短接。

1. 以 nonblock 方式打开 `/dev/ttyS1`。
2. `tcgetattr()` 读取默认 termios。
3. 断言 speed=115200、`CS8`、无 `PARENB`、无 `CSTOPB`。
4. 独立打开 `/dev/console`，确认 console 节点不等于 UART1 测试节点。

通过条件：两个节点均能打开，UART1 默认值为 115200 8N1，输出
`ttyS1 registered; console opens separately`。

### UART-002：FIFO 和 ring 边界回环

前置条件：GPIO14/15 已短接。

1. 设置 raw 115200。
2. 每轮先 `TCIOFLUSH`，再启动带超时 reader。
3. 依次发送并精确接收 1、31、32、33、255、256、257、1024 字节二进制 payload。
4. 对每一字节执行完整比较。

通过条件：8 个长度逐项输出 `PASS`。32 B 是硬件 FIFO 边界，31/33 验证边界两侧；
255/256/257 和 1024 验证软件 ring 分段推进。超时且无数据只记 `PARTIAL`。

### UART-003：termios 格式矩阵

前置条件：GPIO14/15 已短接。

1. 保存原 termios，进入 raw mode。
2. 遍历 5/6/7/8 data bits、none/even/odd parity、1/2 stop bits。
3. 在组合中轮换 9600、115200、1000000 baud。
4. 每次设置后发送一字节，并按 data-bit mask 比较回环结果。
5. 完成后恢复原 termios。

通过条件：所有 24 个格式组合设置和回环成功，原 termios 恢复成功。

### UART-004：非法 termios 原子失败

前置条件：不要求回环；以 blocking 方式打开 UART1 并保存旧 termios。

| 输入 | 预期 errno | 原因 |
| --- | --- | --- |
| baud=0 | `EINVAL` | divider 无定义 |
| baud=UART clock/2 | `EINVAL` | 必须严格小于 clock/2 |
| baud=UART clock/2+1 | `EINVAL` | 超出采样上限 |
| baud=1 | `EINVAL` | divider 超出 16-bit 编码上限 |
| `CRTS_IFLOW` | `EOPNOTSUPP` | input flow control 未适配 |
| `CCTS_OFLOW` | `EOPNOTSUPP` | output flow control 未适配 |
| `CRTSCTS` | `EOPNOTSUPP` | flow control 未适配 |

每次失败后重新 `tcgetattr()`，比较 baud、data bits、parity 和 stop bits。通过条件是
每次调用均为 `ret=-1`、errno 精确匹配，且旧硬件 termios 不变。

### UART-005：nonblock、poll 和 queue ioctl

前置条件：不要求回环。

1. 以 nonblock 打开，设置 raw 9600，执行 `TCIOFLUSH`。
2. 空队列 `read()` 必须返回 `-1/EAGAIN`。
3. 零超时 `poll()` 必须报告 `POLLOUT`，不得报告 `POLLIN`。
4. 空态断言 `FIONREAD=0`、`FIONWRITE=0`、`FIONSPACE>0`。
5. 写入 1024 字节制造非零 TX ring 占用。
6. 断言 `FIONWRITE>0`、`FIONSPACE` 减少，且
   `FIONWRITE + FIONSPACE == 空态 FIONSPACE`。
7. 用 `TCOFLUSH`、`tcdrain()`、`TCIFLUSH` 清理。

通过条件：空态和占用态全部断言成立；不把 `POLLOUT` 写成线速完成证明。

### UART-006：flush、drain 和恢复

前置条件：GPIO14/15 已短接。

1. `TCIOFLUSH` 后发送一字节并 `tcdrain()`。
2. 等待 `POLLIN`，用 `FIONREAD` 证明 RX 队列有数据。
3. 执行 `TCIFLUSH`，再次断言 `FIONREAD=0`。
4. 执行 `TCOFLUSH`。
5. 再完成一次 32 字节回环，证明 flush/drain 后设备仍可收发。

通过条件：所有 ioctl/termios 调用成功，队列从非空变为空，恢复回环通过。
无回环数据时记 `PARTIAL`。

### UART-007：多 fd、last-close 和 reopen

前置条件：GPIO14/15 已短接。

1. 同时打开一个 blocking fd 和一个 nonblock fd。
2. 通过 nonblock fd 完成一字节回环，证明两个 fd 共享同一设备状态。
3. 写入 1024 字节压力 payload，先关闭 nonblock fd，再用 `TCOFLUSH` 清空软件 TX
   ring，避免 upper-half 无 timeout 的 ring drain 等待污染测试进程。
4. 最后关闭 blocking fd，覆盖 upper-half last-close 路径；硬件 FIFO 检查由 upper-half
   的 4 秒 timeout 约束。
5. 重新以 nonblock 打开 UART1，完成独立的一字节回环。

通过条件：多 fd 操作、两个 close、reopen 和最终回环均成功。文档只声明进入
blocking last-close 路径，不以 `close()==0` 单独证明硬件 drain。

### UART-008：双 writer 并发完整性

前置条件：GPIO14/15 已短接。

1. 打开一个 reader 和两个 nonblock writer。
2. 两个线程经 semaphore 同时开始，分别发送 64 个 `0x31` 和 64 个 `0x32`。
3. reader 精确接收 128 字节。
4. 拒绝任何其他字节，分别统计 `0x31`/`0x32` 数量。

通过条件：两个 writer 都完成，接收数量分别为 64/64。该 case 验证 payload 完整性，
不宣称两个独立 `write()` 之间具有原子顺序。

### UART-009：RX overflow 观测和恢复

前置条件：GPIO14/15 已短接。

1. raw 115200 baud，先 flush。
2. 连续发送 4096 字节，再延迟读取以制造 FIFO/ring 压力。
3. poll/read 循环必须显式拒绝 `POLLERR`、`POLLHUP`、`POLLNVAL` 和系统调用错误。
4. 记录实际接收数量；允许少于发送数量，但不得把 poll 错误记成溢出成功。
5. flush 后执行一字节回环，验证溢出后设备恢复。

通过条件：接收数量在 1..4096，且恢复回环通过。少于 4096 记录 `overflow=observed`；
数量相等记录 `overflow=not-observed`，但 case 仍通过，不把单轮结果写成硬件保证。任何
已接收数据后的恢复失败都必须记为 `FAIL`。

### UART-010：UART0 console 隔离

前置条件：GPIO14/15 已短接，USB2 NSH 可交互。

1. UART1 在 115200 baud 下循环执行 100 轮、每轮 1024 字节回环。
2. 运行期间在 USB2 NSH 反复执行 `help`、`ls /dev` 和空行编辑。
3. 分别记录 UART1 case 输出和 USB2 命令响应。

通过条件：UART1 压力回环通过，UART0 NSH 无乱码、卡死或节点串线。USB2 证据只用于
console 隔离结论。

### UART-011：三态构建和裁剪

执行“配置与构建”中的三组 clean build，然后在 SDK 根目录检查：

```text
NM=prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf-nm
AR=prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf-ar

$NM -a cmake_out/ai-m64l-32s-kit_nsh/final_nuttx
$NM -a cmake_out/ai-m64l-32s-kit_nsh-uart/final_nuttx
$NM -a cmake_out/ai-m64l-32s-kit_nsh-uart-test/final_nuttx
$AR t cmake_out/ai-m64l-32s-kit_nsh-uart/arch/libarch.a
$AR t cmake_out/ai-m64l-32s-kit_nsh-uart/boards/libboard.a
$AR t cmake_out/ai-m64l-32s-kit_nsh-uart-test/apps/vendor/bouffalolab/apps/mcu_peripheral_tests/uart/libapps_mcu_uart_test.a
```

判定：off 无 `bl616cl_uart1_register`、`g_uart1port`、
`ai_m64l_kit_uart_initialize`、`mcu_uart_test_main`；product 含前三个 UART1
lower/board 符号、不含 test main；test 四个符号均存在。UART lower 对象位于
`libarch.a`，board 注册对象位于 `libboard.a`，测试 app 位于独立
`libapps_mcu_uart_test.a`。

### UART-012：unsupported 和 pin owner 负构建

从 `nsh-uart-test/defconfig` 复制临时配置，使用
`prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak` 改动临时 defconfig，执行 clean
build。每轮要求“构建失败，并命中指定错误”，随后用 `bl_build.py clean` 清除输出并
删除临时配置。

| 负配置 | 预期错误 |
| --- | --- |
| UART0 TX=GPIO14 | `UART1 pins must not overlap UART0 console pins` |
| UART0 TX=GPIO26 | `UART1 pins must not override UART0 console signal slots` |
| UART1 console 或 UART0 非 console | `BL616CL UART1 requires UART0 as the serial console` 或 unsupported 错误 |
| UART1 IFLOW/OFLOW/DMA | `BL616CL UART1 console, flow control, and DMA are not supported` |
| I2C1 SCL=GPIO14 | `UART1 and I2C1 pins must not overlap` |
| SPI0 CS=GPIO14 | `UART1 and SPI0 pins must not overlap` |

GPIO14/15 是 board 固定映射，因此不再提供任意 UART1 pin Kconfig；reserved pin、TX/RX
重复和 PWM GPIO22 冲突属于编译期防御合同，不能通过正式 board 配置任意改写 pin 来
伪造测试。

## 本轮实测数据

日期：2026-09-01。设备为 Ai-M64L-32S-Kit，UART0 console 为 `/dev/ttyUSB2`、
2000000 baud，UART1 使用 GPIO14 TX 与 GPIO15 RX 实际短接。源码基线 vendor commit
为 `5419ec51e0af`，UART 改动在任务 worktree 中验证。

| 配置 | clean build | final_nuttx text/data/bss | final_nuttx 字节数 | nuttx.bin 字节数 |
| --- | --- | --- | --- | --- |
| `nsh` | `1224/1224` | `467772/15744/20396` | 868452 | 489168 |
| `nsh-uart` | `1225/1225` | `471772/16000/21676` | 873436 | 493424 |
| `nsh-uart-test` | `1227/1227` | `484024/16192/21676` | 887424 | 505856 |

最终产物校验值：

| 配置 | `final_nuttx` SHA256 | `nuttx.bin` SHA256 |
| --- | --- | --- |
| `nsh` | `18347196db1b1198ec5f03ea0f3ca40e186b43385f2509495280f58cf6e8b86f` | `60a72a5e99a0499cb28f837b1ba785ac11d29ba02de4abcc7879f9c21f70fb21` |
| `nsh-uart` | `ddf1e655470ab8a3d55920d6d59280e1d924c7df42a07fe5b0d1202d6aecd9e0` | `1b8c4c496ad3ac29d6c40683a81e18619c680ede7742598e0f098e58e165c570` |
| `nsh-uart-test` | `102d70e76dee84edd63b5acca019435fa7ac96b1dce9459b226d5cdd4330db76` | `5bb5efdb124ef8eac9ba67c66466bdbe5512771464158cb2ed739a7627f90eb7` |

裁剪实测：

- `nsh`：无 UART1 lower、board 注册和 UART test 符号。
- `nsh-uart`：存在 `bl616cl_uart1_register`、`g_uart1port`、
  `ai_m64l_kit_uart_initialize`；无 `mcu_uart_test_main`。
- `nsh-uart-test`：存在上述 UART1 符号和 `mcu_uart_test_main`；生成独立
  `libapps_mcu_uart_test.a`。
- `bl616cl_serial.c.o`/`bl616cl_lowputc.c.o` 位于 `libarch.a`；
  `ai_m64l_kit_uart.c.o` 位于 `libboard.a`。

负构建实测均按预期失败并命中指定错误：UART0 物理 pin、UART0 signal slot、UART1
console、flow control、I2C1 owner、SPI0 owner。临时配置和构建目录均已清理。

运行时使用上述 `nsh-uart-test` 最终产物，烧录校验的 app SHA256 与本地
`nuttx.bin` SHA256 一致。UART-001..009 关键输出如下：

```text
[UART-001] device=/dev/ttyS1 speed=115200 cflag=0x1032 console=/dev/console
[UART-001] PASS ttyS1 registered; console opens separately

[UART-002] PASS length=1
[UART-002] PASS length=31
[UART-002] PASS length=32
[UART-002] PASS length=33
[UART-002] PASS length=255
[UART-002] PASS length=256
[UART-002] PASS length=257
[UART-002] PASS length=1024

[UART-003] PASS 5/6/7/8 bits, none/odd/even, 1/2 stop

[UART-004] PASS rejected speed=0 flags=0x0 errno=22 old-state-kept
[UART-004] PASS rejected speed=20000000 flags=0x0 errno=22 old-state-kept
[UART-004] PASS rejected speed=20000001 flags=0x0 errno=22 old-state-kept
[UART-004] PASS rejected speed=1 flags=0x0 errno=22 old-state-kept
[UART-004] PASS rejected speed=115200 flags=0x80000000 errno=95 old-state-kept
[UART-004] PASS rejected speed=115200 flags=0x20000000 errno=95 old-state-kept
[UART-004] PASS rejected speed=115200 flags=0xa0000000 errno=95 old-state-kept

[UART-005] PASS empty read=-1/EAGAIN pollout=1 rx=0 occupied-queued=222
           occupied-space=33 empty-space=255
[UART-006] PASS TCFLSH/TCDRN and post-drain transfer
[UART-007] PASS multi-fd, blocking last-close path, reopen
[UART-008] PASS payloads complete; no cross-write atomicity
[UART-009] PASS attempted=4096 received=1310 overflow=observed recovery=PASS
```

UART-010 在后台完成 100 轮、每轮 1024 字节回环；前台 USB2 在压力期间执行两轮
`help` 和两轮 `ls /dev`，均返回完整命令响应和 `nsh>`，`/dev/ttyS1` 保持存在：

```text
[UART-010] UART1 progress=10/100
[UART-010] UART1 progress=20/100
[UART-010] UART1 progress=30/100
[UART-010] UART1 progress=40/100
[UART-010] UART1 progress=50/100
[UART-010] UART1 progress=60/100
[UART-010] UART1 progress=70/100
[UART-010] UART1 progress=80/100
[UART-010] UART1 progress=90/100
[UART-010] UART1 progress=100/100
[UART-010] PARTIAL UART1 pressure PASS; USB2 needs operator evidence
```

该 `PARTIAL` 只表示测试程序不能自行判断 USB2 交互；本轮已经同时取得前台交互证据。
随后又固定同一产物重复 5 轮 UART-010，并对 25 次 `help` 的
`stackmonitor_stop`、`stackmonitor_start`、`mcu_uart_test`、`mcu_timer_test` 和
`mcu_wdt_test` 做完整 token 校验，结果为 `25/25`，UART1 压力为 `5/5`。
单独执行主机门禁占位 case `mcu_uart_test 012` 后，NSH 的 `echo $?` 返回 `1`，证明
`PARTIAL` 不再被 shell 当作成功；NSH 会归一化 app 返回值，因此该观察只证明非零，
不用于声称 shell 精确保留测试程序内部返回值 `2`。

首轮纠错过程：默认 256 字节 RX ring 时，UART-002 的 1024 字节回环曾超时，UART-010
在第 12 轮并发 `help` 时超时；flow-control 负测还暴露 lower 使用工具链 `ENOTSUP=134`
而 app 使用 NuttX `ENOTSUP=138` 的 ABI 差异。最终方案让 nonblock writer 每次有效推进
后让出调度、将产品/测试 RX ring 配为 1024 字节，并改用两侧一致的
`EOPNOTSUPP=95`。修正后重做 clean build、烧录和全部实测。最终验证中曾有一次
`help` token 少两个字符，固定同一产物追加的 5 轮/25 次完整性检查未复现；因此保留为
未归因单次观测，不作为已证实驱动缺陷或通过依据。

同一最终固件完成现役非冲突回归：

```text
[GPIO-edge] PASS rejected invalid operations and recovered for 3 cycles

[TIMER-001] RESULT max_err=493.0us (0.493%) tol=500.0us (0.50%)
[TIMER-001] PASS accuracy within tolerance
[TIMER-005] PASS rejected requests preserved state; live update fired;
            lifecycle recovered

Starting oneshot timer with delay 100000 microseconds
Finished

[WDT-003] PASS: boundaries rejected; live update and lifecycle preserved
          shared state

Mon, Jan 01 00:04:32 2018
Mon, Jan 01 00:04:34 2018
uart14_15_final_artifact_alive
nsh>
```

GPIO14/15 已由 UART1 占用，GPIO 回归使用非冲突 `/dev/gpio18`。以上回归证明 UART1
适配未破坏现役 GPIO 软件状态恢复、TIMER0、oneshot、
WDT 非复位生命周期、RTC 推进和 UART0 NSH 存活；不替代各外设的仪器证据。
