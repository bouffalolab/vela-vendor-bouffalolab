# BL616CL-AVE-V10 板级说明

BL616CL 音视频评估板，对应原生 Bouffalo SDK 的 `bsp/board/bl616cl_ave_v10`。板上有
ES8388 codec 和喇叭、DBI 8080 屏排针、DVP 摄像头、RMII 以太网、TF 卡座和 USB，用来
适配 Ai-M64P-32S-Kit 上没有对应硬件的外设。模组为 QFN48，片内 4 MB flash 和 4 MB
PSRAM。

## 配置

| 配置 | 内容 |
|---|---|
| `nsh` | 基础控制台，与 `ai-m64p-32s-kit/nsh` 相同 |
| `nsh-peripherals` | 本板外设测试：片内外设测试（crypto、dma、psram、rtc、timer、trng、wdt）、I2C0 与 `mcu_i2c_test`、NuttX `i2c` 工具；诊断项与 M64P 的同名配置相同（KASAN、canary、perfmon 等） |

本板适用的外设测试统一加入 `nsh-peripherals`。GPIO、UART、SPI、PWM 测试需要空闲引脚
或外接回环，本板没有，Kconfig 限定它们只在 Ai-M64P-32S-Kit 上编译。

```bash
./vela build bl616cl-ave-v10/nsh-peripherals
./vela flash bl616cl-ave-v10/nsh-peripherals --port /dev/ttyACM1
```

## 引脚

模组引出的 GPIO0–36 全部被板上外设占用，没有空闲 GPIO，也没有用户 LED（LED1 是电源灯）。

| 功能 | GPIO | 说明 |
|---|---|---|
| UART0 console | TX 34、RX 35 | 经 CH343P 接 USB-C，2 Mbps |
| BOOT | 36 | 同时是 LCD 的 DBI DC；按键 SW1 和 CH343P 的 DTR 也接在这根线上 |
| I2C0 | SCL 20、SDA 11 | 4.7 kOhm 上拉到 3.3 V；ES8388（地址 0x10）和摄像头 SCCB |
| I2S → ES8388 | MCLK 1、BCLK 4、FS 5、DI 6、DO 7 | MCLK 由 CLKOUT1 输出，尚未适配 |
| DBI 8080 | DB0–7 为 12–19、CS 0、WR 2、DC 36 | 2x14 排针，没有 RD、RESET、背光脚，尚未适配 |
| DVP / RMII | 23–31 | 经模拟开关二选一，见下节；尚未适配 |
| DVP 其它 | PCLK 21、HSYNC 22、MCLK 3（CLKOUT3） | 摄像头复位只有 RC，PWDN 下拉 |
| SD（1-bit） | CLK 9、CMD 8、DAT0 10 | DAT1–3 未接，没有卡检测脚；尚未适配 |
| USB | DP 32、DM 33 | Type-C 的 CC 只有 5.1 kOhm 下拉，只能做 device；尚未适配 |

ES8388 的模拟部分：板载模拟麦克风接 LIN2/RIN2；ROUT1/ROUT2 经 NS4150B 功放接喇叭座
U7，功放使能由电阻分压固定打开；功放输出经衰减回接 LIN1/RIN1，可作回声参考。

## DVP 与以太网二选一

GPIO23–31 经外部模拟开关接摄像头 DVP 或 RMII PHY（RTL8201F，25 MHz 晶振，REF_CLK
由 PHY 输出到 GPIO28），由跳线 H1（`STATUS_SW`）选择，两者不能同时用。固件读不到也
改不了 H1，测试前手动跳好。

| GPIO | DVP | RMII |
|---:|---|---|
| 23 | VSYNC | MDIO |
| 24 | DAT4 | MDC |
| 25 | DAT5 | TX_EN |
| 26 | DAT0 | TXD1 |
| 27 | DAT1 | TXD0 |
| 28 | DAT2 | REF_CLK |
| 29 | DAT3 | RXD1 |
| 30 | DAT6 | RXD0 |
| 31 | DAT7 | RX_DV |

## 串口与复位

控制台是 CH343P（USB ID `1a86:55d3`，Linux 下为 `/dev/ttyACM*`，编号随插拔变化）。
DTR 经 1 kOhm、RTS 经 0 Ohm 和两脚器件接 BOOT（GPIO36）和 EN。打开串口会让模组重启一次；
picocom 要加 `--lower-rts`，否则模组一直处于复位。FlashCube 自动下载和
`bl-module-reset` 的标准 DTR/RTS 时序都可用。

## I2C0 与 ES8388

`nsh-peripherals` 注册 `/dev/i2c0`。上板检查：

```text
i2c dev -b 0 0x08 0x77
i2c get -b 0 -a 10 -r 0
mcu_i2c_test hw probe --bus 0 --addr 0x10 --freq 400000
mcu_i2c_test hw eeprom --addr 16 --offset 26 --addr-width 1 --capacity 53 --length 1 --page-size 1 --allow-write
```

没接摄像头时总线上只有 0x10。ES8388 上电后寄存器 0x00 为 0x06，0x00–0x34 都应等于
NuttX `drivers/audio/es8388.h` 的复位默认值。ES8388 读操作不自动递增寄存器地址，
`i2c dump` 只有首字节有效，要逐个 `i2c get`。

NuttX `i2c set` 写不了寄存器：它用 `NOSTOP` + `NOSTART` 两条消息拼接写，BL616CL I2C
驱动不支持 `NOSTART`。写寄存器用单条消息，上面的 `hw eeprom` 每次只写一个寄存器，
并会备份、读回比较和恢复。完整结果见 `docs/bl616cl-i2c.md`。
