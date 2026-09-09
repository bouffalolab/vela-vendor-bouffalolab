# Ai-M64L-32S-Kit 板级说明

## 统一外设测试配置

`configs/nsh-peripherals` 用于在同一固件中回归 DMA、I2C、PSRAM、PWM、SPI、
Timer、UART 和 WDT。该配置保留 GPIO lower-half，但关闭与 SPI0 引脚冲突的
GPIO 示例和测试命令。

后续新增外设驱动及测试统一加入此配置，不为单个外设新增 defconfig。
基础 `configs/nsh` 提供 `free`、`ps` 命令，并通过启动脚本挂载 `/proc`。

QFN48 封装提供 GPIO0 到 GPIO36。板级固定或保留资源如下：

| 功能 | GPIO | 说明 |
|---|---:|---|
| SPI Flash | 6-11、21 | 不得用于测试外设 |
| USB | 32、33 | 不得用于测试外设 |
| UART0 console | TX 34、RX 35 | 2 Mbps 启动和 NSH 控制台 |
| UART1 | TX 14、RX 15 | 测试时外接环回线 |
| I2C1 | SCL 4、SDA 5 | 外接 3.3 V 目标和上拉电阻 |
| SPI0 | CS 12、CLK 13、MISO 18、MOSI 19 | 测试时 MISO 与 MOSI 外接环回 |
| PWM0 channel 3 | 22 | 正向输出，可接逻辑分析仪或示波器 |
| GPIO 安全节点 | GPIO23 output、GPIO20 input | 不占用上述外设引脚；不构成默认回环对 |

I2C1 的板级通用默认仍为 GPIO12/13；`nsh-peripherals` 显式覆盖为 GPIO4/5，
以保留 SPI0 的既有四线分配，且不影响只启用 I2C1 的配置。I2C controller 的
SCL 必须使用偶数 GPIO、SDA 必须使用奇数 GPIO；GPIO4/5 满足该约束，并使用
独立的 I2C1 pin function。

TIMER1 同时是普通 timer lower-half 和 one-shot lower-half 的硬件资源。
统一配置选择普通 TIMER1，提供 `/dev/timer0` 和 `/dev/timer1`，因此关闭
`CONFIG_BL616CL_ONESHOT`。这不影响架构 MTIMER 系统时钟。

## 外接测试前置

- UART1：GPIO14 TX 与 GPIO15 RX 短接。
- SPI0：GPIO18 MISO 与 GPIO19 MOSI 短接；GPIO12 是软件片选。
- I2C1：GPIO4/5 接外部 3.3 V I2C 目标，并提供 SCL/SDA 上拉。没有目标时
  fake transport 用例仍可运行，硬件探测/传输用例应报告环境缺失。
- PWM：GPIO22 接测量设备；不要同时驱动板载绿色 LED 测试接口。
- WDT：会触发复位的用例必须单独执行并根据 reset cause 验证，不能与其余
  并发测试混跑。
- GPIO：统一配置只注册 `/dev/gpio23` 和 `/dev/gpio20`，不编译
  `mcu_gpio_test` 或 `gpio` 示例。需要 GPIO12 到 GPIO20 回环时使用独立 GPIO
  测试配置，不得同时启用 SPI0。
