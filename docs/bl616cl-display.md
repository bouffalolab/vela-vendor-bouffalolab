# BL616CL 显示（LCD）与触摸适配与验证

本文说明 Bouffalo SDK 统一显示框架和触摸芯片驱动在 BL616CL openvela 上的移植：能力
范围、代码归属、DMA 与时序、Kconfig、NuttX LCD 与 touchscreen 接口语义，以及在
BL616CL-AVE-V10 上的实测和限制。板级接线与上板命令见
`boards/bl616cl/bl616cl-ave-v10/README.md` 的“屏与触摸”，测试命令见
`apps/mcu_peripheral_tests/display/README.md`，代码来源与 SDK 同步方法见
`components/display/README.md`。

## 背景

Bouffalo SDK 的 `bsp/common/display` 把显示分成四层：总线（DBI、SPI、软件 DBI type C
等）、端点（一块屏在总线上的接口、片选和时钟）、屏（DCS 命令集的通用驱动加各款屏的
初始化描述）、显示对象（GRAM 屏用 `display_draw()` 推像素）。`bsp/common/touch` 有
GT911、FT6x36 两款 I2C 触摸芯片驱动。此前 openvela 上 BL616CL 没有显示和触摸支持。

本次把框架原样带入 `components/display`，BL616CL 的总线实现放在 chips，板级代码定义
总线、屏和控制脚，上层接 NuttX 的 LCD 字符设备（`/dev/lcd0`）、可选的 framebuffer
（`/dev/fb0`）和 touchscreen（`/dev/input0`），LVGL 用这些节点运行。逻辑照 SDK，只在
openvela 结构要求的地方改动（见 `components/display/README.md` 的“与 SDK 的结构差异”）。

## 能力范围

| 能力 | 结论 | 边界 |
|---|---|---|
| DBI type B（8080 8 位） | 纳入，已实测 | AVE-V10 U10；命令和参数 CPU 写 FIFO，像素默认 DMA0；WR 时钟上限 54 MHz |
| DBI type C 3/4 线、QSPI | 纳入，只构建 | 与 type B 同一个总线实现；板上没有对应的屏 |
| SPI 显示总线（type C 4 线） | 纳入，只构建 | SPI0 或 SPI1，由显示独占；RGB565 按 16 位帧、XRGB8888 按 24 位帧发送；时钟上限 40 MHz |
| 软件 DBI type C（GPIO） | 纳入，只构建 | 3 线；有 DC 脚时 4 线；同步、慢，用于 RGB 屏的命令口或小屏 |
| 屏描述 | 24 款全部可编译链接；ST7796 320x480 已实测 | 每款一个组件选项，只编译板级选中的 |
| 像素格式 | RGB565 已实测；RGB888、XRGB8888 只构建 | 由总线和屏共同决定 |
| `/dev/lcd0` | 纳入，已实测 | `PUTAREA`、`PUTRUN`、视频/plane/对齐查询、背光；读回不支持 |
| `/dev/fb0` | 选项，默认关 | NuttX `lcd_framebuffer` 前端，整帧缓冲在 RAM 里，`FBIO_UPDATE` 时刷到屏 |
| 旋转 | 纳入 | 0/90/180/270，由框架按屏的 MADCTL 实现；实测只用了 0 |
| GT911 触摸 | 纳入，已实测 | 轮询，单点，芯片原始坐标 |
| FT6x36 触摸 | 纳入，只构建 | 同上 |
| TE 同步、读屏 ID | 不支持 | AVE-V10 没有引出 TE、RD |
| 硬件 DPI/RGB 视频输出 | 不支持 | BL616CL 没有 DPU；框架的 scanout 核心只为编译需要带入 |

## 代码归属与调用链

```text
应用 / LVGL
  │ open/ioctl /dev/lcd0                    │ read /dev/input0
  ▼                                          ▼
NuttX lcd_dev.c（可选 lcd_framebuffer.c）    NuttX touchscreen upper half
  │ struct lcd_dev_s                         │ touch_event()
  ▼                                          ▼
components/display/nuttx/display_lcddev.c    components/display/touch/touch_poll.c
  │ display_draw()                           │ LPWORK 定时读
  ▼                                          ▼
components/display（SDK 原样：display、      touch_gt911.c / touch_ft6x36.c
  bus core、DCS 屏驱动、屏描述）               │ I2C_TRANSFER()
  │ display_bus_ops_t                        ▼
  ▼                                        NuttX I2C（bl616cl_i2c.c）
chips/bl616cl：bl616cl_dbi.c、
  bl616cl_display_spi.c、soft_dbi_c（SDK 文件）、
  bl616cl_display.c（GPIO）
  │ LHAL bflb_dbi/bflb_spi、NuttX DMA API
  ▼
DBI0 / SPI0/1 / GPIO，DMA0
```

板级代码（`boards/bl616cl/bl616cl-ave-v10/src/bl616cl_ave_v10_display.c`、
`_touch.c`）静态定义总线、端点和屏，实现 `board_lcd_initialize()`、
`board_lcd_getdev()`，在板级初始化里注册 `/dev/lcd0`、`/dev/fb0` 和 `/dev/input0`。

用到 LHAL 的代码都在 chips，先包含 `bl616cl_lhal.h`：LHAL 头文件会引入工具链 newlib 的
`errno` 定义，与 NuttX 不同的值（例如 `ENOTSUP`、`ETIMEDOUT`）会在同一个编译单元里
静默覆盖 NuttX 的值。软件 DBI type C 是 SDK 文件，不改，由 chips 的 CMake 强制预包含
`bl616cl_lhal.h` 后编进 arch。

## DMA 与时序

**通道**：DBI 和 SPI 显示总线各用一个固定的 DMA0 通道，在 `DMA0 channel assignment`
里分配（默认 DBI TX CH2、Display SPI TX CH3），总线初始化时取得后一直持有，重复分配
编译失败（见 [bl616cl-dma.md](bl616cl-dma.md) 的“通道分配”）。SDK 用 `dma_mgr` 动态
分配，openvela 不做动态分配。

**单次刷新的长度**：一次 `display_draw()` 由一个 DMA 传输发出，超过 4095 个单位时由
DMA 适配层拆成每段 4064 个单位、用 LLI 节点串起来。`BL616CL_DBI_DMA_LLI_COUNT`（默认 40）
决定一次刷新最多多少字节：DBI 每节点 16256 字节，40 个节点 650240 字节，够两帧 320x480
RGB565；SPI 按 16 位单位计，每节点 8128 字节。LLI 节点来自 DMA0 的共享池
`BL616CL_DMA0_LLI_NODES`：传输开始时取、结束时还，所有 DMA0 用户共用。一次刷新用
“节点数减一”个，320x480 RGB565 整帧在 DBI 上用 18 个，到上限 40 时用 39 个；池子小于
39 时编译失败。默认值按同时进行的传输合计：只开 DBI 或 SPI 显示 DMA 时 56（一路显示
刷新到上限，加 16 个给其它用户），两路都开时 96，保证两路同时刷到上限也够。改小池子或
调大每次刷新节点数时按同样的合计核算，不够时这次 `DMA_START()` 失败、刷新返回错误。
超过单次上限的刷新由 `display_lcddev.c` 按行拆小，应用不用关心。

**burst**：照 SDK。DBI 在 CH0–CH5 用 INCR8（这几个通道 FIFO 较大）、FIFO 门限 7，
CH6、CH7 用 INCR4、门限 3；SPI 用 INCR4、FIFO 门限 7。burst 经 `dma_config_s.option`
传给 DMA 适配层，其它 DMA 用户不受影响。

**完成**：DBI 在 TC 中断里断开 DMA 请求、清并屏蔽 TC、停通道，通知框架；SPI 在 DMA
完成回调里等 SPI 空闲后断开 DMA 请求、清 RX FIFO、恢复 8 位帧，通知框架。框架发 DMA
前清 cache（端点 `dcache_dis` 为 0 时），同步刷新等完成信号量。

**出错与超时**：照 SDK，同步刷新一直等完成，不设超时；某次传输出错时框架忘掉当前
端点，下一次传输重新配置控制器。DBI 等不到 TC（例如 DMA 出错）会一直阻塞，与 SDK
相同。SPI 的完成来自 NuttX DMA 回调，DMA 出错时回调带错误码并结束这次传输；和 SDK
一样，只有异步刷新的回调拿得到这个错误，同步刷新（`/dev/lcd0` 走的路径）仍返回 0。

**命令与参数**：DBI 命令和最多 255 字节参数由 CPU 写 FIFO；SPI 命令和参数轮询发送。
关掉 `BL616CL_DBI_DMA` 时像素也由 CPU 写 FIFO（SDK 无 DMA 时的路径，用于点屏调试）。

## Kconfig 与裁剪

| 选项 | 默认 | 作用 |
|---|---|---|
| `BL616CL_DISPLAY` | n | 显示框架（select `BL_COMPONENT_DISPLAY`），编译 chips 的显示 GPIO |
| `BL616CL_DBI` | y | DBI 控制器总线 |
| `BL616CL_DBI_CLK_MODE` | 1 | type C、QSPI 的时钟极性和相位（SDK 默认值） |
| `BL616CL_DBI_DMA` | y | 像素经 DMA0（依赖 `BL616CL_DMA0`） |
| `BL616CL_DBI_DMA_LLI_COUNT` | 40 | 一次刷新最多的 LLI 节点数 |
| `BL616CL_DISPLAY_SPI` | n | SPI 显示总线；控制器在 SPI0/SPI1 里选，所选控制器不能同时打开 `BL616CL_SPI0/1` |
| `BL616CL_DISPLAY_SPI_MODE` | 0 | SPI 模式 |
| `BL616CL_DISPLAY_SPI_DMA` | y | 像素经 DMA0 |
| `BL616CL_DISPLAY_SPI_DMA_LLI_COUNT` | 40 | 同上 |
| `BL616CL_DISPLAY_SOFT_DBI_C` | n | 软件 DBI type C 总线 |
| `BL616CL_DBI_TX_DMA_CHANNEL`、`BL616CL_DISPLAY_SPI_TX_DMA_CHANNEL` | 2、3 | 在 `DMA0 channel assignment` 里 |
| `BL616CL_TOUCH` | n | 触摸芯片驱动（select `BL_COMPONENT_TOUCH`、`INPUT_TOUCHSCREEN`、`SCHED_LPWORK`） |
| `BL_COMPONENT_DISPLAY_PANEL_<IC>` | — | 无菜单，由板级屏模组选项 select，只编译选中的屏 |
| `BL_COMPONENT_TOUCH_GT911/FT6X36` | — | 无菜单，由板级触摸芯片选项 select |

AVE-V10 的板级选项（`BL616CL_AVE_V10_DISPLAY`、屏模组、WR 时钟、旋转、
`BL616CL_AVE_V10_TOUCH`、触摸芯片、轮询周期）见板级 README。上层选项：`LCD_DEV` 注册
`/dev/lcd0`；`VIDEO_FB` 加 `LCD_FRAMEBUFFER` 注册 `/dev/fb0`。

关闭 `BL616CL_DISPLAY`、`BL616CL_TOUCH` 时，组件、chips 显示代码、板级显示与触摸代码
都不编译，不开 DBI 时钟、不占 DMA 通道、不注册设备节点。`bl_lhal` 库里原有的
`bflb_dbi.o` 不链接进最终镜像。

## NuttX 接口语义

**`/dev/lcd0`**（`nuttx/display_lcddev.c`）：

- `PUTAREA`：行连续（stride 等于行宽字节数）且缓冲按总线要求对齐（DBI、SPI 为 4 字节）
  时，缓冲直接交给框架发 DMA，PSRAM、SRAM 都可以；超过一次刷新上限时按行拆分，每段
  起点保持对齐。stride 不连续、缓冲未对齐或奇数宽度导致段起点不对齐时，逐行拷到约
  4 KiB 的暂存缓冲再发。
- `PUTRUN` 等同于一行的 `PUTAREA`。
- `GETRUN`、`GETAREA` 返回 `-ENOTSUP`：GRAM 屏不读回。
- `GETVIDEOINFO`、`GETPLANEINFO`：分辨率为旋转后的；RGB565 报 `FB_FMT_RGB16_565`、
  16 bpp，RGB888 报 `FB_FMT_RGB24`，XRGB8888 报 `FB_FMT_RGB32`。
- `GETAREAALIGN`：屏要求的行列对齐和缓冲对齐；ST7796 行列为 1，缓冲 4 字节。
- `SETPOWER`：按 `LCD_MAXPOWER` 比例设背光，板级没有背光控制脚时返回 `-ENOTSUP`
  （AVE-V10 即如此）。`SETCONTRAST` 返回 `-ENOTSUP`。

**`/dev/input0`**（`touch/touch_poll.c`）：LPWORK 每个周期读一次芯片，取一个点；新接触报
`TOUCH_DOWN`，位置变化报 `TOUCH_MOVE`，接触消失报 `TOUCH_UP`，位置不变不报。坐标是
芯片原始坐标，不随屏的旋转变换（与 SDK 相同）。芯片读写照 SDK：GT911 不改写配置，
读状态寄存器后读点、清状态；FT6x36 读 `TD_STATUS` 和最多两个点。I2C 用 100 kHz（SDK
为 200 kHz，BL616CL I2C 驱动只支持 100、400 kHz）。

## 实板验证

环境：BL616CL-AVE-V10，U10 接随附的 ST7796 320x480 屏（RGB565）与 GT911 触摸，WR 时钟
约 26.7 MHz。

**画图与帧率**（`nsh-peripherals`，`mcu_display_test`）：

| 项目 | 像素经 DMA0 | CPU 写 FIFO |
|---|---|---|
| `info`、`bars`、`area` | PASS | PASS |
| 整屏一次刷新（缓冲在 PSRAM），只算画图 | 81 fps（约 12.2 ms，理论 11.5 ms） | 约 11 fps（含填缓冲） |
| 每次 40 行（缓冲在 SRAM），只算画图 | 57 fps | 约 21 fps（含填缓冲） |
| 每次 1 行 | 3 fps（每次约 0.56 ms） | — |
| 同时放音（`mcu_i2s_test tone`） | 整屏 77 fps、40 行 47 fps，I2S PASS | — |

`nsh-peripherals` 带 KASAN 等诊断项，CPU 侧的数字偏慢。

**触摸**：GT911 读到 Product ID `0x00313139`、固件 `0x1060`、分辨率 320x480；`idle`
空闲 10 s 与 20 s 都没有事件。

**画面与触摸的人工确认**（人在板旁看屏、按屏）：色条顺序、四角方块和 `area` 各块的
位置、颜色都正确，模组复位后和 CPU 写 FIFO 时画面相同。`fill` 整屏红绿蓝快速轮换时，
屏幕中部能看到横向撕裂（没有 TE，见“限制”）。`touch` 两轮共 10 次点按，抬手位置离
十字中心 5–14 像素（约 0.15 mm/像素），方向和四角映射正确；两次拖动各有 18、13 个
MOVE，横坐标连续增大。LVGL 演示可以正常操作，但跟手一般：帧率不高，拖动后松手，惯性
滚动会先顿一下再继续滑行。

**LVGL**（LVGL 9 widgets 演示，`LV_USE_NUTTX_LCD` 经 `/dev/lcd0`，`/dev/input0` 触摸，
monkey 经 `/dev/utouch` 注入点击、长按和拖动）：

| 配置 | 结果 |
|---|---|
| `nsh-peripherals` + LVGL + monkey（`-t 0x11 -s 320x480 --running-minutes 30`） | 30 分钟跑完，时间戳 0→1817 s 连续（没有复位），无错误日志；堆从 421308 到 422196 字节（峰值 425432），没有持续增长；结束后 sysmon 仍每 0.3 s 输出；单帧 render 平均 288 ms（KASAN、非全优化），flush 平均 12.7 ms、最大 15 ms |
| `lvgl` 演示配置（全优化、无 KASAN）+ monkey 1 分钟 | render 平均 37 ms、最大 65 ms，flush 平均 12 ms、最大 13 ms，CPU 平均 41%，堆约 407 KB |

`lvgl` 配置的 widgets 演示需要 Montserrat 12、18 号字体，配置已打开（镜像
647856 字节）。

**framebuffer**（`VIDEO_FB` + `LCD_FRAMEBUFFER`）：注册 `/dev/fb0`，整帧缓冲 307200
字节；NuttX `fb` 示例画完 6 层矩形正常退出，LVGL 改用 fbdev（关 `LV_USE_NUTTX_LCD`）
后正常运行，一次整帧刷新约 17 ms。

**构建与裁剪**：Ai-M64P-32S-Kit `nsh`、`nsh-peripherals`、`wifi`、`ble`，AVE-V10 `nsh`、
`nsh-peripherals`、`lvgl`，以及 AVE 上显示/触摸只开一个、都不开的三种组合全部构建通过，
显示和触摸代码没有新增 warning（`wifi`、`ble` 全量构建里已有的 83 条 warning 都在
wl80211、lhal、rfparam、supplicant、zblue 里）。显示关闭的配置里最终镜像没有
`bflb_dbi`、`display_`、`lcddev` 符号，不生成 `libbl_display.a`，arch、board 库里没有
显示对象；触摸关闭同理。8 款可选的 8080 屏模组各构建一次，镜像里只有所选屏的描述。
SPI 显示总线（DMA 与轮询、SPI0 与 SPI1）、软件 DBI type C、24 款屏的全部 30 个模组
描述和 FT6x36 用临时配置编译链接通过。

**回归**：两板 `nsh-peripherals` 现役外设测试全部 PASS（AVE：crypto、trng、psram、rtc、
timer、wdt、i2s、i2c、dma、display；M64P：39 项）。

## 限制

- 同步刷新不设超时：DBI 等不到 TC 会一直阻塞，与 SDK 相同。
- SPI 显示总线、DBI type C/QSPI、软件 DBI type C 和除 ST7796 外的屏只做了编译链接，
  没有实物验证；SPI 的 MOSI/MISO 交换照 SDK 板级初始化固定为不交换。
- 读不回画面。AVE-V10 没有引出 TE，框架的 8080/SPI 屏路径也不等 TE，整屏快速换色
  （如 `fill`）时能看到横向撕裂。
- LVGL widgets 演示约 20 fps（render 37 ms、flush 12 ms），拖动松手后惯性滚动会先顿一下，
  帧率和滑动手感留待优化。
- 触摸只报一个点，坐标不随旋转；没有中断线，靠轮询（默认 20 ms）。
- DMA 通道固定分配，不支持运行时借还。
