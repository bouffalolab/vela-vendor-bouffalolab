# MCU 显示与触摸测试

`display_test_main.c` 编译为独立的 NSH 命令 `mcu_display_test`。测试只用公共接口：
`/dev/lcd0` 的 LCD 字符设备 ioctl（`LCDDEVIO_GETVIDEOINFO`、`GETPLANEINFO`、
`GETAREAALIGN`、`PUTAREA`、`PUTRUN`）和 `/dev/input0` 的 touchscreen 读取，不调用显示框架
或 DBI 总线的接口。

## 背景与边界

`/dev/lcd0` 和 `/dev/input0` 由 BL616CL-AVE-V10 板级代码注册：U10 排针上的屏经 DBI
控制器（8080 8 位）驱动，触摸芯片挂在 I2C0 上轮询读取，见
`boards/bl616cl/bl616cl-ave-v10/README.md` 的“屏与触摸”。屏的 RD 脚没有引出，固件读不回
画面，所以画图用例只自动判定 ioctl 返回值和帧率，画面是否正确要人看：

- 色条的颜色顺序和四角标记的位置（方向、红蓝是否对调）；
- 小矩形、奇数宽度和非连续缓冲是否画在指定位置，区外是否保持原样；
- 整屏填充的颜色是否正确、有无花屏。

触摸用例需要有人按屏；按下和抬手位置由测试自动判定。

## 运行前检查

```text
CONFIG_BL616CL_DISPLAY=y
CONFIG_BL616CL_AVE_V10_DISPLAY=y
CONFIG_LCD_DEV=y
CONFIG_BL616CL_TOUCH=y
CONFIG_BL616CL_AVE_V10_TOUCH=y
CONFIG_BL_MCU_PERIPHERAL_TESTS_DISPLAY=y
```

测试依赖 `BL616CL_AVE_V10_DISPLAY` 和 pthread（并发用例 DISP-008 要用），只在 AVE-V10 上编译；没有 `INPUT_TOUCHSCREEN` 时
`touch`、`idle` 两个用例不编译。`bl616cl-ave-v10/nsh-peripherals` 已全部打开。
开机日志应有 `Touch Product ID: 0x00313139`（GT911）；没有接屏或触摸芯片不应答时，
板级初始化打印错误后继续，`/dev/input0` 不存在。

## 命令

```text
mcu_display_test [info|bars|fill|area|edge|concurrent|touch|idle|all] [-r rows] [-t seconds]
```

| case | 内容 |
|---|---|
| `info`（DISP-001） | 打印分辨率、像素格式、bpp、plane 数、`GETAREAALIGN` 的对齐要求，以及 `GETFRAMERATE`/`SETFRAMERATE` 的结果（刷新率由屏决定，两者都返回 `-ENOTSUP`） |
| `bars`（DISP-002） | 从左到右 8 条竖色条：白、黄、青、绿、品红、红、蓝、黑；四角各一个方块：左上红、右上绿、左下蓝、右下白 |
| `fill`（DISP-003） | 整屏依次填红、绿、蓝，共 30 帧，每次 `PUTAREA` 画 `-r` 行（默认整屏一次）；打印总帧率和只算画图时间的帧率 |
| `area`（DISP-004） | 灰底上画：(0,0) 红色 8x8；(101,57) 绿色 33x21（奇数宽高）；(40,120) 蓝色 20x20，缓冲故意不按 4 字节对齐；(160,200) 30x10，品红、青色左右各半，从更宽缓冲里按 stride 取（非连续）；(10,300) 用 `PUTRUN` 画 50 像素黄线 |
| `edge`（DISP-007） | 用长度刚好等于区域的缓冲画：(10,440) 白色 1x1（2 字节）、(20,440) 红色 3x1（6 字节）、(30,440) 绿色 5x3（30 字节），长度都不是整 4 字节，总线不能读过缓冲末尾 |
| `concurrent`（DISP-008） | 两个线程各画 200 次 30x10 的窗口（从 64 像素宽的图里按 stride 取，走驱动的暂存缓冲）：(20,450) 品红、(200,450) 青色，两块都应是纯色 |
| `touch`（DISP-005） | 依次在四角和中心显示白色十字，每个按一下，五个都按完再判定，抬手位置与十字中心相差不超过 20 像素（ST7796 3.5 英寸屏约 3 mm，手指点按的偏差）算通过；最后沿屏幕中线从左拖到右，至少 5 个 MOVE、横向移动不少于屏宽一半 |
| `idle`（DISP-006） | `-t` 秒（默认 10）内不碰屏，不能收到任何触摸事件 |

`all` 依次执行 `info`、`bars`、`fill`、`area`、`edge`、`concurrent`，不含需要人按的
`touch` 和 `idle`。每个
case 最后打印 `PASS` 或 `FAIL` 和返回值。

`fill` 的缓冲用 `malloc` 申请，打开 PSRAM 时整屏缓冲落在 PSRAM，`-r 40` 这类较小的
缓冲通常在 SRAM；打印的缓冲地址可以区分（`0x6...` 为 SRAM，`0x88...` 为 PSRAM）。

## 实测结果

2026-11-26，BL616CL-AVE-V10，ST7796 320x480 RGB565，GT911，`nsh-peripherals`：

| 项目 | 像素经 DMA0（默认） | CPU 写 FIFO（关 `BL616CL_DBI_DMA`） |
|---|---|---|
| `info`、`bars`、`area` | PASS | PASS |
| `fill` 整屏缓冲（PSRAM），只算画图 | 81 fps | 约 11 fps（含填缓冲） |
| `fill -r 40`（SRAM），只算画图 | 57 fps | 约 21 fps（含填缓冲） |
| `fill -r 1`，只算画图 | 3 fps（每次约 0.56 ms） | — |
| `idle -t 10`、`idle -t 20` | 0 事件，PASS | — |

WR 时钟约 26.7 MHz 时一整帧（307200 字节）理论需 11.5 ms，DMA 整屏实测约 12.2 ms。
同时用 `mcu_i2s_test tone -t 8000 &` 放音时，`fill` 为 77 fps（整屏）、47 fps
（`-r 40`），I2S 用例 PASS。画面和触摸的人工确认结果见 `docs/bl616cl-display.md`。

## 限制

- 读不回画面，`LCDDEVIO_GETRUN`/`GETAREA` 返回 `-ENOTSUP`，画面正确性只能人看。
- 触摸坐标是芯片原始坐标，不随 `Rotation` 旋转。
- 触摸只报一个点。
