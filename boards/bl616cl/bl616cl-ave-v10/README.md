# BL616CL-AVE-V10 板级说明

BL616CL 音视频评估板，对应原生 Bouffalo SDK 的 `bsp/board/bl616cl_ave_v10`。板上有
ES8388 codec 和喇叭、DBI 8080 屏排针、DVP 摄像头、RMII 以太网、TF 卡座和 USB，用来
适配 Ai-M64P-32S-Kit 上没有对应硬件的外设。模组为 QFN48，片内 4 MB flash 和 4 MB
PSRAM。

## 配置

| 配置 | 内容 |
|---|---|
| `nsh` | 基础控制台，与 `ai-m64p-32s-kit/nsh` 相同 |
| `nsh-peripherals` | 本板外设测试：片内外设测试（crypto、dma、psram、rtc、timer、trng、wdt）、I2C0 与 `mcu_i2c_test`、NuttX `i2c` 工具、ES8388 音频（`/dev/audio/pcm0`、`/dev/audio/pcm_in0`）与 `mcu_i2s_test`；诊断项与 M64P 的同名配置相同（KASAN、canary、perfmon 等） |

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
| I2S → ES8388 | MCLK 1、BCLK 4、FS 5、DI 6、DO 7 | MCLK 由 CLKOUT1 输出，CLKOUT1 不能再作他用 |
| DBI 8080 | DB0–7 为 12–19、CS 0、WR 2、DC 36 | 2x14 排针，没有 RD、RESET、背光脚，尚未适配 |
| DVP / RMII | 23–31 | 经模拟开关二选一，见下节；尚未适配 |
| DVP 其它 | PCLK 21、HSYNC 22、MCLK 3（CLKOUT3） | 摄像头复位只有 RC，PWDN 下拉 |
| SD（1-bit） | CLK 9、CMD 8、DAT0 10 | DAT1–3 未接，没有卡检测脚；尚未适配 |
| USB | DP 32、DM 33 | Type-C 的 CC 只有 5.1 kOhm 下拉，只能做 device；尚未适配 |

ES8388 的模拟部分：板载 IMA2718T421 模拟 MEMS 麦由 3.3V_A 供电（不用 codec 的 mic
bias），OUT 经电容接 LIN2，RIN2 经电容接麦的地；ROUT1/ROUT2 经 NS4150B 功放差分输入
接喇叭座 U7，功放使能由电阻分压固定打开；功放输出（原理图网络名 MICB±，实为喇叭线）
经 4.7 kOhm/510 Ohm 衰减回接 LIN1/RIN1，可作回声参考。

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
i2c set -b 0 -a 10 -r 1a c4
mcu_i2c_test hw probe --bus 0 --addr 0x10 --freq 400000
mcu_i2c_test hw eeprom --addr 16 --offset 26 --addr-width 1 --capacity 53 --length 1 --page-size 1 --allow-write
```

没接摄像头时总线上只有 0x10。开机时音频驱动已经配置过 codec，寄存器不再是复位默认值
（例如 0x00 为 0x12）。ES8388 读操作不自动递增寄存器地址，`i2c dump` 只有首字节有效，
要逐个 `i2c get`。

`i2c set` 可以写寄存器（上例把 0x1A 写成 0xC4，先 `i2c get` 记下原值，用完写回）。不要加
`-s`：它把寄存器号和数值分成两次调用发送，驱动不支持；i2ctool 的选项会保留到后续
命令，误用后加一次 `-n` 恢复。`hw eeprom` 每次只写一个寄存器，并会备份、读回比较和
恢复。完整结果见 `docs/bl616cl-i2c.md`。

## 音频（I2S0 + ES8388）

`nsh-peripherals` 打开 `BL616CL_AVE_V10_AUDIO`：I2S0 为 master，MCLK 由 WIFIPLL
320 MHz 分频（11.43 或 12.31 MHz，随采样率系列变），NuttX ES8388 驱动注册
`/dev/audio/pcm0`（播放）和 `/dev/audio/pcm_in0`（录音）两个节点。

两个节点是同一颗 codec 的两个驱动实例，每次启动都会复位整颗 codec，不能同时播放和
录音。上板检查：

```text
ls /dev/audio
mcu_i2s_test
mcu_i2s_test tone -t 3000 -c left
mcu_i2s_test tone -t 3000 -c right
mcu_i2s_test record -t 5000 -p
```

### ES8388 通路与本板接线

codec 走哪条通路由 NuttX ES8388 驱动的 `ES8388 output channel`、`ES8388 input
channel` 两个选项决定。每个选项都能用，效果取决于本板接线（见上文 ES8388 的模拟部分）；
`BL616CL_AVE_V10_AUDIO` 子菜单会显示当前选择在本板上的结果。`nsh-peripherals` 用输出
Line 2、输入 Line 2。

| ES8388 选项 | 本板效果 | 依据 |
|---|---|---|
| 输出 Line 2 | 只开 LOUT2/ROUT2，ROUT2 进功放 INN，喇叭放右声道 | `nsh-peripherals` 使用，实测 |
| 输出 Line 1 | 只开 LOUT1/ROUT1，ROUT1 进功放 INP，喇叭放右声道 | 未实测 |
| 输出 Both | ROUT1、ROUT2 都是右声道、同相，在功放差分输入端互相抵消，喇叭几乎无声；codec 和 I2S 照常工作 | 按原理图推断，未单独实测 |
| 输入 Line 2 | 左声道是板载麦克风，右声道是 RIN2 参考端噪声 | `nsh-peripherals` 使用，实测 |
| 输入 Line 1 | 左、右声道分别是两根喇叭线经 4.7 kOhm/510 Ohm 衰减后的回授，可作回声参考 | 未实测 |
| 输入 Differential | 驱动固定取 LIN1−RIN1，即喇叭回授的差分信号，不是麦克风 | 未实测 |

`mcu_i2s_test` 不带参数时跑播放（单音、8 个采样率、反复启停）和 3 秒录音，自动判定
帧率和录音数据是否有错位造成的尖峰；`-c left` 应无声、`-c right` 有声，喇叭是否出声、
录音时说话电平是否升高、回放是否可辨需要人工确认。44.1/22.05/11.025 kHz 实际快约
1.2%；非 256 fs 配置下本板录音底噪升高。细节见
`docs/bl616cl-i2s.md` 和 `apps/mcu_peripheral_tests/i2s/README.md`。
