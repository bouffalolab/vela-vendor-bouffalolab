# BL616CL 头文件边界

本目录遵循 openvela 的公共/局部头文件划分；硬件定义复用
`drivers/soc/bl616cl/std/include/hardware/` 和 `drivers/lhal/`，不维护第二份寄存器全集。

## 目录职责

| 位置 | 内容与使用方 |
| --- | --- |
| `include/chip.h` | 架构公共代码需要的芯片能力定义；不引入芯片私有头文件。 |
| `include/irq.h` | NuttX IRQ 编号契约，通过架构中断头文件间接引用。 |
| `include/bl616cl_*.h` | 经评估确需对外提供的扩展契约；使用 `<arch/chip/文件名>`。 |
| 局部 `chip.h` | 芯片及架构代码的基础入口，保持精简并可被汇编包含。 |
| 局部 `bl616cl_<模块>.h` | 驱动间、板级初始化使用的接口；例如 UART 注册和时钟初始化。 |
| `hardware/bl616cl_memorymap.h` | 直接导入上游全部 SoC 地址，不重复添加前缀别名。 |
| `hardware/bl616cl_core.h` | 复用上游 T-Head 核心定义，计算需要的寄存器地址与字段偏移。 |
| `bl616cl_lhal.h` | 私有 LHAL/NuttX 兼容边界，隔离寄存器访问宏及 errno 冲突。 |
| `bl616cl_sdk.h` | 私有 SDK/NuttX 头文件兼容边界，导入真实类型并隔离名字冲突。 |

构建系统把整个 `include/` 映射到 `include/arch/chip`。对外可见不代表
所有函数都是用户态系统调用，也不赋予应用直接访问寄存器的权限。
公共头文件不得反向包含局部 `chip.h` 或 `hardware/`。

## 已保留的扩展

- `bl616cl_dma.h` 提供 BL616CL DMA 设备获取入口；传输使用 NuttX DMA 接口。
- `bl616cl_timer.h` 定义应用与驱动共同使用的分频 ioctl，保持现有命令值。
- `bl616cl_dma.h`、`bl616cl_pwm.h`、`bl616cl_timer.h` 中的测试扩展是标准接口
  不足以覆盖的契约，保留现有配置门控及数据结构，不混入公共 `chip.h`。
- `bl616cl_uart.h` 是板级注册接口，放在芯片局部目录。UART 应用测试通过
  termios 操作设备，不再通过私有头文件获取测试边界值。

## 硬件定义复用

外设实现按需包含上游 `*_reg.h`，直接使用其宏名，例如 `HBN_RSV0_OFFSET`。
不添加 `BL616CL_HBN_RSV0_OFFSET` 这种没有新语义的重命名，不为每个外设增加
仅转发一个 include 的包装文件。memorymap 合并 std/LHAL 两源的 60 个有效基址定义；
上游本身已有的 `BL616CL_*` 名称保持原样。

芯片适配代码包含 `bflb_*` 头前先包含 `bl616cl_lhal.h`；调用 std API 时先包含
`bl616cl_sdk.h`，再包含真实 std API 头。前者保存并恢复 NuttX 寄存器访问宏和
40 个与 newlib 不同的 errno 宏，后者另处理 SDK 的 `ERROR` 枚举冲突。
调用方直接使用上游 `GLB_*`、`HBN_*` 函数、类型和枚举，不手写函数 ABI 或
数值副本。芯片私有时钟配置使用 `bl616cl_clockconfig.h`，std 的
`bl616cl_clock.h` 保持普通 include，不使用跨目录路径。
LHAL 库仍使用自身 errno，I2C/SPI 在返回边界把捕获的 LHAL 超时码转换为
NuttX `ETIMEDOUT`，不能因为调用方恢复了 errno 就省略此转换。
`tools/check_bl616cl_errno.sh` 可在构建后检查 errno 定义和调用方包含顺序。

SoC 上游头文件包含 C 枚举，核心头文件包含 C 类型及内联函数，因此两个
`hardware/` 适配头均仅供 C 使用。`memorymap.h` 内的 `ARCH_RISCV` 用于满足
上游头文件的预处理条件，不通过公共头文件导出该依赖。

CORET 的 `MTIME` 地址采用 `offsetof(CORET_Type, MTIME)`；当前工具链下偏移为
`0x7ff8`。上游字段注释中的 `0x7ffc` 不能代替结构体实际布局。
MTimer 的目标频率属于时钟配置，定义在 `bl616cl_clockconfig.h`。

统一外设配置和 QFN48 引脚分配见
[板级说明](../../boards/bl616cl/ai-m64l-32s-kit/README.md)。

## IRQ 编号边界

LHAL 使用原始 CLIC 索引，NuttX 为异常保留前 16 个编号：

```text
LHAL raw IRQ -- bl616cl_irq_raw_to_nuttx() --> NuttX IRQ
            <-- bl616cl_irq_nuttx_to_raw() --
UART0: 44                                      60
MTIME:  7                                      23
```

`bl616cl_irq_adapter.c` 负责 LHAL 注册与回调处的转换；NuttX 的中断控制入口
在访问 CLIC 前反向转换。公共 `include/irq.h` 不引入 SDK 枚举；
`bl616cl_irq.c` 编译期检查全部 74 项映射和数量，覆盖 7 个 RV32 核心异步中断
及 67 个芯片槽位（含保留槽），防止上游变化后静默漂移。IRQ 的加 16 是真实
编号适配，因此它不同于地址或位域的纯重命名。

## 依据

- SDK 根目录 `docs/zh-cn/chip_porting/porting_guide.md` 第 7 节。
- SDK 根目录 `docs/zh-cn/chip_porting/Vendor.md` 的芯片目录说明。
- SDK 根目录 `nuttx/Documentation/reference/os/conventions.rst` 的头文件可见性规则。
- SDK 根目录 `nuttx/cmake/nuttx_generate_headers.cmake` 的实际导出路径。

上述文档明确公共/局部边界；本目录对测试扩展及上游硬件复用的安排是
BL616CL 的具体设计，不代表官方要求所有芯片使用相同文件清单。
