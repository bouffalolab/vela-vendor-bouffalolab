# display 组件

Bouffalo SDK 统一显示框架（`bsp/common/display`）和触摸芯片驱动（`bsp/common/touch`）
在 openvela 上的移植。功能、Kconfig、板级接法和验证结果见
`docs/bl616cl-display.md`，本文件只说明这个目录里的代码从哪来、和 SDK 有什么不同，
以及怎样从 SDK 同步。

## 基线

| 目录 | SDK 来源 | 基线 |
|---|---|---|
| `components/display/`（除 `nuttx/`、`touch/`） | `bsp/common/display` | bouffalo_sdk `d76428a8`，该目录最后一次修改 `148a7e19` |
| `components/display/touch/` | `bsp/common/touch` 的 `gt911_i2c.c`、`ft6x36_i2c.c` | 同上，该目录最后一次修改 `d1e41158` |
| `chips/bl616cl/bl616cl_dbi.c/.h` | `bsp/common/display/bus/dbi` | 同上 |
| `chips/bl616cl/bl616cl_display_spi.c/.h` | `bsp/common/display/bus/spi` | 同上 |

GitHub 发布版 v2.3.39 还没有这一版的 `bus/dbi`、`bus/spi`，对照时以上面的提交为准。

## 原样带入的文件

`display.c/.h`、`display_types.h`、`bus/display_bus.c/.h`、`bus/display_bus_driver.h`、
`bus/display_io.c/.h`、`bus/soft_dbi_c/`、`panel/`（DCS 通用驱动、24 款屏的描述、
`video_standard`、`display_board_gpio`）、`scanout/display_scanout.c/.h`、`simple_font/`
（字库数据为 SIL OFL 1.1，许可见 `simple_font/tools/LICENSE.OFL`）与 SDK 逐字节相同。

`display_os.h` 只加了一处：NuttX 下的信号量类型（`sem_t`）。

没有带入：`default/`（SDK 按 Kconfig 选屏的默认显示实例，Vela 由板级代码定义总线和屏）、
`bus/dsi`、`scanout/bl618dg_dpu`、`scanout/sim_v1`（BL618DG 和模拟器用）、
`panel/template`、SDK 的 CMake/Kconfig 和 README；`bsp/common/touch` 的 `touch.c`
（SDK 的触摸分发层，Vela 改接 NuttX touchscreen）。

## 与 SDK 的结构差异

逻辑照 SDK，下面是只因 openvela 结构不同而改的地方：

- **平台层**：`nuttx/display_os_nuttx.c` 替代 SDK 的 `display_os.c`，信号量用 `nxsem`，
  关中断用 `enter_critical_section()`，cache 用 `up_clean_dcache()`，延时在调度器起来前和
  中断里用 `up_mdelay()`，其余用 `nxsig_usleep()`。GPIO 两个函数在
  `chips/bl616cl/bl616cl_display.c`。
- **用到 LHAL 的代码放在 chips**：LHAL 头文件会引入工具链 newlib 的 `errno` 定义，
  与 NuttX 不同的值（如 `ENOTSUP` 134/138、`ETIMEDOUT` 116/110）会在同一个编译单元里
  静默覆盖 NuttX 的值。所以 DBI、SPI 总线和 GPIO 放在 chips，先包含
  `bl616cl_lhal.h`；`bus/soft_dbi_c/display_soft_dbi_c.c` 文件不改，由
  `chips/bl616cl/CMakeLists.txt` 强制预包含 `bl616cl_lhal.h` 后编进 arch。
- **DMA**：SDK 的 `dma_mgr` 动态分配改为 Kconfig 固定通道（`DMA0 channel assignment`
  菜单，总线初始化时取得后一直持有）；LHAL DMA 调用改为 NuttX DMA API，一次
  `DMA_START()` 完成装载和启动；LLI 节点由 DMA 适配层的共享池提供；burst 经
  `dma_config_s.option` 选择（DBI INCR8/INCR4，SPI INCR4，与 SDK 相同）。
  SPI 总线的完成由 DMA 回调代替 SDK 的 DMA 中断；NuttX DMA API 在 DMA 出错时也调用
  这个回调，总线用错误码结束这次传输。
- **中断**：DBI 中断用 `irq_attach()`。
- **上层**：`nuttx/display_lcddev.c` 把显示接成 NuttX `lcd_dev_s`（`/dev/lcd0`，可选
  `/dev/fb0`）；不连续或未对齐的输入先拷到约 4 KiB 的暂存缓冲再交给框架。
- **触摸**：`touch/` 里 GT911、FT6x36 的寄存器读写照 SDK，I2C 改用 NuttX
  `I2C_TRANSFER()`，频率从 SDK 的 200 kHz 改为 100 kHz（BL616CL I2C 驱动只支持
  100/400 kHz）；`touch_poll.c` 在 LPWORK 里定时读一个点，按 NuttX touchscreen 报
  DOWN/MOVE/UP，坐标原样输出。

## 从 SDK 同步

1. 对照上面的基线提交，看 SDK 这几个目录的改动。
2. 原样带入的文件直接覆盖；`display_os.h` 保留 NuttX 信号量那一处。
3. `bus/dbi`、`bus/spi`、`display_os.c`、触摸驱动的改动，按同样的逻辑改到 chips 和
   `nuttx/`、`touch/` 对应位置。
4. 新增屏：在 `Kconfig` 加 `BL_COMPONENT_DISPLAY_PANEL_<IC>`，在 `CMakeLists.txt`
   的屏列表里加一项，再由板级模组选项 select。
5. 更新本文件的基线。
