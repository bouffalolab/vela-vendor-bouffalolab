# Ai-M64P-32S-Kit 板级说明

## 统一外设测试配置

`configs/nsh-peripherals` 用于在同一固件中回归 crypto、DMA、GPIO、I2C、PSRAM、PWM、
RTC、SPI、Timer、TRNG、UART 和 WDT，编入全部 12 个 `mcu_*_test` 命令。GPIO 测试使用不与其他外设冲突的 GPIO23/20 回环，不编译
`gpio` 示例。NSH 命令行最长 127 个字符、最多 16 个参数，并开启命令历史和
Tab 补全，各测试 README 中写全参数的命令可以直接输入。

本板适用的外设测试统一加入此配置，不为单个外设新增 defconfig。GPIO、UART、SPI、PWM 测试依赖本板接线，Kconfig 限定只在本板（`ARCH_BOARD_AI_M64P_32S_KIT`）编译；音视频类外设在 `bl616cl-ave-v10` 上测试。
基础 `configs/nsh` 提供 `free`、`ps` 命令，并通过启动脚本挂载 `/proc`。

QFN48 封装提供 GPIO0 到 GPIO36。片内 flash（SF1）和 PSRAM 不占用这些 GPIO，
GPIO6–11、21 可作普通 IO（GPIO6–11 只是外置 flash SF2 的候选脚）。板级固定或保留
资源如下：

| 功能 | GPIO | 说明 |
|---|---:|---|
| USB | 32、33 | 不得用于测试外设 |
| UART0 console | TX 34、RX 35 | 2 Mbps 启动和 NSH 控制台 |
| UART1 | TX 14、RX 15 | 测试时外接环回线 |
| I2C0 | SCL 30、SDA 31 | 外接 3.3 V 目标和上拉电阻 |
| I2C1 | SCL 4、SDA 5 | 外接 3.3 V 目标和上拉电阻 |
| SPI0 | CS 12、CLK 13、MISO 18、MOSI 19 | 测试时 MISO 与 MOSI 外接环回 |
| SPI1 | 无 | 四线 pin 未冻结，板级不注册、不占引脚 |
| PWM0 | CH0 正 8/负 9、CH1 正 10/负 11、CH2 负 21、CH3 正 6/负 7 | `nsh-peripherals` 的分配，可在板级 Kconfig 改；四通道同频，负输出是正输出的反相 |
| GPIO 回环 | GPIO23 output、GPIO20 input | `mcu_gpio_test` 默认回环，测试时外接跳线 |
| 板载 LED | 红 14、绿 15、蓝 16、暖白 28、冷白 27 | 高电平点亮（Ai-M64P-32S-Kit V1.0 原理图）；蓝灯注册为 `/dev/gpio16` |

I2C1 的板级通用默认仍为 GPIO12/13；`nsh-peripherals` 显式覆盖为 GPIO4/5，
以保留 SPI0 的既有四线分配，且不影响只启用 I2C1 的配置。I2C0 的板级默认是
GPIO4/5，`nsh-peripherals` 覆盖为 GPIO30/31，两条总线不撞脚。I2C controller 的
SCL 必须使用偶数 GPIO、SDA 必须使用奇数 GPIO；GPIO4/5 满足该约束，并使用
独立的 I2C1 pin function。

TIMER1 同时是普通 timer lower-half 和 one-shot lower-half 的硬件资源。
统一配置选择普通 TIMER1，提供 `/dev/timer0` 和 `/dev/timer1`，因此关闭
`CONFIG_BL616CL_ONESHOT`。这不影响架构 MTIMER 系统时钟。

## 外接测试前置

- UART1：GPIO14 TX 与 GPIO15 RX 短接；这两脚同时接板载红、绿 LED，测试时会闪。
- SPI0：GPIO18 MISO 与 GPIO19 MOSI 短接；GPIO12 是软件片选。
- I2C0/I2C1：GPIO30/31、GPIO4/5 各接外部 3.3 V I2C 目标，并提供 SCL/SDA 上拉。
  没有目标时硬件探测/传输用例应报告环境缺失。
- RTC：`mcu_rtc_test -c 005` 会删除 `/dev/rtc0`，放在最后单独跑，之后复位。
- PWM：测量时把探头接 GPIO6–11、21（CH2 只有负输出 21）。
- WDT：会触发复位的用例必须单独执行并根据 reset cause 验证，不能与其余
  并发测试混跑。
- GPIO：GPIO23 与 GPIO20 短接；统一配置只注册 `/dev/gpio16`（板载蓝灯）、
  `/dev/gpio20`、`/dev/gpio22` 和 `/dev/gpio23`。
