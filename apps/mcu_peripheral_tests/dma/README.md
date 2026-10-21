# BL616CL DMA0 测试说明

## 背景

本目录只有一个测试入口 `mcu_dma_test`。测试程序通过 OpenVela 通用
`dma_dev_s` API 验证 BL616CL DMA0 的公共适配，而不是直接调用 LHAL。测试编译为
独立的 `libapps_mcu_dma_test.a`；chip adapter `bl616cl_dma.c.o` 进入
`libarch.a`，LHAL `bflb_dma.c.o` 进入 `libbl_lhal.a`。

BL616CL DMA0 有 8 个 channel，共用一个 raw IRQ 31（NuttX IRQ 47）。adapter 支持
memory-to-memory 和 memory-to-peripheral、peripheral-to-memory one-shot：1/2/4-byte
同宽访问，源和目的 step 只能是 0 或 width，单次最多 4095 transfer units。本测试只做
mem2mem 数据传输和外设方向的参数校验；外设方向的实际传输由 I2S 播放和录音
（`mcu_i2s_test`）覆盖。pause/resume、cyclic、LLI/link/insert 和硬件 error 注入不属于
本轮能力。

## 配置与构建

| 配置 | DMA0 adapter | test app | clean build |
|---|---:|---:|---:|
| `nsh` | n | n | 1224/1224 |
| `nsh-dma`（历史，已删除） | y | n | 1227/1227 |
| `nsh` | y | y | 1229/1229 |

在 SDK 根目录依次执行：

```bash
vendor/bouffalolab/vela clean \
  bl616cl/ai-m64p-32s-kit/configs/nsh
vendor/bouffalolab/vela build \
  bl616cl/ai-m64p-32s-kit/configs/nsh -j14

vendor/bouffalolab/vela clean \
  bl616cl/ai-m64p-32s-kit/configs/nsh-peripherals
vendor/bouffalolab/vela build \
  bl616cl/ai-m64p-32s-kit/configs/nsh-peripherals -j14

vendor/bouffalolab/vela clean \
  bl616cl/ai-m64p-32s-kit/configs/nsh
vendor/bouffalolab/vela build \
  bl616cl/ai-m64p-32s-kit/configs/nsh -j14
```

裁剪检查结果：

- 关闭态没有 `bl616cl_dma.c.o`、`bflb_dma.c.o`、DMA test archive 或 adapter/test
  符号。
- 产品态包含 `bl616cl_dma.c.o`、`bflb_dma.c.o`，强符号
  `riscv_dma_initialize` 的类型为 `T`，不包含 test app。
- 测试态额外包含 `libapps_mcu_dma_test.a`、`dma_test_main.c.o`。
- 三态 whole image 均为 4 MiB，boot2、双 partition、app 和 MFG 擦除区校验通过。

## 运行命令与总流程

测试固件启动到 NSH 后执行：

```text
mcu_dma_test
```

程序按 DMA-001 到 DMA-007 顺序执行（编号 DMA-008 不再使用）。每个 case 自己申请
固定 channel、配置、启动、等待完成并释放；任一断言失败会打印该 case 的负 errno，但
仍继续运行其余 case，最终以 `DMA0 summary` 汇总。完成判据是七项均打印
`PASS ret=0`，汇总为 `passed=7 failed=0`，且没有 panic、assert 或意外复位。

DMA0 通道按使用者固定分配（见 `docs/bl616cl-dma.md` 的“通道分配”），分配给 SDK
驱动或应用代码的通道一直被持有。测试启动时按 `<arch/chip/bl616cl_dma.h>` 的
`BL616CL_DMA0_RESERVED` 跳过这些通道，把其余通道按编号排成空闲通道表，下文的“第 n
个空闲通道”即这张表的第 n 项，并打印 `DMA0 test: <n> free channels, skipping
assigned 0x..`。Ai-M64P-32S-Kit 不开 I2S，空闲通道是 CH0–CH7；BL616CL-AVE-V10 打开
I2S，跳过 CH0/CH1，空闲通道是 CH2–CH7。空闲通道少于 5 个时测试直接失败。

测试只使用 OpenVela 公共 DMA API 和 `<arch/chip/bl616cl_dma.h>` 的
`bl616cl_dma0_device()`，判据只来自公开可观察结果：`DMA_START`/`DMA_CONFIG` 返回值、
callback 次数与 result、`DMA_RESIDUAL`、目的 buffer 逐字节比较，以及
`DMA_GET_CHAN`/`DMA_PUT_CHAN` 的返回值与阻塞行为。目的 buffer 后紧跟 32 字节保护区，
传输后保护区必须保持原值，用来发现越界写入。

cache maintenance 由 DMA client 负责：源 buffer 在启动前 clean；目的 buffer 和保护区
在 DMA 前 flush（clean + invalidate，使初值落到内存），CPU 读取前 invalidate。测试
buffer 按 32 字节对齐。这里验证的是调用方履行该协议后的 cached/non-cache alias
可见性，不宣称 DMA adapter 自动维护 cache，也不宣称未 clean 的 dirty cache 数据会被
保护。

各节「实测关键数据」与文末汇总来自用例精简前的固件（当时 8 项）；输出格式未变的行
予以保留。现行 7 项于 2026-10-16 在 BL616CL-AVE-V10 上全部 PASS（`passed=7 failed=0`）；
2026-10-20 改为按空闲通道运行后，在 AVE-V10（6 个通道）和 Ai-M64P-32S-Kit（8 个通道）
上各连跑两次，均为 `passed=7 failed=0`。记录见 `docs/bl616cl-dma.md`。

## DMA-001：width、step、数据和 TC 回调

流程：

1. 申请第 1 个空闲通道，分别配置 1、2、4-byte 同宽传输，源/目的 step 均为 width。
2. 每轮写入不同逐字节 pattern，clean 源、flush 目的和保护区，再启动 256 units。
3. 等待 TC callback，要求 callback 恰好一次，返回长度等于请求字节数，residual=0。
4. invalidate 目的并逐字节比较源/目的，保护区不得改变。
5. 再配置 1-byte、`src_step=0`、`dst_step=1`；源 128 字节互不相同，要求目的 128 字节
   全部等于源首字节，保护区不变。
6. 释放 channel。

实测关键数据：

```text
width=1 step=width ret=0 callbacks=1 residual=0
width=2 step=width ret=0 callbacks=1 residual=0
width=4 step=width ret=0 callbacks=1 residual=0
width=1 src_step=0 ret=0 callbacks=1
[DMA-001] PASS ret=0
```

## DMA-002：TransferSize 上限

流程：

1. 申请第 2 个空闲通道，配置 4-byte width 和递增 step。
2. 传输 `4095 * 4` 字节，等待一次 TC callback（result 为请求字节数、residual=0），
   逐字节比较并检查保护区。
3. 在相同有效配置下请求 `4096 * 4` 字节；必须在写硬件前返回 `-E2BIG`。
4. 释放 channel。

实测关键数据：

```text
units=4095 ret=0 callbacks=1 data=match; units=4096 ret=-7
[DMA-002] PASS ret=0
```

## DMA-003：无效请求原子拒绝

流程：

1. 申请第 3 个空闲通道，先安装一份有效的 4-byte mem2mem 配置。
2. 依次提交负 step、不支持的 dev2dev 方向、源/目的 width 不同、mem2mem 带非零
   DRQ、mem2dev 把 DRQ 放在内存侧、dev2mem 请求号超过 63；均须返回 `-EINVAL`。
   mem2dev、dev2mem 本身是合法方向，实际外设传输由 I2S 播放和录音覆盖。
3. 依次提交非零 priority、timeout、option；首版不支持，必须返回 NuttX ABI 的
   `-ENOTSUP=-138`。
4. 对保留的有效配置依次提交零长度、长度非 width 整数倍、源未对齐、目的未对齐和
   地址末端溢出；均须返回 `-EINVAL`。
5. 调用 cyclic、pause、resume；均须返回 `-ENOTSUP=-138`。
6. 要求 pre-start residual=0、失败位图 `contract=0x00000`，证明每个 errno 都符合合同。
7. 使用最初的有效配置完成 64 字节传输并比较数据和保护区，证明失败请求没有污染旧
   配置。

实测关键数据：

```text
config invalid=-22/-22/-22/-22/-22/-22 unsupported=-138/-138/-138
start invalid=-22/-22/-22/-22/-22 ops=-138/-138/-138 retained=0 residual=0 contract=0x00000
[DMA-003] PASS ret=0
```

## DMA-004：全部空闲通道与共享 IRQ

流程：

1. 申请全部空闲通道（M64P 8 个，AVE-V10 6 个），每个 channel 使用独立源、目的和
   pattern。
2. 全部配置为 1-byte 递增 mem2mem，再依次启动 128 字节传输。
3. 每个 channel 都必须收到且只收到一次 callback，返回长度为 128，residual=0。
4. 对每组目的 buffer 做 invalidate 和逐字节比较。
5. 释放全部 channel。该 case 只证明资源和共享 IRQ 分发，不作为并行带宽数据。

实测关键数据：

```text
channels=8 ret=0 (resource/IRQ dispatch evidence, not bandwidth)
[DMA-004] PASS ret=0
```

AVE-V10 上为 `channels=6`。

## DMA-005：NULL callback 完成

流程：

1. 申请第 4 个空闲通道，配置 1-byte 递增 mem2mem，以 NULL callback 启动 128 字节。
2. 轮询 `DMA_RESIDUAL` 直到为 0，超时 2 s。
3. 轮询 `DMA_CONFIG` 直到不再返回 `-EBUSY`，并要求返回 0。channel 离开 RUNNING
   说明中断已处理完成，且没有调用 NULL callback。
4. 逐字节比较目的数据并检查保护区，释放 channel。

## DMA-006：cache alias

流程：

1. 申请第 5 个空闲通道，确认源、目的和保护区位于 cacheable OCRAM，计算目的 buffer 的
   non-cache alias。
2. clean cached 源，flush 目的和保护区，以 cached 源 → non-cache 目的 alias 启动
   128 字节传输。
3. 要求 callback 恰好一次、result=128、residual=0。
4. invalidate 后通过 cached alias 比较数据并检查保护区，释放 channel。

## DMA-007：owner 等待、耗尽和并发 put

流程：

1. 持有全部空闲通道。
2. 启动一个线程再次申请最后一个空闲通道；等待 20 ms 后它必须仍阻塞。
3. 释放这个通道；等待线程必须被唤醒并获得同一 fixed-ident channel，再释放全部资源。
4. 持有倒数第二个空闲通道，同时启动两个线程对同一 handle 执行 put；两线程都必须
   返回，但 available semaphore 只能 post 一次。
5. 再持有这个通道，启动 waiter；它必须在持有期间阻塞，释放后获得同一 handle。

第 2、5 步的等待会让驱动打印通道被占用的错误日志（见 `docs/bl616cl-dma.md`），是
本用例预期的输出。实测关键数据（AVE-V10）：

```text
DMA0 CH7 is held by code at 0x8006ad58 (assigned to nothing); waiting, DMA0 channels are not shared
DMA0 CH6 is held by code at 0x8006af80 (assigned to nothing); waiting, DMA0 channels are not shared
  extra-waiter=blocked/woken concurrent-put=single-post ret=0
[DMA-007] PASS ret=0
```

## 最终实测汇总

以下为用例精简前固件的记录。Ai-M64P-32S-Kit 使用 `/dev/ttyUSB2`、2000000 baud。
测试固件 `nuttx.bin` SHA256 为
`0d8eee354d532d3b6ca531ff786b18c927a548076a342b85eb15510637fcb3cf`，烧录后设备端
回读与 host 一致。标准复位后出现 `NuttShell (NSH) NuttX-3.6.1`。

```text
DMA0 test: USB2 is command/log transport; DMA assertions are local
[DMA-001] PASS ret=0
[DMA-002] PASS ret=0
[DMA-003] PASS ret=0
[DMA-004] PASS ret=0
[DMA-005] PASS ret=0
[DMA-006] PASS ret=0
[DMA-007] PASS ret=0
[DMA-008] PASS ret=0
DMA0 summary: passed=8 failed=0
nsh>
```

同一固件继续回归：GPIO edge 三周期、TIMER-005、oneshot 100000 us 和 WDT-003
全部通过；RTC 在 sleep 2 秒后从 00:00:03 推进到 00:00:05；最终 `uptime`、`ps`
和 alive 标记均返回 NSH。未执行会触发复位的 WDT case。

## 限制

- 只支持 DMA0 one-shot；外设方向目前只有 I2S0 一个 consumer（TX、RX 各一个通道，
  初始化后一直持有）。
- pause/resume、cyclic、LLI/link/insert 返回 `-ENOTSUP`。
- 硬件 error IRQ 没有可重复的触发方法，error 回调（`-EIO`、residual）没有测试覆盖。
- mem2mem 传输太快，无法稳定在 RUNNING 状态下观测，传输中 `DMA_STOP`/residual、
  RUNNING 时 config/start 返回 `-EBUSY` 没有测试覆盖；不声明线速传输中的取消时序。
- task 上下文 put 等待 in-flight callback 退出、callback 中 put 被拒绝，均无测试覆盖。
- cache maintenance 属于 client；每个未来 consumer 必须独立定义 buffer ownership、
  descriptor 属性和 clean/invalidate 时机。
