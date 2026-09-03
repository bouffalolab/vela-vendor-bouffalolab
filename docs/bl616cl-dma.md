# BL616CL DMA0 通用适配与验证

> `nsh-dma` 是唯一正式 DMA 配置。测试 hook 使用临时派生配置，不进入长期 board configs。

## 背景

BL616CL LHAL 已提供 DMA0 CH0..CH7 和丰富的外设 request 表，但此前 OpenVela chip
适配没有 `dma_dev_s` 后端，`CONFIG_DMA` 也未进入产品配置。本次先建立公共
memory-to-memory one-shot 能力，为后续 UART、I2C、SPI、ADC、crypto 和 MTD consumer
提供可裁剪的 controller/channel 前置。

## 最大能力交集

| 能力族 | 本轮结论 | 边界 |
|---|---|---|
| DMA0 CH0..CH7 | 纳入 | fixed-ident 独占申请、等待、释放和复用 |
| mem2mem one-shot | 纳入 | 1/2/4-byte 同宽，step=0 或 width，1..4095 units |
| TC IRQ | 纳入 | raw IRQ 31 -> NuttX IRQ 47，共享 snapshot/clear |
| error IRQ 状态机 | 纳入软件合同 | error 优先、`-EIO`、residual；无硬件注入结论 |
| stop/residual | 纳入受限能力 | byte residual；实测为 pre-enable stop，不声明线速取消 |
| callback 生命周期 | 纳入 | NULL callback、callback 内 stop、put guard、drain |
| cache ownership | 纳入合同 | client clean/invalidate；cached/non-cache alias 可见性 |
| pause/resume、cyclic | 延后 | mandatory op 稳定返回 `-ENOTSUP` |
| LLI/link/insert | 延后 | 首版依赖 `!DMA_LINK`，不进入 vtable |
| 外设 consumer | 延后 | 必须逐 consumer 冻结 owner、request、pin 和实物信号 |

## 架构与调用链

```text
RISC-V up_initialize()
  -> riscv_dma_initialize()
  -> DMA0 clock + 8 channel table + shared IRQ

OpenVela DMA client
  -> bl616cl_dma0_device()
  -> DMA_GET_CHAN / DMA_CONFIG / DMA_START / DMA_STOP / DMA_RESIDUAL
  -> chips/bl616cl/bl616cl_dma.c
  -> BL616CL DMA0 registers and LHAL clock wrapper
```

adapter 自行映射 OpenVela 与 LHAL 不同的 direction/width 编码，并自行处理 TC/error
聚合 IRQ，不复用只处理 TC 且存在 detach 并发窗口的 LHAL callback ISR。

## 配置与裁剪

| 选项 | 默认 | 作用 |
|---|---:|---|
| `CONFIG_DMA` | n | 打开 OpenVela generic DMA ABI，并选择 `ARCH_DMA` |
| `CONFIG_BL616CL_DMA0` | n | 编译 DMA0 adapter、LHAL DMA 和 clock 依赖 |
| `CONFIG_BL616CL_DMA0_TEST` | n | 打开 IRQ 注入、pre-enable hold 和统计 hook |
| `CONFIG_BL_MCU_PERIPHERAL_TESTS_DMA` | n | 编译单一测试命令 `mcu_dma_test` |

`BL616CL_DMA0` 依赖 `DMA && !DMA_LINK`。关闭时 adapter、`bflb_dma.c.o` 和测试 app
均不进入目标 archive。产品配置 `nsh-dma` 不包含 test hook；只有 `nsh`
包含测试能力。

## API 合同

1. `get_chan(dev, ident)` 只等待指定的 0..7 channel，不替换成其他空闲 channel。
2. 每个 channel 状态按 FREE、OWNED、READY、RUNNING、terminal、RELEASING 流转；
   config 先完整校验再原子替换。
3. 只接受 `DMA_MEM_TO_MEM`、源/目的同宽 1/2/4 byte、step=0 或 width、DRQ=0。
4. 非零 priority/timeout/option 以及 cyclic/pause/resume 返回 NuttX ABI 的
   `-ENOTSUP`；地址、长度、对齐和 step 错误返回 `-EINVAL`，4096 units 返回
   `-E2BIG`。
5. TC callback 返回原请求字节数；error callback 返回 `-EIO`；residual 单位为字节。
6. ISR 先 snapshot 并 clear 全部 TC/error 位，再在锁外回调；同一 channel 同时出现
   TC/error 时 error 优先且只回调一次。
7. task-context put 会等待 in-flight callback 退出再释放；并发或重复 put 只 post 一次。
   ISR/callback context 的 put 由运行时 guard 无副作用拒绝。
8. DMA 不自动维护 client buffer cache。启动前由 client clean producer，DMA 写入前及
   CPU 读取前由 client invalidate consumer，并保证 partial cache line ownership。

## 构建与裁剪结果

```bash
vendor/bouffalolab/vela build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
vendor/bouffalolab/vela build \
  bl616cl/ai-m64l-32s-kit/configs/nsh-dma -j14
vendor/bouffalolab/vela build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

| 配置 | 结果 | 归档/符号门禁 |
|---|---:|---|
| `nsh` | 1224/1224 | 无 adapter、LHAL DMA、test archive/symbol |
| `nsh-dma` | 1227/1227 | adapter + LHAL DMA；强 `riscv_dma_initialize`；无 test |
| `nsh` | 1229/1229 | 另含独立 test archive 和 test hook |

三态 whole image 都通过 4 MiB 布局和 MFG 擦除区校验。构建 warning 仅有既有的
critmonitor shadow、cpuload 未使用变量和无 MFG 输入提示，没有 DMA build error。

## 实板功能验证

Ai-M64L-32S-Kit 使用 `/dev/ttyUSB2`、2000000 baud。测试固件 `nuttx.bin` SHA256：

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

运行断言覆盖 1/2/4-byte width、固定/递增 step、逐字节数据、4095/4096 units、
无效请求原子性、八通道、TC IRQ、NULL callback、error 优先软件合同、stop/residual、
cache alias、owner 等待、并发 put 和 callback drain。逐项流程和关键数据直接记录在
`apps/mcu_peripheral_tests/dma/README.md`。

同一固件继续完成 GPIO、timer、oneshot、WDT 非复位、RTC 和系统存活回归，未出现
panic、assert 或意外复位。GPIO 使用实际注册节点执行 edge smoke；没有把未接线的
GPIO loopback 当作回归前提。

## 限制与后续入口

- USB2 是命令和日志通道，不是 DMA 数据通道；数据、IRQ 和状态由本地断言确认。
- 软件 IRQ 注入不提升为硬件 error 证据；后续需要可重复的总线错误注入方法。
- pre-enable hold 不提升为线速 stop 证据；后续需要可观测长传输或外设流。
- UART/I2C/SPI/ADC/MTD/crypto consumer 必须建立独立 Kconfig、owner、cache 和实物
  验证闭环，不能因公共 mem2mem 通过而宣称已支持。
- DMA_LINK、LLI、cyclic、pause/resume 需要单独解决 pool bounds、取消和进度语义。
