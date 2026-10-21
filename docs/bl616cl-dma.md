# BL616CL DMA0 通用适配与验证

> 当前统一验证配置为 `nsh-peripherals`，其中 DMA adapter 和测试 app 同时启用。
> 下文 `nsh-dma` 构建数字仅保留为历史裁剪证据。

## 背景

BL616CL LHAL 已提供 DMA0 CH0..CH7 和丰富的外设 request 表，但此前 OpenVela chip
适配没有 `dma_dev_s` 后端，`CONFIG_DMA` 也未进入产品配置。本次先建立公共
memory-to-memory one-shot 能力，为后续 UART、I2C、SPI、ADC、crypto 和 MTD consumer
提供可裁剪的 controller/channel 前置。2026-10-16 补上 memory-to-peripheral 和
peripheral-to-memory，首个 consumer 是 I2S0（见 [bl616cl-i2s.md](bl616cl-i2s.md)）。

## 最大能力交集

| 能力族 | 本轮结论 | 边界 |
|---|---|---|
| DMA0 CH0..CH7 | 纳入 | fixed-ident 独占申请、等待、释放和复用；外设通道固定分配，见“通道分配” |
| mem2mem one-shot | 纳入 | 1/2/4-byte 同宽，step=0 或 width，1..4095 units |
| mem2dev、dev2mem one-shot | 纳入 | DRQ 用 LHAL `DMA_REQUEST_*`，只放在外设一侧；外设方向 burst 固定 INCR1，DMA 做流控 |
| TC IRQ | 纳入 | raw IRQ 31 -> NuttX IRQ 47，共享 snapshot/clear |
| error IRQ 状态机 | 已实现，无测试覆盖 | error 优先、`-EIO`、residual；硬件 error 无法稳定触发 |
| stop/residual | 纳入受限能力 | byte residual；只测完成后 residual=0，传输中 stop 无测试，不声明线速取消 |
| callback 生命周期 | 纳入 | NULL callback 有测试；callback 内可对本通道 start 下一段（I2S 连续流依赖此行为并已实测）；callback 内 stop、put 拒绝、put 等待 callback 退出无测试 |
| cache ownership | 纳入合同 | client clean/invalidate；cached/non-cache alias 可见性 |
| pause/resume、cyclic | 延后 | mandatory op 稳定返回 `-ENOTSUP` |
| LLI/link/insert | 延后 | 首版依赖 `!DMA_LINK`，不进入 vtable |
| 外设 consumer | I2S0 已接入（默认 CH0/CH1，初始化时申请并一直持有），其它延后 | 必须逐 consumer 冻结 owner、request、pin 和实物信号 |

## 架构与调用链

```text
RISC-V up_initialize()
  -> riscv_dma_initialize()
  -> DMA0 clock + 8 channel table + shared IRQ

OpenVela DMA client
  -> bl616cl_dma0_device()
  -> DMA_GET_CHAN / DMA_CONFIG / DMA_START / DMA_STOP / DMA_RESIDUAL
  -> chips/bl616cl/bl616cl_dma.c
  -> LHAL DMA channel devices, start/stop/pending, clock wrapper
  -> BL616CL DMA0 registers (transfer programming, IRQ status)
```

通道设备（`dma0_ch0..7`）、通道启停、剩余长度查询和 width/burst 编码用 LHAL
`bflb_dma`。adapter 自行映射 OpenVela 与 LHAL 不同的 direction 编码，自己写单次传输的
地址和 control 寄存器（`bflb_dma_channel_init()` 会清掉 `DMA_I`，也不设 TransferSize
和地址；LLI 路径改变 4095 单元上限），并自行处理 TC/error 聚合 IRQ，不复用只处理 TC
且存在 detach 并发窗口的 LHAL callback ISR，也不调用 `bflb_dma_channel_irq_attach()`
（会覆盖同一 IRQ 上的 adapter ISR）。

## 配置与裁剪

| 选项 | 默认 | 作用 |
|---|---:|---|
| `CONFIG_DMA` | n | 打开 OpenVela generic DMA ABI，并选择 `ARCH_DMA` |
| `CONFIG_BL616CL_DMA0` | n | 编译 DMA0 adapter、LHAL DMA 和 clock 依赖 |
| `CONFIG_BL_MCU_PERIPHERAL_TESTS_DMA` | n | 编译单一测试命令 `mcu_dma_test` |

`BL616CL_DMA0` 依赖 `DMA && !DMA_LINK`。关闭时 adapter、`bflb_dma.c.o` 和测试 app
均不进入目标 archive。当前 `nsh-peripherals` 同时包含 adapter 和测试 app。

## 通道分配

DMA0 的 8 个通道按外设固定分配：一个通道只给一个外设，外设在初始化时申请并一直
持有，不在外设之间轮流借用。

| 通道 | 默认分配 | 配置项 |
|---|---|---|
| CH0 | I2S0 TX（打开 `BL616CL_I2S` 时） | `CONFIG_BL616CL_I2S_TX_DMA_CHANNEL` |
| CH1 | I2S0 RX（同上） | `CONFIG_BL616CL_I2S_RX_DMA_CHANNEL` |
| CH2–CH7 | 未分配 | — |
| 任意 | 应用代码（默认不用） | `CONFIG_BL616CL_DMA0_APP_CHANNELS` |

- 查看和修改：全部分配集中在 menuconfig 的 `BL616CL Peripheral Support` →
  `DMA0 channel assignment` 菜单里，一处能看到。SDK 驱动的通道各有一个选项；应用代码
  （SDK 驱动以外、自己调用 `DMA_GET_CHAN` 的代码）用到的通道填进 `Application
  code`，按位填写，bit n 对应 CHn，例如用 CH6、CH7 填 `0xc0`。
- 编译期检查：分配都在构建前确定，构建时检查，运行时不会重新分配。两个使用者配到同
  一个通道时构建失败，并指出是哪个通道，例如 `DMA0 CH1 is assigned more than once,
  see the DMA0 channel assignment menu`；应用通道超出 CH7 时报 `DMA0 application
  channels name a channel above CH7`。
- 已分配通道的掩码：`<arch/chip/bl616cl_dma.h>` 的 `BL616CL_DMA0_RESERVED` 包含 SDK
  驱动和应用代码的通道（bit n 对应 CHn）。不在菜单里的代码（如测试）只能用掩码以外
  的通道，`mcu_dma_test` 就按这个掩码跳过已分配的通道。
- 反初始化：使用者在初始化时申请通道，停流、空闲时都不归还。有反初始化的驱动在反
  初始化时归还，以便重新初始化时能再次申请；通道号仍由菜单固定，不会给别的使用者。
  I2S 驱动只在启动时注册一次，没有反初始化。
- 运行期冲突：按 NuttX 接口，`DMA_GET_CHAN` 在通道被占用时会一直等待（接口保证总是
  返回通道）。等待前驱动打印一条错误日志，例如：

  ```text
  DMA0 CH0 is held by code at 0x80039bdc (assigned to I2S0 TX); waiting, DMA0 channels are not shared
  ```

  地址是持有者调用 `DMA_GET_CHAN` 处的返回地址，用
  `riscv-none-elf-addr2line -f -e final_nuttx <地址>` 可以查到函数（上例为
  `bl616cl_i2s_dma_setup`）。出现这条日志说明两个模块用了同一个通道，改掉其中一个的
  通道号即可。括号里是菜单里的分配：SDK 驱动名、`application code`，或者
  `nothing`（菜单里没有分配这个通道，说明有代码用了通道却没有登记）。
  `mcu_dma_test` 的 DMA-007 会故意在已占用的通道上等待，打印两条这样的日志属正常。
- 新增使用 DMA0 的 SDK 驱动只改两处：在 `DMA0 channel assignment` 菜单里加通道选项；
  在 `bl616cl_dma.h` 里写一个登记块并挂进 `BL616CL_DMA0_ASSIGNMENTS`。掩码、编译期
  查重和运行时日志里的名字都由这张登记表生成，`bl616cl_dma.c` 不用改。例如 SPI0：

  ```c
  #ifdef CONFIG_BL616CL_SPI0_DMA
  #  define BL616CL_DMA0_SPI0(X, arg) \
       X(arg, CONFIG_BL616CL_SPI0_TX_DMA_CHANNEL, "SPI0 TX") \
       X(arg, CONFIG_BL616CL_SPI0_RX_DMA_CHANNEL, "SPI0 RX")
  #else
  #  define BL616CL_DMA0_SPI0(X, arg)
  #endif

  #define BL616CL_DMA0_ASSIGNMENTS(X, arg) \
    BL616CL_DMA0_I2S(X, arg) \
    BL616CL_DMA0_SPI0(X, arg)
  ```

## API 合同

1. `get_chan(dev, ident)` 只等待指定的 0..7 channel，不替换成其他空闲 channel；
   通道已被占用时，等待前打印一条错误日志（见“通道分配”）。
2. 每个 channel 状态按 FREE、OWNED、READY、RUNNING、terminal、RELEASING 流转；
   config 先完整校验再原子替换。
3. 只接受 `DMA_MEM_TO_MEM`、`DMA_MEM_TO_DEV`、`DMA_DEV_TO_MEM`，源/目的同宽
   1/2/4 byte，step=0 或 width。mem2mem 两侧 DRQ 都为 0；外设方向只在外设一侧填
   LHAL 请求号（0..63，0 是 UART0 RX），内存一侧为 0。`DMA_DEV_TO_DEV` 返回
   `-EINVAL`。
4. 非零 priority/timeout/option 以及 cyclic/pause/resume 返回 NuttX ABI 的
   `-ENOTSUP`；地址、长度、对齐和 step 错误返回 `-EINVAL`，4096 units 返回
   `-E2BIG`。
5. TC callback 返回原请求字节数；residual 单位为字节。error callback 返回 `-EIO`，
   residual 为未传输字节数（实现行为，当前无测试覆盖）。
6. ISR 先 snapshot 并 clear 全部 TC/error 位，再在锁外回调；同一 channel 同时出现
   TC/error 时 error 优先且只回调一次（实现行为，当前无测试覆盖）。
7. callback 运行时通道已是 COMPLETE 或 ERROR，可以在 callback 里对本通道
   `DMA_START` 下一段；put 先把通道置为 RELEASING，之后的 start 被拒绝。
   并发或重复 put 只 post 一次。task-context put 会等待 in-flight callback 退出再释放；
   在中断（含 callback）上下文调用 put 触发 `DEBUGASSERT`，未开断言时直接返回，不改变
   channel（这两点为实现行为，当前无测试覆盖）。
8. DMA 不自动维护 client buffer cache。启动前由 client clean producer，DMA 写入前及
   CPU 读取前由 client invalidate consumer，并保证 partial cache line ownership。

## 构建与裁剪结果

```bash
vendor/bouffalolab/vela build \
  bl616cl/ai-m64p-32s-kit/configs/nsh -j14
vendor/bouffalolab/vela build \
  bl616cl/ai-m64p-32s-kit/configs/nsh-peripherals -j14
vendor/bouffalolab/vela build \
  bl616cl/ai-m64p-32s-kit/configs/nsh -j14
```

| 配置 | 结果 | 归档/符号门禁 |
|---|---:|---|
| `nsh` | 1224/1224 | 无 adapter、LHAL DMA、test archive/symbol |
| `nsh-dma`（历史，已删除） | 1227/1227 | adapter + LHAL DMA；强 `riscv_dma_initialize`；无 test |
| `nsh` | 1229/1229 | 另含独立 test archive |

三态 whole image 都通过 4 MiB 布局和 MFG 擦除区校验。构建 warning 仅有既有的
critmonitor shadow、cpuload 未使用变量和无 MFG 输入提示，没有 DMA build error。

## 实板功能验证

以下为用例精简前固件（当时 8 项）的记录；当前 7 项版本的结果见本节末尾。
Ai-M64P-32S-Kit 使用 `/dev/ttyUSB2`、2000000 baud。测试固件 `nuttx.bin` SHA256：

```text
0d8eee354d532d3b6ca531ff786b18c927a548076a342b85eb15510637fcb3cf
```

烧录后设备端回读与 host 一致；复位后进入 `NuttShell (NSH) NuttX-3.6.1`。执行
`mcu_dma_test`，8 个 case 全部通过：

```text
[DMA-001] PASS ret=0
[DMA-002] PASS ret=0
[DMA-003] PASS ret=0
[DMA-004] PASS ret=0
[DMA-005] PASS ret=0
[DMA-006] PASS ret=0
[DMA-007] PASS ret=0
[DMA-008] PASS ret=0
DMA0 summary: passed=8 failed=0
```

当前用例（DMA-001..007）只用公共 DMA API，覆盖 1/2/4-byte width、固定/递增 step、
逐字节数据和越界保护区、4095/4096 units、无效请求原子性、八通道与共享 TC IRQ、
NULL callback、cache alias、owner 等待和并发 put。逐项流程和关键数据直接记录在
`apps/mcu_peripheral_tests/dma/README.md`。

同一固件继续完成 GPIO、timer、oneshot、WDT 非复位、RTC 和系统存活回归，未出现
panic、assert 或意外复位。GPIO 使用实际注册节点执行 edge smoke；没有把未接线的
GPIO loopback 当作回归前提。

当前 7 项版本于 2026-10-16 在 BL616CL-AVE-V10（`nsh-peripherals`，`nuttx.bin`
SHA256 `d512c38dc1cdc568663d87f989e522c5e92d28b7d9facf5472194b6d74f71fa5`）上执行：

```text
[DMA-001] PASS ret=0
[DMA-002] PASS ret=0
[DMA-003] PASS ret=0
[DMA-004] PASS ret=0
[DMA-005] PASS ret=0
[DMA-006] PASS ret=0
[DMA-007] PASS ret=0
DMA0 summary: passed=7 failed=0
```

2026-10-20 改为固定分配后：BL616CL-AVE-V10 打开 I2S，`mcu_dma_test` 跳过 CH0/CH1，
用其余 6 个通道 7 项全部 PASS，连跑两次，之后 I2S 用例照常 PASS；Ai-M64P-32S-Kit
不开 I2S，`mcu_dma_test` 用 8 个通道 7 项全部 PASS，同样连跑两次。在 AVE-V10 上用
未适配的旧测试申请 CH0 时，打印 `DMA0 CH0 is held by code at ... (assigned to I2S0
TX)` 后一直等待，地址经 addr2line 指向 `bl616cl_i2s_dma_setup`。外设方向的数据
传输见 [bl616cl-i2s.md](bl616cl-i2s.md)。

同日改为登记表并加入应用通道后：在 AVE-V10 上把 I2S RX 也配到 CH0、把应用通道配成
`0x2`（与 I2S RX 的 CH1 重叠）、配成 `0x100`，三次构建都失败，分别报 CH0 重复、CH1
重复和超出 CH7。应用通道配成 `0x80` 时，`mcu_dma_test` 跳过 `0x83`，用其余 5 个通道
7 项全部 PASS，I2S FINAL 用例照常 PASS。

## 限制与后续入口

- USB2 是命令和日志通道，不是 DMA 数据通道；数据、IRQ 和状态由本地断言确认。
- error IRQ 路径没有测试覆盖；需要可重复的硬件总线错误触发方法后再补。
- 传输中 stop/residual、put 等待 in-flight callback 没有测试覆盖；后续需要可观测的
  长传输或外设流。
- UART/I2C/SPI/ADC/MTD/crypto consumer 必须建立独立 Kconfig、owner、cache 和实物
  验证闭环，不能因 mem2mem 或 I2S 通过而宣称已支持。
- DMA_LINK、LLI、cyclic、pause/resume 需要单独解决 pool bounds、取消和进度语义。
