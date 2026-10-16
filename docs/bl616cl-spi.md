# BL616CL SPI0/SPI1 Master 适配与验证

本文说明 BL616CL SPI master 与 OpenVela SPI 子系统的最大能力交集、实现归属、
Kconfig 裁剪、构建和实物验收限制。当前已完成 SPI0/SPI1 lower-half，以及 SPI0 实物
环回和 CS/CLK 波形；SPI1 pin 和板级注册仍待实物资源冻结，因此能力声明限定在已有
证据范围内。

## 背景

BL616CL LHAL 有 SPI0/SPI1 两个控制器，实现同步 polling exchange，并暴露 mode、
data width、bit/byte order、FIFO、部分 IRQ/DMA 控制。此前 OpenVela 没有 BL616CL
SPI lower-half、board pin/CS owner 或 `/dev/spiN` 注册。

本适配不修改 CI 同步的 `vendor/bouffalolab/drivers/`。chip adapter 翻译 OpenVela
SPI ABI，board 代码只接管从同源 BL616CLDK 选择的 SPI0 候选 pin 和 GPIO CS，
父仓 CMake wrapper 按实例选择 LHAL `bflb_spi.c`。该 pinmux 已在 Ai-M64P-32S-Kit
上完成环回和波形验证；SPI1 完整四线 pin 未经当前板卡证实，故只开放 lower-half，
不创建虚假的 board 节点。

## 最大能力交集

| 能力 | 当前结论 | 依据和边界 |
|---|---|---|
| SPI0/SPI1 master | 纳入 | 独立 base、clock/reset、device、私有状态和 mutex |
| polling | 纳入 | LHAL `bflb_spi_poll_exchange()` 是当前完整同步路径 |
| mode 0..3 | 纳入 | OpenVela 与 LHAL 语义可直接映射；实物环回和波形覆盖 |
| 8/16-bit | 纳入 | 公共 `nwords` 与 LHAL byte count 可无歧义换算 |
| 实际频率 | 纳入 | 基于 peripheral clock/divider 向下取整，返回不高于请求值 |
| MSB/LSB | 纳入 | `SPI_HWFEATURES(HWFEAT_LSBFIRST)`；独立 Kconfig 裁剪 |
| TX/RX/full-duplex | 纳入 | 空 buffer 由 LHAL polling 路径处理；full-duplex、RX-only 有实物环回 |
| GPIO CS | 纳入 SPI0 | 默认低有效，可选高有效，CS inactive latch 先于 output enable |
| 多 target | 纳入 SPI0 | target index 与 GPIO 独立；未知 target 抑制 exchange |
| sequence CS | 纳入 | 支持 transfer 间保持或释放 CS；分析仪已确认保持和释放 |
| 同总线互斥 | 纳入 | 一把实例 mutex 保护 consumer 的完整配置和 exchange 序列 |
| 双实例并发 | 纳入设计，未验证 | 独立 mutex；SPI1 无板级节点，实物双实例待 SPI1 pin |
| 24/32-bit | 排除首版 | OpenVela buffer word 步长与 LHAL 3/4-byte word 不一致 |
| byte order | 排除首版 | 公共 ABI 无独立入口；当前固定为 LHAL `SPI_BYTE_LSB` |
| IRQ | 后续项 | 有 IRQ/FIFO 原语，无完整 ISR、完成和错误恢复合同 |
| DMA/trigger | 后续项 | 缺 channel、cache、完成、中止和实物验证闭环 |
| delay/cmd-data | 后续项 | 公共可选 ABI 当前明确返回 `-ENOSYS` |
| SPI slave | 排除 | 本任务和当前 OpenVela lower-half 只适配 master |

完整枚举不等于一次性打开全部能力。当前实现选择可裁剪且有明确软件/实物判据的
polling 子集；IRQ/DMA 等保留为独立后续项，不把“尚未适配”误写为硬件不支持。

## 调用链与代码归属

```text
board_late_initialize()
  -> bl616cl_board_initialize()
  -> ai_m64p_kit_spi_initialize()
  -> pin owner + GPIO CS inactive level
  -> bl616cl_spi_configure_pins()
  -> bl616cl_spibus_initialize(0, board_ops, board_arg)
  -> spi_register(spi, 0)
  -> /dev/spi0

SPIIOC_TRANSFER
  -> OpenVela spi upper-half
  -> lock + mode/bits/frequency/hwfeatures
  -> board select(devid, true)
  -> bl616cl_spi_exchange()
  -> bflb_spi_poll_exchange()
  -> board select(devid, false) + unlock
```

代码和 archive 归属：

- `chips/bl616cl/bl616cl_spi.c`：实例、clock、配置、exchange、mutex 和 recovery，
  编入 `libarch.a`；
- `boards/bl616cl/ai-m64p-32s-kit/src/ai_m64p_kit_spi.c`：SPI0 pin、target、GPIO CS
  和 `/dev/spi0` 注册，编入 `libboard.a`；
- `cmake/bl616cl_lhal.cmake`：有任一实例时编入同步仓 `bflb_spi.c`，生成
  `libbl_lhal.a`；
- `apps/mcu_peripheral_tests/spi/`：单一测试 main，独立生成
  `libapps_mcu_spi_test.a`；
- `drivers/`：保持未修改；chip 代码不放入 `libbl_std.a`。

## Kconfig 与资源所有权

| 选项 | 作用 | 关闭效果 |
|---|---|---|
| `BL616CL_SPI` | chip 总开关，选择 OpenVela SPI/exchange/bit-order 能力 | 无实例时不编 chip/LHAL SPI 对象 |
| `BL616CL_SPI0/1` | 独立 controller 实例 | 只生成所选实例私有状态 |
| `AI_M64P_KIT_SPI` | board SPI 总开关 | 不接管 pin、不注册节点 |
| `AI_M64P_KIT_SPI0` | SPI0 pin、target、GPIO CS 和 `/dev/spi0` | 关闭后无 board SPI 对象/节点 |
| `AI_M64P_KIT_SPI0_TARGET1` | 第二个 GPIO CS target | 关闭后无 target 1 数据和 pin owner |
| `BL_MCU_PERIPHERAL_TESTS_SPI` | 测试命令 | 无测试 archive、命令和字符串 |

SPI0 默认 GPIO12=CS、GPIO13=CLK、GPIO18=MISO、GPIO19=MOSI。编译期检查
signal 编号、pin 唯一性、保留 pin、第二 CS，以及与 I2C、UART1、PWM 的 pin
重叠。按 BL616CL PIN_MUX 表，每 12 个 GPIO 一组的 signal swap 会把 SS/SCLK 与
两个数据 signal 对调，而不是只对调 MISO/MOSI；因此 pinmux 配置把该端口所有分组的
swap 清零，只用端口级 MOSI/MISO swap 选择数据方向：MISO 在 signal 2 时不交换，
在 signal 3 时交换。MISO/MOSI 两脚必须分别落在 signal 2 和 3。默认 pin 的寄存器
取值与原生 SDK 一致，逻辑分析仪确认 MOSI 数据从 GPIO19 输出、GPIO18 为输入。
交换组合（MISO=GPIO19、MOSI=GPIO18）用临时配置实测：不短接时数据从 GPIO18 输出、
GPIO19 无翻转；短接后 `hw loopback`（mode 0 MSB 1 MHz、mode 3 LSB 400 kHz）和
`hw paths` PASS。

SPI1 只有 chip 开关，没有 Ai-M64P board 开关。这保证未知 pin 不会被软件占用。

## 频率、错误与恢复合同

请求频率通过 peripheral clock 和 8-bit divider 计算，actual frequency 向下取整并由
`SPI_SETFREQUENCY()` 返回。0 Hz 或低于 divider 可实现下限时返回 0。配置失败会设置
config error，后续 exchange 被抑制，直到同一配置项成功写入；这样不会用一笔无效配置
继续访问硬件。

LHAL/newlib 的 `ETIMEDOUT=116`，OpenVela app 的 `ETIMEDOUT=110`。chip adapter 在
跨 archive 边界时将 `-116` 规范化为 `-110`，其它 errno 原样保留。exchange 错误后
当前 recovery 清 TX/RX FIFO、deinit/init 并恢复最后有效配置。

OpenVela SPI `exchange` 回调返回 `void`，因此 ioctl 可能在 timeout、未知 target 或
配置失败时返回 OK。exchange 失败时 chip adapter 用 `spierr` 记录错误；测试 app 以
环回数据比对判定结果，不把 upper-half 返回值当成 lower-half 成功。普通 consumer
当前没有同步错误传播合同，这是独立上游改进项。

recovery 没有经过实物 BUSY、INTSTS、FIFO 和 CS 故障场景验证，不能宣称完整
controller recovery 已通过。

## 构建

在 SDK 根目录用 `nsh-peripherals` 构建：

```bash
vendor/bouffalolab/vela clean \
  bl616cl/ai-m64p-32s-kit/configs/nsh-peripherals
vendor/bouffalolab/vela build \
  bl616cl/ai-m64p-32s-kit/configs/nsh-peripherals -j14
```

对象归属和最终 ELF 的裁剪检查命令见 `apps/mcu_peripheral_tests/spi/README.md`。

## 其它外设回归

2026-08-30 在 SPI 测试固件（`nuttx.bin` SHA256
`e252b9dfa4782c221166af959073c60c145f2121395421c1ccc499d732e3b48d`）上，
timer/WDT/oneshot/RTC 回归通过：timer 001 最大误差 346 us
（0.346%，容限 0.50%），timer 002 周期比例 2.001，timer 005 生命周期和拒绝路径
PASS；WDT 连续喂狗 9 次、9017 ms 无复位，WDT 非法/重复生命周期 PASS；oneshot
100000 us 完成；`/dev/rtc0` 存在且 `date` 可读。期间没有 assert、panic 或 watchdog
reset。

## 实物验收流程

SPI0 需要断电短接 GPIO19(MOSI) 与 GPIO18(MISO)，逻辑分析仪连接 GPIO13(CLK)、
GPIO12(CS0)、MOSI、MISO 和 GND。除烧录外，完整流程如下：

1. 记录板卡/模组、固件 SHA256、pin、短接和分析仪设置。
2. 启动后确认 `/dev/spi0` 存在、对应 GPIO 节点被移除、无 SPI 初始化错误。
3. 执行最小 mode 0、8-bit、100 kHz、32-word loopback。
4. 覆盖 mode 0..3、8/16-bit、100 kHz/400 kHz/1 MHz、MSB/LSB；数据必须逐字
   一致，分析仪确认实际 frequency、CPOL/CPHA 和 bit order。
5. 执行 full-duplex、TX-only、RX-only 环回；TX-only 没有回读数据，用分析仪确认 MOSI/CLK，
   并由随后 RX-only 读到全 `0xff` 确认没有残留 RX 数据。
6. 执行 sequence hold/release；分析仪确认 transfer 间 CS 是否保持或释放。
7. 执行 1、31、32、33、256、1024 words 边界，共 48 组 mode/width 组合。
8. 断开环回制造负例，必须 FAIL；恢复短接后必须再次 PASS。
9. 重跑 GPIO 可用节点、timer 001/002/005、WDT 002/003、oneshot、RTC，确认
   `nsh>` 存活且无系统异常。

每条命令、内部动作和 PASS 判据见
`apps/mcu_peripheral_tests/spi/README.md`，该文档直接记录关键实测数据，不依赖外部
任务日志。

## 实物实测数据

2026-10-13 在 Ai-M64P-32S-Kit（BL616CL QFN48，片内 4 MB flash、4 MB PSRAM）上测量。
固件为 `nsh-peripherals`，`nuttx.bin` SHA256 为
`7aa301b940a05d30f19ae2f6b749cd055547302c4d794dbb29f28fccd66e782f`。逻辑分析仪
DSLogic U3Pro32，阈值 1.6 V。命令省略了默认参数（bus 0、target 0、mode 0、8-bit、
400 kHz、32 words）。

负例：未短接时 `hw loopback` 报 FAIL。同时采集的波形中 CS 有一个约 0.84 ms 的低脉冲，
CLK 为 256 个时钟（32 字节 × 8 位），GPIO19 上解码出测试数据
`29 5E 93 C8 FD 32 …`（mode 0、MSB first），GPIO18 恒为高。这确认 MOSI 从 GPIO19 输出，
GPIO18 是未被驱动的输入。

短接 GPIO18-19 后的终端结果：

| 命令 | 结果 |
|---|---|
| `hw boundary` | mode 0..3 × 8/16-bit × 1/31/32/33/256/1024 words，48 组全部 PASS |
| `hw boundary --lsb` | 同一集合 48 组全部 PASS |
| `hw loopback --freq 100000` | PASS |
| `hw loopback --freq 1000000` | PASS |
| `hw loopback --mode 1`、`--mode 2`、`--mode 3` | 均 PASS |
| `hw paths --length 33` | full-duplex、TX-only、RX-only 均 PASS |
| `hw sequence` | `cs=hold:PASS release:PASS` |

2026-10-16 去掉测试钩子后的程序在同一短接下重跑上表命令（loopback 改为 100 kHz、1 MHz、
mode 1 16-bit、mode 3 LSB 四组），全部 PASS；`hw paths` 的 TX-only 之后 RX-only 读回全
`0xff`。

同时以 20 MHz 采集 CS（GPIO12）和 CLK（GPIO13）：

| 检查项 | 实测 |
|---|---|
| sequence hold 轮 | 一个 CS 低电平窗口内 16 个时钟，两段 transfer 之间 CS 不释放 |
| sequence release 轮 | 两个 CS 窗口各 8 个时钟，中间释放一次 |
| 请求 100 kHz / 400 kHz / 1 MHz | 实测 100.0 / 400.0 / 1000.0 kHz，不高于请求值 |
| CPOL | mode 0/1 时 CLK 空闲为低，mode 2/3 为高 |
| 1 MHz 连续性 | 第 104 个时钟后停顿 24.3 µs，其余连续 |

本轮未重跑 GPIO、timer、WDT、oneshot、RTC 现役回归。

同日补测 CPHA 和 bit order：不接 GPIO18-19 短接（环回数据比较按预期 FAIL），分析仪
50 MHz 采样 CS/CLK/GPIO18/GPIO19，执行
`mcu_spi_test hw loopback --freq 1000000 --mode N --length 4 [--lsb]`，N 为 0..3。
发送数据为 `29 5E 93 C8`。8 种组合的结果：

| mode | CLK 空闲 | MOSI 变化沿 | 按另一沿采样，MSB first 命令 | LSB first 命令 |
|---|---|---|---|---|
| 0 | 低 | 后沿（CPHA=0） | `29 5E 93 C8` | 按 LSB 拼为 `29 5E 93 C8` |
| 1 | 低 | 前沿（CPHA=1） | 同上 | 同上 |
| 2 | 高 | 后沿（CPHA=0） | 同上 | 同上 |
| 3 | 高 | 前沿（CPHA=1） | 同上 | 同上 |

每帧 32 个时钟，SCLK 1 MHz；GPIO18 全程无翻转，说明控制器不驱动 MISO 脚。

## 当前限制

- SPI0 裁剪、启动、`/dev/spi0` 注册、实物环回，以及 CS/CLK、CPOL/CPHA 和 bit
  order 波形已通过。
- SPI1 lower-half 可编译；四线 pin、模组引出、board 注册和实物测试尚未冻结，不能
  声明 SPI1 或双实例实板通过。
- TX-only 的波形没有自动判据，依赖逻辑分析仪；自动判据只覆盖它不残留 RX 数据。
- 普通 consumer 无法通过 `spi_transfer()` 返回值取得 `exchange(void)` 内部错误；
  exchange 失败只有 `spierr` 日志。
- recovery 还缺真实 timeout/FIFO/BUSY/CS 故障场景验证。
- `spi_register()` 失败会释放 controller 引用，但尚未回滚已设置的 pinmux 和 CS
  输出；这是不阻塞当前软件闭环的后续改进项。
