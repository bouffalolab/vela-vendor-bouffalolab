# display 组件

Bouffalo SDK 统一显示框架（`bsp/common/display`）和触摸芯片驱动（`bsp/common/touch`）
在 openvela 上的移植。功能、Kconfig、板级接法和验证结果见
`docs/bl616cl-display.md`，本文件只说明这个目录里的代码从哪来、和 SDK 有什么不同，
以及怎样从 SDK 同步。

## 基线与同步清单

基线是 bouffalo_sdk v2.3.39：复制和改写的文件都和 v2.3.39 相同，只有 `bus/dbi`、
`bus/spi`（以及没用到的 README、`default/Kconfig`、`scanout/sim_v1`）取自 v2.3.39 之后的
SDK 版本，v2.3.39 里找不到这几个版本，以后的发布版会带上。

`sdk_sync.txt` 逐个列出 SDK 这两个目录下的 129 个文件：上次同步时的 git blob id（按文件
内容计算，和取自哪个提交无关），以及移植时怎么处理的。以它为准，不以某个提交为准。

| 方式 | 文件数 | 含义 |
|---|---|---|
| `same` | 77 | 原样复制到本目录，与 SDK 逐字节相同 |
| `patched` | 1 | 原样复制后有 Vela 改动：`display_os.h` 加了 NuttX 信号量类型 |
| `ported` | 21 | 逻辑搬进了 Vela 自己的文件（清单最后一列），如 `bus/dbi/display_dbi.c` → `chips/bl616cl/bl616cl_dbi.c`，SDK 的 CMake/Kconfig → 本目录与 chips 的 CMake/Kconfig |
| `skipped` | 30 | 没有用到：`default/`、`bus/dsi`、`scanout/bl618dg_dpu`、`scanout/sim_v1`、`panel/template`、README、`touch_conf.h` |

清单由 `tools/bl616cl/display_sdk_sync.py` 维护。

## 原样带入的文件

`display.c/.h`、`display_types.h`、`bus/display_bus.c/.h`、`bus/display_bus_driver.h`、
`bus/display_io.c/.h`、`bus/soft_dbi_c/`、`panel/`（DCS 通用驱动、24 款屏的描述、
`video_standard`、`display_board_gpio`）、`scanout/display_scanout.c/.h`、`simple_font/`
（字库数据为 SIL OFL 1.1，许可见 `simple_font/tools/LICENSE.OFL`）与 SDK 逐字节相同。

`display_os.h` 只加了一处：NuttX 下的信号量类型（`sem_t`）。

没有带入：`default/`（SDK 按 Kconfig 选屏的默认显示实例，Vela 由板级代码定义总线和屏）、
`bus/dsi`、`scanout/bl618dg_dpu`、`scanout/sim_v1`（BL618DG 和模拟器用）、
`panel/template`、SDK 的 README 和 `touch_conf.h`。SDK 的 CMake/Kconfig、DBI 与 SPI
总线、`display_os.c`、触摸的 `touch.c` 与两个芯片驱动按 Vela 结构改写，见下一节。

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
  SPI 总线的完成由 DMA 回调代替 SDK 的 DMA 中断（DMA 出错的处理见下一节）。
- **中断**：DBI 中断用 `irq_attach()`。
- **上层**：`nuttx/display_lcddev.c` 把显示接成 NuttX `lcd_dev_s`（`/dev/lcd0`，可选
  `/dev/fb0`）；不连续、未对齐或长度不是整 4 字节的输入先拷到约 4 KiB 的暂存缓冲再交给框架（总线 DMA
按 32 位字读，直接发会读过调用方缓冲的末尾），暂存缓冲从拷贝到传输完成一直加锁。
- **触摸**：`touch/` 里 GT911、FT6x36 的寄存器读写照 SDK（读点的差异见下一节），I2C 改用 NuttX
  `I2C_TRANSFER()`，频率从 SDK 的 200 kHz 改为 100 kHz（BL616CL I2C 驱动只支持
  100/400 kHz）；`touch_poll.c` 在 LPWORK 里定时读一个点，按 NuttX touchscreen 报
  DOWN/MOVE/UP，坐标原样输出，替代 SDK 的触摸分发层 `touch.c`。

## 和 SDK 逻辑不同的地方

只因结构不同而改的地方见上一节。下面几处是逻辑上和 SDK 不同的，同步 SDK 时要保留，
直到 SDK 自己改好同一处：

- **GT911 读点**（`touch/touch_gt911.c`）：SDK 把整个状态字节当点数，读点数据也不检查
  结果，抬手时那帧（数据就绪、0 个点）会被当成 1 个点，读点失败会报 (0,0)。这里只认数据
  就绪的帧，点数取低 4 位，0 个点即抬手；没有新帧或读失败时返回错误，触摸状态不变。
- **FT6x36 读点**（`touch/touch_ft6x36.c`）：读点数据失败返回错误，不报点。
- **SPI 总线的 DMA 出错**（`chips/bl616cl/bl616cl_display_spi.c`）：SDK 没有 DMA 出错
  路径；这里 NuttX DMA 回调带错误码时，用它结束这次传输。和 SDK 一样，只有异步刷新拿得到
  这个错误，同步刷新仍返回 0。

这几处在代码里用 `Vela:` 注释标出，并在 `sdk_sync.txt` 对应行下面缩进写了一行说明。SDK
这几个文件有更新时，`status` 和 `update` 会把说明和 SDK 的变化一起打印出来。以后新增
和 SDK 逻辑不同的改动，同样三处都写；SDK 改好之后，三处一起删掉。

## 从 SDK 同步

工具只读 SDK 仓库的 git 对象，不碰它的工作区；`--sdk` 指向 SDK 仓库，`--rev` 是要同步到的
提交或 tag。

1. 看改了什么：

   ```bash
   tools/bl616cl/display_sdk_sync.py status --sdk <SDK 仓库> --rev <提交或 tag>
   ```

   按方式列出变化的文件；`ported` 文件给出 SDK 新旧两版的 `git diff` 命令和要改的 Vela
   文件；SDK 新增或删除的文件单独列出。
2. 同步：

   ```bash
   tools/bl616cl/display_sdk_sync.py update --sdk <SDK 仓库> --rev <提交或 tag>
   ```

   写入前先检查全部文件：`same` 文件被本地改过、要合并的文件不在、有文件还留着冲突标记
   或上次留下的活没做完时，什么都不写。`same` 文件直接换成新版；`patched` 文件用
   `git merge-file` 三方合并（以上次同步的 SDK 版本为基准，保留 Vela 改动）；`skipped`
   只更新 blob id。合并有冲突的 `patched` 文件和有变化的 `ported` 文件留给手工处理：
   清单里这一行记成“旧 blob..新 blob”，即这次要同步到的版本。
3. 手工处理留下的文件：解决冲突标记；按 `status` 给出的 diff，把 `ported` 文件的改动按
   同样逻辑改到 Vela 文件里，结构差异见上文。打印出的“Vela change to keep”要保留。全部
   做完后运行：

   ```bash
   tools/bl616cl/display_sdk_sync.py resolve
   ```

   它把这些行改成记下的那个新 blob（不是 SDK 之后又更新到的版本）；这些行对应的 Vela 文件
   有一个不在或还留着冲突标记时，什么都不改。然后再运行一次第 2 步的 `update`：没有剩下
   的事时它更新清单开头的 `synced` 行；如果 SDK 在这期间又有更新，会接着同步。
4. SDK 新增的文件：判断属于哪种方式，在 `sdk_sync.txt` 里加一行（新增屏另外在
   `Kconfig` 加 `BL_COMPONENT_DISPLAY_PANEL_<IC>`、在 `CMakeLists.txt` 屏列表加一项，再由
   板级模组选项 select）；删除的文件同步删掉对应行和 Vela 文件。
5. 构建、实板验证后把代码和 `sdk_sync.txt` 一起提交，`status` 应输出
   `no change since the last sync`。

`display_sdk_sync.py check` 不需要 SDK，检查 `same` 文件没有被改过、`patched` 和 `ported` 的
Vela 文件没有留下冲突标记、没有没做完的行、清单里的 Vela 文件都在；改 `same` 文件前先在清单里把它改成 `patched`。工具
自己的测试是 `tools/bl616cl/display_sdk_sync_test.py`，改工具后运行。
