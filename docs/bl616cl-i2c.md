# BL616CL I2C0/I2C1 Master 适配与验证

本文说明 BL616CL I2C master 与 OpenVela I2C 子系统的最大能力交集、实现边界、
Kconfig 裁剪、构建检查和运行验证。当前已完成 I2C1-only 软件合同与现役外设回归；
I2C0 GPIO4/5 和外接 I2C target 资源尚未冻结，因此不把 fake 或 USB-UART 结果写成
实物 I2C 总线通过。

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
| 100/400 kHz | 纳入 | 两个频率均进入 fake 配置合同；其它频率当前拒绝 |
| polling transfer | 纳入 | LHAL 同步 FIFO 轮询，等待上限 100 ms |
| write + repeated-start + read | 纳入 | 严格二元消息；write prefix 1..16，read 1..1024 |
| 同总线原子性 | 纳入 | 一把实例 mutex 覆盖完整 OpenVela transfer |
| 双总线并发 | 纳入软件合同 | 两实例各自串行，fake 证明可同时进入；实物双总线待资源冻结 |
| 10-bit address | 后续项 | 寄存器有字段，当前没有真实 10-bit target 验证 |
| 三段以上 restart | 后续项 | LHAL 私有 restart 与公共 ABI 不同，尚未逐段证明线序 |
| `NOSTART` continuation | 后续项 | 当前 LHAL 未实现公共语义，返回 `-EOPNOTSUPP` |
| IRQ、trace、bus reset | 后续项 | 缺完整 ISR/callback、trace 和 GPIO recovery 验证闭环 |
| DMA | 排除 | 当前路径启用后直接返回，没有 channel、完成等待和 cache 合同 |
| slave、1 MHz、3.4 MHz | 排除 | LHAL 无 slave API，频率文档上限为 400 kHz |
| zero-length probe | 排除 | LHAL packet length 会发生 `length - 1` 下溢 |

## 实现和所有权

调用链为：

```text
board_late_initialize()
  -> ai_m64l_kit_i2c_initialize()
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

代码归属：

- `chips/bl616cl/bl616cl_i2c.c`：实例资源、消息翻译、mutex、错误窗口和测试 hook；
- `boards/bl616cl/ai-m64l-32s-kit/src/ai_m64l_kit_i2c.c`：pin owner 和节点注册；
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
| `AI_M64L_KIT_I2C` | board I2C 资源总开关 | 不执行 board 注册 |
| `AI_M64L_KIT_I2C0/1` | board 实例、pin 和 `/dev/i2cN` | 未选择实例不占 pin、不注册节点 |
| `BL616CL_I2C_TEST` | chip fake 注入接口 | 正式产品无测试替换入口 |
| `BL_MCU_PERIPHERAL_TESTS_I2C` | 测试 app | 无测试 archive、命令和字符串 |

板级默认关闭 I2C：

- I2C0 默认 GPIO4=SCL、GPIO5=SDA。当前 Ai-M64L-32S-Kit 模组上这两脚与
  32.768 kHz/DC-DC 资源相关；未确认模组变体前不能作为可用 I2C0 pin。
- I2C1 默认 GPIO12=SCL、GPIO13=SDA。开启后这两脚从 GPIO lower-half 表移除，
  不生成 `/dev/gpio12`、`/dev/gpio13`，避免双 owner。
- Flash GPIO6..11/21、USB GPIO32/33、UART0 GPIO34/35、boot GPIO36 不作为 I2C pin。
- 板上没有已确认的 I2C target 和独立上拉。实物测试需外接 3.3 V EEPROM 和
  2.2 kOhm 至 4.7 kOhm 上拉。

## 消息和错误合同

硬件访问前统一检查 `count > 0`、非空 buffer、7-bit 地址、100/400 kHz 和
1..1024 字节。非法结构返回 `-EINVAL`；合法但当前未支持的 `TEN`、`NOSTART`、
终止 `NOSTOP`、17 字节 combined prefix 和三段无 STOP 链返回 `-EOPNOTSUPP`。

`WRITE|NOSTOP -> READ` 只有在地址和频率一致时合成一次 LHAL 双消息调用。第一段
是 1..16 字节 prefix，第二段是 1..1024 字节 read。STOP 分隔的普通消息仍在同一
mutex 内分别调用 LHAL，防止其它 client 插入整组 transfer。

chip/arch 与 app 使用的 `ENOTSUP` 数值不同，本实现跨 archive 合同固定使用两侧
一致的 `EOPNOTSUPP=95`。LHAL 独立 archive 使用工具链 `ETIMEDOUT=116`，chip
adapter 在进入 OpenVela ABI 时规范化为 NuttX `ETIMEDOUT=110`；其它已验证的
OpenVela/fake errno 原样保留。LHAL 成功路径会清状态，只有失败早退可立即采样 raw
status。无 target 当前预期 `-ETIMEDOUT`；本项不把 raw bit 擅自映射成完整 NACK、
arbitration 或 frame errno 分类。

## 构建与裁剪验证

在 SDK 根目录分别执行：

```bash
python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh-i2c-no-instance
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh-i2c-no-instance -j6

# 将目标依次替换为：
# nsh-i2c-i2c0-only
# nsh-i2c-i2c1-only
# nsh-i2c-dual
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
`ai_m64l_kit_i2c.c.o`，`libbl_lhal.a` 无 `bflb_i2c.c.o`，测试 archive 不存在。
三个实例配置的 map 均把对象归入上述四个独立 archive；单实例 `nm` 只出现自身
实例私有符号。

## I2C1-only 软件实测

运行环境：Ai-M64L-32S-Kit，CH340 `1a86:7523`，`/dev/ttyUSB2`，2,000,000 baud。
I2C1-only 固件烧录时 host/device SHA256 均为
`cbed36c91f46631dbe693341bc5dfd31e4d9875908cc35788222c681a54e52fe`，读取校验
长度为 515824 B。复位后出现 `NuttShell (NSH)` 和 `nsh>`，`/dev/i2c1` 存在。

命令：

```text
mcu_i2c_test fake all --bus 1
```

完整关键结果：

```text
I2C_TEST RESULT mode=fake case=flags PASS separate_calls=2 combined_calls=1
I2C_TEST RESULT mode=fake case=boundary PASS single_lengths=1,1024 combined_prefixes=1,16 read_lengths=1,1024 bus=1
I2C_TEST fake-invalid PASS name=null-vector ret=-22 hardware_calls=0
I2C_TEST fake-invalid PASS name=zero-count ret=-22 hardware_calls=0
I2C_TEST fake-invalid PASS name=zero-frequency ret=-22 hardware_calls=0
I2C_TEST fake-invalid PASS name=unsupported-frequency ret=-22 hardware_calls=0
I2C_TEST fake-invalid PASS name=address-128 ret=-22 hardware_calls=0
I2C_TEST fake-invalid PASS name=ten-bit ret=-95 hardware_calls=0
I2C_TEST fake-invalid PASS name=nostart ret=-95 hardware_calls=0
I2C_TEST fake-invalid PASS name=unknown-flags ret=-22 hardware_calls=0
I2C_TEST fake-invalid PASS name=zero-length ret=-22 hardware_calls=0
I2C_TEST fake-invalid PASS name=oversize ret=-22 hardware_calls=0
I2C_TEST fake-invalid PASS name=null-buffer ret=-22 hardware_calls=0
I2C_TEST fake-invalid PASS name=terminal-nostop ret=-95 hardware_calls=0
I2C_TEST fake-invalid PASS name=combined-prefix-17 ret=-95 hardware_calls=0
I2C_TEST fake-invalid PASS name=combined-address-mismatch ret=-22 hardware_calls=0
I2C_TEST fake-invalid PASS name=combined-frequency-mismatch ret=-22 hardware_calls=0
I2C_TEST fake-invalid PASS name=three-phase-chain ret=-95 hardware_calls=0
I2C_TEST RESULT mode=fake case=invalid PASS rejected=16
I2C_TEST fake-errors configure-injected ret=-34 raw_status=00000040 order=configure,status,cleanup
I2C_TEST fake-errors injected ret=-5 raw_status=00000028 order=configure,transfer,status,cleanup
I2C_TEST RESULT mode=fake case=errors PASS configure_errno=-34 transfer_errno=-5 recovered=1
I2C_TEST RESULT mode=fake case=concurrent PASS threads=4 iterations=32 calls=128 max_active=1
I2C_TEST RESULT mode=fake case=all PASS failed=0
nsh>
```

该结果证明消息验证、边界、errno、失败取证顺序、恢复和单总线互斥的软件合同；
fake 不经过 GPIO、SCL/SDA 或外部 target，不能替代实物总线证据。

未连接 target 时运行真实控制器负向 probe：

```text
mcu_i2c_test hw probe --bus 1 --addr 82 --no-target
I2C_TEST RESULT mode=hw case=probe PASS bus=1 addr=0x52 freq=100000 expected=no-target ret=-110 raw_status=0000000b
nsh>
```

这证明了 OpenVela `/dev/i2c1`、chip adapter、LHAL polling 和超时 errno 规范化路径。
当前没有外接上拉和分析仪，不能把它解释为 SCL、NACK 波形或正向 target 通过。

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
3. 对确认不存在的普通地址执行 `hw probe --no-target`；必须是 `-ETIMEDOUT`，并
   记录 raw status。其它本地错误或意外 ACK 均失败。
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

- I2C1-only 的构建、裁剪、烧录、启动、fake 合同和真实无 target 超时路径已通过。
- I2C0-only 和 dual 当前不能通过运行验收。默认 GPIO4/5 在本模组上与
  32.768 kHz/DC-DC 资源冲突，启用后曾出现烧录校验成功但无启动日志；在 pin owner
  冻结前不能解释为 I2C0 功能通过或失败。
- 外接 EEPROM、上拉和逻辑分析仪尚未冻结，所有 `hw` case 均未取得实测 PASS。
- I2C1 fake 证明软件消息合同，不证明实际 SCL 频率、ACK/NACK、repeated-start
  波形、电气质量或 EEPROM 数据保持。
- 完成 ST021 验收仍需补齐 I2C1 实物总线、oneshot/RTC 回归；若要验收双实例，还需
  先为 I2C0 冻结不冲突的模组 pin 和两路 target。
