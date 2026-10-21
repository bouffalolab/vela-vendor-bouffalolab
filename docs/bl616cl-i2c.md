# BL616CL I2C0/I2C1 Master 适配与验证

本文说明 BL616CL I2C master 与 OpenVela I2C 子系统的最大能力交集、实现边界、
Kconfig 裁剪、构建检查和运行验证。当前已完成 I2C1-only 无 target 超时路径与现役外设回归，
并用逻辑分析仪确认 I2C0、I2C1 在 GPIO4/5 上的无 target 地址波形。2026-10-16 起在
BL616CL-AVE-V10 上以板载 ES8388 完成正向读写验证（见“正向 target”）；多字节读和
双总线仍缺实物 target。

## 背景

BL616CL LHAL 提供 I2C0/I2C1 两个控制器的同步 polling master，但 OpenVela 适配层
此前没有 chip lower-half、board pin owner 或 `/dev/i2cN` 节点。LHAL 位于 CI 同步的
`vendor/bouffalolab/drivers/`，本适配只通过父仓 CMake wrapper 条件编入
`bflb_i2c.c`，不修改同步驱动仓。

OpenVela `I2C_TRANSFER()` 接受消息数组并要求整组传输原子。公共 `NOSTOP`、
`NOSTART` 和 10-bit flag 与 LHAL 私有消息语义并不等价，因此 chip lower-half 必须
先验证和翻译，再访问控制器。

## 最大能力交集

| 能力 | 当前结论 | 依据和边界 |
|---|---|---|
| I2C0/I2C1 master | 纳入 | 两个实例有独立 base、IRQ、idx 和独立 mutex |
| 7-bit address | 纳入 | 地址 0x00..0x7f；实物 probe 只允许普通地址 0x08..0x77 |
| 单消息 read/write | 纳入 | 1..1024 字节；0 和 1025 在硬件前拒绝 |
| 100/400 kHz | 纳入 | 两个频率均在 AVE-V10 的 ES8388 上验证；其它频率当前拒绝 |
| polling transfer | 纳入 | LHAL 同步 FIFO 轮询，等待上限 100 ms |
| write + repeated-start + read | 纳入 | 严格二元消息；write prefix 1..16，read 1..1024 |
| 同总线原子性 | 纳入 | 一把实例 mutex 覆盖完整 OpenVela transfer |
| 双总线并发 | 纳入，未实测 | 两实例各一把 mutex，互不串行；实物双总线待资源冻结 |
| 10-bit address | 后续项 | 寄存器有字段，当前没有真实 10-bit target 验证 |
| 三段以上 restart | 后续项 | LHAL 私有 restart 与公共 ABI 不同，尚未逐段证明线序 |
| `NOSTART` continuation | 写续写纳入 | 1..16 字节写 + 带 `NOSTART` 的写，翻译成 LHAL 子地址 + 写；读续读和三段以上的链返回 `-EOPNOTSUPP` |
| IRQ、trace、bus reset | 后续项 | 缺完整 ISR/callback、trace 和 GPIO recovery 验证闭环 |
| DMA | 排除 | 当前路径启用后直接返回，没有 channel、完成等待和 cache 合同 |
| slave、1 MHz、3.4 MHz | 排除 | LHAL 无 slave API，频率文档上限为 400 kHz |
| zero-length probe | 排除 | LHAL packet length 会发生 `length - 1` 下溢 |

## 实现和所有权

调用链为：

```text
board_late_initialize()
  -> bl616cl_bringup() -> bl616cl_board_initialize()
  -> ai_m64p_kit_i2c_initialize() 或 bl616cl_ave_v10_i2c_initialize()
  -> bl616cl_i2cbus_initialize(bus, scl, sda)
  -> i2c_register(bus, lower)
  -> /dev/i2c0 或 /dev/i2c1

I2CIOC_TRANSFER
  -> OpenVela i2c upper-half
  -> bl616cl_i2c_transfer()
  -> 参数和消息组验证
  -> 实例 mutex
  -> LHAL configure + transfer
  -> 失败时立即采样 raw status
  -> cleanup + unlock
```

某条总线初始化或注册失败时，板级代码记一条 `ERROR` 日志，其它总线和后面的外设照常
初始化。

代码归属：

- `chips/bl616cl/bl616cl_i2c.c`：实例资源、消息翻译、mutex 和错误窗口；
- `boards/bl616cl/ai-m64p-32s-kit/src/ai_m64p_kit_i2c.c`：pin owner 和节点注册；
- `boards/bl616cl/bl616cl-ave-v10/src/bl616cl_ave_v10_i2c.c`：AVE-V10 的 I2C0 节点注册。
  ES8388 驱动（`bl616cl_ave_v10_audio.c`）再调用一次 `bl616cl_i2cbus_initialize(0, ...)`，
  拿到同一个实例，实例按引用计数共享，两边的传输由同一把实例 mutex 串行；
- `cmake/bl616cl_lhal.cmake`：仅在实例开启时编入同步仓的 `bflb_i2c.c`；
- `apps/mcu_peripheral_tests/i2c/`：独立 `libapps_mcu_i2c_test.a` 和一个测试 main；
- `drivers/`：保持未修改。

适配对象进入 `libarch.a`，board 注册进入 `libboard.a`，LHAL 对象进入
`libbl_lhal.a`，测试命令进入独立 `libapps_mcu_i2c_test.a`。chip 代码不会放入
`libbl_std.a`。

## Kconfig 与资源裁剪

主要选项如下：

| 选项 | 作用 | 关闭效果 |
|---|---|---|
| `BL616CL_I2C` | chip 总开关，选择 OpenVela I2C polling master | 无实例时 chip/LHAL 对象不进入 archive |
| `BL616CL_I2C0/1` | 控制器实例 | 只生成对应实例私有数据和符号 |
| `AI_M64P_KIT_I2C` | board I2C 资源总开关 | 不执行 board 注册 |
| `AI_M64P_KIT_I2C0/1` | board 实例、pin 和 `/dev/i2cN` | 未选择实例不占 pin、不注册节点 |
| `BL616CL_AVE_V10_I2C0` | AVE-V10 的 I2C0（固定 GPIO20/11）和 `/dev/i2c0` | 不注册节点 |
| `BL_MCU_PERIPHERAL_TESTS_I2C` | 测试 app | 无测试 archive、命令和字符串 |

Ai-M64P-32S-Kit 板级默认关闭 I2C：

- I2C0 默认 GPIO4=SCL、GPIO5=SDA。数据手册引脚表中 GPIO4/5 的 function 5 就是
  I2C0 SCL/SDA，没有 32.768 kHz 晶振或 DC-DC 功能。早先在另一块模组上记录过这两脚
  冲突、启用后无启动日志；在 Ai-M64P-32S-Kit 上未复现，启用 I2C0 后正常启动，
  波形见“逻辑分析仪实测”。
- I2C1 默认 GPIO12=SCL、GPIO13=SDA。开启后这两脚从 GPIO lower-half 表移除，
  不生成 `/dev/gpio12`、`/dev/gpio13`，避免双 owner。
- USB GPIO32/33、UART0 GPIO34/35、boot GPIO36 不作为 I2C pin。
- 板上没有已确认的 I2C target 和独立上拉。实物测试需外接 3.3 V EEPROM 和
  2.2 kOhm 至 4.7 kOhm 上拉。

BL616CL-AVE-V10 只有 I2C0，硬连到 GPIO20（SCL）、GPIO11（SDA），板上 4.7 kOhm 上拉到
3.3 V，挂 ES8388 codec（地址 0x10）和摄像头排线的 SCCB。

## 消息和错误合同

硬件访问前统一检查 `count > 0`、非空 buffer、7-bit 地址、100/400 kHz 和
1..1024 字节。非法结构返回 `-EINVAL`；合法但当前未支持的 `TEN`、单独或接在读后面的
`NOSTART`、终止 `NOSTOP`、17 字节 prefix 和三段无 STOP 链返回 `-EOPNOTSUPP`。

两种双消息在地址和频率一致时合成一次 LHAL 调用，第一段都是 1..16 字节写（prefix），
作为 LHAL 的 `NOSTOP` 子地址：

- `WRITE|NOSTOP -> READ`：第二段是 1..1024 字节读，线上为 prefix 后 repeated start 再读。
- `WRITE[|NOSTOP] -> WRITE|NOSTART`：第二段是 1..1024 字节写，线上为一次 START、
  地址+W、prefix、数据、STOP，和把两段拼成一条写消息相同。NuttX `i2c set` 写寄存器
  就是这种消息（寄存器号带 `NOSTOP`，数值带 `NOSTART`）。原生 SDK 在 BL616CL 上用
  同样的 LHAL 子地址 + 写来写 codec 寄存器。

`WRITE|NOSTOP -> WRITE`（不带 `NOSTART`）在公共语义里要 repeated start，LHAL 子地址
做不到，仍返回 `-EOPNOTSUPP`。STOP 分隔的普通消息仍在同一 mutex 内分别调用 LHAL，
防止其它 client 插入整组 transfer。

chip/arch 与 app 使用的 `ENOTSUP` 数值不同，本实现跨 archive 合同固定使用两侧
一致的 `EOPNOTSUPP=95`。LHAL 独立 archive 使用工具链 `ETIMEDOUT=116`，chip
adapter 在进入 OpenVela ABI 时规范化为 NuttX `ETIMEDOUT=110`；其它错误码原样保留。LHAL 成功路径会清状态，只有失败早退可立即采样 raw
status。无 target 当前预期 `-ETIMEDOUT`；本项不把 raw bit 擅自映射成完整 NACK、
arbitration 或 frame errno 分类。

## 构建与裁剪验证

以下矩阵是历史裁剪证据；当前复测使用 `nsh-peripherals`，不再从已删除的
`nsh-i2c` 派生。

在 SDK 根目录分别执行：

```bash
vendor/bouffalolab/vela clean \
  bl616cl/ai-m64p-32s-kit/configs/nsh
vendor/bouffalolab/vela build \
  bl616cl/ai-m64p-32s-kit/configs/nsh -j6

# 将目标依次替换为：
# nsh
# nsh-peripherals
# nsh
```

2026-08-30 fresh clean build 数据：

三个启用 I2C 测试 app 的配置使用 `CONFIG_NSH_LINELEN=256`，以完整输入 EEPROM
测试命令；no-instance 保持默认 64。该设置只扩大 NSH 命令行缓冲，不改变 I2C
lower-half 的能力或传输长度边界。

| 配置 | 构建 | chip | board | LHAL | test app | 实例符号 | `nuttx.bin` |
|---|---:|---:|---:|---:|---:|---|---:|
| no-instance | 1228/1228 | 0 | 0 | 0 | 0 | 无 | 479648 B |
| I2C0-only | 1234/1234 | 1 | 1 | 1 | 1 | 仅 `g_bl616cl_i2c0` | 515840 B |
| I2C1-only | 1234/1234 | 1 | 1 | 1 | 1 | 仅 `g_bl616cl_i2c1` | 515824 B |
| dual | 1234/1234 | 1 | 1 | 1 | 1 | `g_bl616cl_i2c0/1` | 516400 B |

对应 `nuttx.bin` SHA256：

```text
no-instance c0c40873d14c6da68f51363fbabc4273a3cfd494982ee5da1178b8c99c4f5911
I2C0-only  d006ef702fca6c9a9fdc1fbc68601b83236ac294b85a8cf2a5f6c8fbdc779ab6
I2C1-only  cbed36c91f46631dbe693341bc5dfd31e4d9875908cc35788222c681a54e52fe
dual       3bb8c27210cebf56e02ab1053e45b01d42e0b19f6b00efbf35f452069b72b7bb
```

no-instance 的 `libarch.a` 无 `bl616cl_i2c.c.o`，`libboard.a` 无
`ai_m64p_kit_i2c.c.o`，`libbl_lhal.a` 无 `bflb_i2c.c.o`，测试 archive 不存在。
三个实例配置的 map 均把对象归入上述四个独立 archive；单实例 `nm` 只出现自身
实例私有符号。

## I2C1-only 无 target 实测

运行环境：Ai-M64P-32S-Kit，CH340 `1a86:7523`，`/dev/ttyUSB2`，2,000,000 baud。
I2C1-only 固件烧录时 host/device SHA256 均为
`cbed36c91f46631dbe693341bc5dfd31e4d9875908cc35788222c681a54e52fe`，读取校验
长度为 515824 B。复位后出现 `NuttShell (NSH)` 和 `nsh>`，`/dev/i2c1` 存在。

在未连接 target 的 GPIO12/13 上运行真实控制器负向 probe：

```text
mcu_i2c_test hw probe --bus 1 --addr 82 --no-target
I2C_TEST RESULT mode=hw case=probe PASS bus=1 addr=0x52 freq=100000 expected=no-target ret=-110
nsh>
```

这证明了 OpenVela `/dev/i2c1`、chip adapter、LHAL polling 和超时 errno 规范化路径。
该记录当时没有分析仪，波形见下节；它不能解释为正向 target 通过。

### 逻辑分析仪实测

2026-10-13，Ai-M64P-32S-Kit，DSLogic U3Pro32（10 MHz 采样、阈值 1.6 V）接 GPIO4/5，
不接 target，也不接外部上拉，只有芯片内部上拉。分别在 `nsh-peripherals`（I2C1 在
GPIO4/5）和临时配置（I2C0 在 GPIO4/5、关闭 I2C1）上执行：

```text
mcu_i2c_test hw probe --bus 1 --addr 0x52 --freq 100000 --no-target
mcu_i2c_test hw probe --bus 1 --addr 0x52 --freq 400000 --no-target
mcu_i2c_test hw probe --bus 0 --addr 0x52 --freq 100000 --no-target
```

三条命令都返回 `PASS ... ret=-110`，sigrok I2C 解码均为
Start、Address write 0x52、NACK、Stop。临时配置启动到 `nsh>`，`/dev/i2c0` 存在。

| 实例 | 请求 | SCL 实测 |
|---|---|---|
| I2C1 | 100 kHz | 92.6 kHz |
| I2C1 | 400 kHz | 303.0 kHz |
| I2C0 | 100 kHz | 92.6 kHz |

SCL 低于请求值，不超过请求值。当时只有内部上拉，接外部 2.2 kOhm 至 4.7 kOhm 上拉后的
频率未测，原因未验证。

### 正向 target：BL616CL-AVE-V10 上的 ES8388

2026-10-16，BL616CL-AVE-V10，`nsh-peripherals`，I2C0 接板载 ES8388（地址 0x10）。

| 命令 | 结果 |
|---|---|
| `mcu_i2c_test hw probe --bus 0 --addr 0x10 --freq 100000`、`--freq 400000` | 均 PASS（ACK） |
| `mcu_i2c_test hw probe --bus 0 --addr 0x52 --freq 100000 --no-target` | PASS，`ret=-110` |
| `i2c dev -b 0 0x08 0x77` | 只有 0x10 应答 |
| `i2c get -b 0 -a 10 -r <reg>`，reg 0x00–0x34 | 53 个寄存器全部等于 NuttX `es8388.h` 的复位默认值 |
| `mcu_i2c_test hw combined --bus 0 --addr 0x10 --offset 0x00 --addr-width 1 --length 1 --freq 400000` | PASS，读到 0x06 |
| `mcu_i2c_test hw eeprom --addr 16 --offset 26 --addr-width 1 --capacity 53 --length 1 --page-size 1 --allow-write`，另加 `--freq 400000` 再跑一次 | 均 PASS，写入、读回、恢复都已校验 |
| 同上，`--offset 16` | PASS |
| `mcu_i2c_test hw concurrent --bus 0 --addr 0x10 --offset 0 --length 1 --iterations 32 --freq 400000` | PASS，128 次组合读一致 |

`hw eeprom` 在这里只是借用它的“备份、单消息写、读回、恢复”流程；`--length 1
--page-size 1` 保证每次只写一个寄存器。上面的 0x1A（LDACVOL）、0x10（LADCVOL）写入
值分别是 0x40、0x4A，测试后恢复为默认 0xC0。

这组结果证明了单字节读、写地址后 repeated-start 读、单消息写和同一总线的并发互斥，
在 100/400 kHz 下对真实从机都正确。没有覆盖的部分：

- 多字节读：ES8388 读操作不自动递增寄存器地址（`i2c dump` 只有首字节有效），不能用它
  验证多字节读和 `hw boundary`，仍需要 EEPROM。
- 波形：这次没有接逻辑分析仪，接 4.7 kOhm 外部上拉时的 SCL 实际频率和 ACK 时序未测。
- `hw dual`：AVE-V10 只有 I2C0。

同日驱动支持 `NOSTART` 续写后，在同一板上补测：

| 命令 | 结果 |
|---|---|
| `i2c set -b 0 -a 10 -r 1a c4`，再 `i2c get` | 读回 0xC4；写回 0xC0 后读回 0xC0 |
| 同上加 `-f 400000`，写 0xC8 | 读回 0xC8；写回 0xC0 后读回 0xC0 |
| `mcu_i2c_test hw eeprom ... --allow-write --split-write`，0x1A 在 100/400 kHz、0x10 在 100 kHz | 均 PASS，备份、读回、恢复已校验 |
| `i2c set -s -b 0 -a 10 -r 1a c4` | 按预期失败，0x1A 仍为 0xC0 |

`i2c set -s` 把寄存器号和数值分成两次调用，第一次以 `NOSTOP` 结束，要求总线跨调用
保持占用，本驱动不支持。i2ctool 的 `-s/-n` 等选项会保留到之后的命令，用过 `-s`
后 `i2c get` 也会失败，要加一次 `-n` 恢复。只验证了 1 字节 prefix；更长的 prefix
写续写需要 EEPROM。

## 现役外设回归

I2C1 使用 GPIO12/13 后，GPIO 回归改用仍存在的 `/dev/gpio19`，不复用已经被移除的
`/dev/gpio12`。同一 I2C1-only 固件实测：

```text
mcu_gpio_test -c edge --out /dev/gpio19 -n 16 -r
[GPIO-edge] PASS rejected invalid operations and recovered for 16 cycles

mcu_timer_test -c 001 -t 100000 -n 5 -e 0.5 -v
RESULT max_err=360.0us (0.360%) tol=500.0us (0.50%)
[TIMER-001] PASS accuracy within tolerance

mcu_timer_test -c 002 -t 500000 -a 39 -b 79 -v
div=39 period=0.4998s
div=79 period=0.9999s
ratio period_b/period_a=2.001 (expected 2.000, tol +/-5%)
[TIMER-002] PASS prescaler takes effect

mcu_timer_test -c 005
[TIMER-005] PASS rejected requests preserved state; live update fired; lifecycle recovered

mcu_wdt_test -c 002 -t 3000 -p 9000 -i 1000 -v
fed #9 elapsed=9017ms timeleft=3000ms
PASS: fed 9 times over 9017ms, no reset; watchdog stopped

mcu_wdt_test -c 003 -t 3000
PASS: invalid/live changes rejected; duplicate lifecycle preserved state

oneshot -d 100000 /dev/oneshot
Starting oneshot timer with delay 100000 microseconds
Finished

ls /dev/rtc0
 /dev/rtc0
date
Mon, Jan 01 00:00:02 2018

echo ST021_I2C1_REGRESSION_ALIVE
ST021_I2C1_REGRESSION_ALIVE
nsh>
```

回归期间没有意外 assert、panic 或 watchdog reset。GPIO-010 依赖 gpio8，当前
I2C1-only 临时配置没有该节点，因此该 case 不计为通过。RTC 本轮只验证节点和读时，
没有启用 `mcu_rtc_test` 重跑 Alarm 全集；最终实物验收仍需执行完整 RTC 回归。

## 实物总线完整流程

开始前记录模组变体、总线、SCL/SDA、target 型号/容量/地址、供电、上拉阻值和逻辑
分析仪通道。建议 I2C1 使用 AT24C32、0x50、4096 B、GPIO12/13 和 4.7 kOhm 上拉。

1. `ls /dev/i2c1`，确认节点；检查 gpio12/13 节点不存在。
2. 分别以 100 kHz、400 kHz 执行 `hw probe`；分析仪记录实际 SCL 和 ACK。
3. 对确认不存在的普通地址执行 `hw probe --no-target`；必须是 `-ETIMEDOUT`。
   其它本地错误或意外 ACK 均失败。
4. 执行 `hw combined`；分析仪必须看到一次 START、地址+W、prefix、
   repeated-start、地址+R、数据和 STOP，中间不得出现 STOP。
5. 执行 `hw boundary --length 1024`，逐项覆盖 1、4、31、32、33、255、256、1024。
6. 明确 EEPROM `--capacity` 后执行 `hw eeprom --allow-write`；依次保存 backup、
   分页写 pattern、整段读回、恢复 backup、再次读回。pattern 和 restore 都验证成功
   才 PASS。
7. 执行 `hw concurrent`，四线程每线程 32 次 combined read，所有数据必须与参考一致。
8. 只有两条总线和两个 target 都冻结后执行 `hw dual`，并记录两路同时活动的波形。
9. 重跑 GPIO、timer、oneshot、WDT、RTC，最后确认 `nsh>` 存活。

逐 case 参数和内部动作见 `apps/mcu_peripheral_tests/i2c/README.md`。该 README 直接
保存命令、流程、判据和运行关键数据，不依赖外部任务日志。

## 当前限制和验收状态

- I2C1-only 的构建、裁剪、烧录、启动和真实无 target 超时路径已通过。
- I2C0-only 在 GPIO4/5 上已确认启动和无 target 地址波形。正向 target 已在 AVE-V10
  的 ES8388 上验证（见“正向 target”），M64P 上仍缺。
- 2026-10-14 起 `nsh-peripherals` 同时注册 I2C0（SCL 30、SDA 31）和 I2C1（GPIO4/5）：
  两条总线的 `hw probe --no-target`（`ret=-110`）都 PASS；
  `hw dual` 仍缺两路 target，GPIO30/31 上也还没有逻辑分析仪波形。
- M64P 的外接 EEPROM 和上拉尚未冻结，除无 target probe 外的 `hw` case 在 M64P 上均未
  取得实测 PASS。AVE-V10 上 `hw probe`、`hw combined`、单字节 `hw eeprom` 和
  `hw concurrent` 已对 ES8388 PASS；多字节读和 `hw boundary` 仍需 EEPROM。
- 串口 PASS 只证明协议调用成功，不证明实际 SCL 频率、ACK/NACK、repeated-start
  波形、电气质量或 EEPROM 数据保持。
- 完成 I2C 验收仍需补齐 I2C1 实物总线、oneshot/RTC 回归；若要验收双实例，还需
  在 GPIO4/5 和 GPIO30/31 上各接一路 target。
