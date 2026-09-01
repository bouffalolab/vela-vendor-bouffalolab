# BL616CL DMA0 测试说明

## 背景

本目录只有一个测试入口 `mcu_dma_test`。测试程序通过 OpenVela 通用
`dma_dev_s` API 验证 BL616CL DMA0 的公共适配，而不是直接调用 LHAL。测试编译为
独立的 `libapps_mcu_dma_test.a`；chip adapter `bl616cl_dma.c.o` 进入
`libarch.a`，LHAL `bflb_dma.c.o` 进入 `libbl_lhal.a`。

BL616CL DMA0 有 8 个 channel，共用一个 raw IRQ 31（NuttX IRQ 47）。首版只接入
memory-to-memory one-shot：支持 1/2/4-byte 同宽访问，源和目的 step 只能是 0 或
width，单次最多 4095 transfer units。pause/resume、cyclic、LLI/link/insert、硬件
error 注入和外设 DMA consumer 不属于本轮能力。

## 配置与构建

| 配置 | DMA0 adapter | test hook/app | clean build |
|---|---:|---:|---:|
| `nsh` | n | n | 1224/1224 |
| `nsh-dma` | y | n | 1227/1227 |
| `nsh-dma-test` | y | y | 1229/1229 |

在 SDK 根目录依次执行：

```bash
python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh-dma
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh-dma -j14

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh-dma-test
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh-dma-test -j14
```

裁剪检查结果：

- 关闭态没有 `bl616cl_dma.c.o`、`bflb_dma.c.o`、DMA test archive 或 adapter/test
  符号。
- 产品态包含 `bl616cl_dma.c.o`、`bflb_dma.c.o`，强符号
  `riscv_dma_initialize` 的类型为 `T`，不包含 test hook/app。
- 测试态额外包含 `libapps_mcu_dma_test.a`、`dma_test_main.c.o` 和 test hook。
- 三态 whole image 均为 4 MiB，boot2、双 partition、app 和 MFG 擦除区校验通过。

## 运行命令与总流程

测试固件启动到 NSH 后执行：

```text
mcu_dma_test
```

程序按 DMA-001 到 DMA-008 顺序执行。每个 case 自己申请固定 channel、配置、启动、
等待完成并释放；任一断言失败会打印该 case 的负 errno，但仍继续运行其余 case，最终
以 `DMA0 summary` 汇总。完成判据是八项均打印 `PASS ret=0`，汇总为
`passed=8 failed=0`，且没有 panic、assert 或意外复位。

cache maintenance 由 DMA client 负责：源 buffer 在启动前 clean，目的 buffer 在 DMA
写入前和 CPU 读取前 invalidate。测试 buffer 按 32 字节对齐并独占 cache line。这里验证
的是调用方履行该协议后的 cached/non-cache alias 可见性，不宣称 DMA adapter 自动维护
cache，也不宣称未 clean 的 dirty cache 数据会被保护。

## DMA-001：width、step、数据和 TC 回调

流程：

1. 固定申请 channel 0，分别配置 1、2、4-byte 同宽传输，源/目的 step 均为 width。
2. 每轮写入不同逐字节 pattern，clean 源、invalidate 目的，再启动 256 units。
3. 等待 TC callback，要求 callback 恰好一次，返回长度等于请求字节数，residual=0。
4. 读取 test 统计，要求 IRQ 计数和 callback 计数各增加一次，TC status/clear 含 CH0，
   error status/clear 不含 CH0。
5. invalidate 目的并逐字节比较源/目的。
6. 再配置 1-byte、`src_step=0`、`dst_step=1`，要求目的 128 字节全部复制源首字节。
7. 释放 channel。

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

1. 固定申请 channel 1，配置 4-byte width 和递增 step。
2. 传输 `4095 * 4` 字节，等待一次 TC callback 并逐字节比较。
3. 在相同有效配置下请求 `4096 * 4` 字节；必须在写硬件前返回 `-E2BIG`。
4. 检查 CH1 TC snapshot/clear 和 error 位，释放 channel。

实测关键数据：

```text
units=4095 ret=0 callbacks=1 data=match; units=4096 ret=-7
DMA-002 irq: tc=0x02/tc_clear=0x02 error=0x00/error_clear=0x00
[DMA-002] PASS ret=0
```

## DMA-003：无效请求原子拒绝

流程：

1. 固定申请 channel 2，先安装一份有效的 4-byte mem2mem 配置。
2. 依次提交负 step、非 mem2mem、源/目的 width 不同和非零 DRQ；均须返回
   `-EINVAL`。
3. 依次提交非零 priority、timeout、option；首版不支持，必须返回 NuttX ABI 的
   `-ENOTSUP=-138`。
4. 对保留的有效配置依次提交零长度、长度非 width 整数倍、源未对齐、目的未对齐和
   地址末端溢出；均须返回 `-EINVAL`。
5. 调用 cyclic、pause、resume；均须返回 `-ENOTSUP=-138`。
6. 要求 pre-start residual=0、失败位图 `contract=0x00000`，证明每个 errno 都符合合同。
7. 使用最初的有效配置完成 64 字节传输并比较数据，证明失败请求没有污染旧配置。

实测关键数据：

```text
config invalid=-22/-22/-22/-22 unsupported=-138/-138/-138
start invalid=-22/-22/-22/-22/-22 ops=-138/-138/-138 retained=0 residual=0 contract=0x00000
[DMA-003] PASS ret=0
```

## DMA-004：八通道与共享 IRQ

流程：

1. 按 ident 0..7 固定申请全部 8 个 channel，每个 channel 使用独立源、目的和 pattern。
2. 全部配置为 1-byte 递增 mem2mem，再依次启动 128 字节传输。
3. 每个 channel 都必须收到且只收到一次 callback，返回长度为 128。
4. 对每组目的 buffer 做 invalidate 和逐字节比较。
5. 释放全部 channel，并检查聚合 IRQ 的 TC snapshot/clear；该 case 只证明资源和分发，
   不作为并行带宽数据。

实测关键数据：

```text
channels=8 ret=0 (resource/IRQ dispatch evidence, not bandwidth)
DMA-004 irq: tc=0xc0/tc_clear=0xc0 error=0x00/error_clear=0x00
[DMA-004] PASS ret=0
```

## DMA-005：回调边界与软件 IRQ 合同

流程：

1. 固定申请 channel 3，以 NULL callback 完成真实 DMA；要求状态仍进入完成态、
   residual=0、IRQ 计数增加，而 callback 计数不增加。
2. 用普通 callback 再启动；callback 内调用 STOP 必须无死锁，调用 void
   `DMA_PUT_CHAN` 必须由运行时 guard 无副作用拒绝，拒绝计数增加一次。
3. 启用 test-only pre-enable hold，在 channel 仍为 RUNNING 时软件注入同一 channel 的
   TC+error；error 必须优先，只回调一次、返回 `-EIO`，residual 保持完整 128 字节。
4. 解除 hold，再做一次真实 DMA 完成，证明 error terminal 状态可重启。
5. 释放 channel。

软件注入只验证 adapter 状态机、优先级和清理合同，不是 BL616CL 硬件 error IRQ 的
实物注入证据。

实测关键数据：

```text
injection_count=1 callback_count=17 rejected_puts=1
ret=0 error-residual=128 restart-callback=1 rejected-put-delta=1
[DMA-005] PASS ret=0
```

## DMA-006：stop/residual 与 cache alias

流程：

1. 固定申请 channel 4，确认测试 buffer 位于 cacheable OCRAM，并计算对应 non-cache alias。
2. 配置 128 字节传输，打开 test-only hold，使寄存器已编程但 channel 尚未 enable。
3. 要求 residual=128；RUNNING 状态重新 config/start 均返回 `-EBUSY`。
4. STOP 后再次 STOP，两个调用都必须可安全完成；residual 仍为 128。
5. 解除 hold，以 cached 源和 non-cache 目的 alias 重启真实 DMA；按 client cache 协议
   invalidate 后通过 cached alias 比较数据。
6. 检查真实 TC callback 和 IRQ snapshot/clear，释放 channel。

该流程证明 pre-enable stop、byte residual 换算、结束后重启和 alias 可见性。DMA 传输过快，
本轮不宣称能稳定捕获线速中途取消。

实测关键数据：

```text
busy=-16/-16 repeat-stop=0 ret=0 aliases=0x20fd3380/0x20fcf380
DMA-006 irq: tc=0x10/tc_clear=0x10 error=0x00/error_clear=0x00
[DMA-006] PASS ret=0
```

## DMA-007：owner 等待、耗尽和并发 put

流程：

1. 按 ident 0..7 持有全部 channel。
2. 启动第九个线程再次申请 channel 7；等待 20 ms 后它必须仍阻塞。
3. 释放原 channel 7；等待线程必须被唤醒并获得同一 fixed-ident channel，再释放全部资源。
4. 重新持有 channel 6，同时启动两个线程对同一 handle 执行 put；两线程都必须返回，
   但 available semaphore 只能 post 一次。
5. 再持有 channel 6，启动 waiter；它必须在持有期间阻塞，释放后获得同一 handle。

实测关键数据：

```text
ninth-waiter=blocked/woken concurrent-put=single-post ret=0
[DMA-007] PASS ret=0
```

## DMA-008：callback drain

流程：

1. 固定申请 channel 7，使用 test-only hold，启动带阻塞点的 callback。
2. 由独立线程软件注入 TC，等待 callback 已进入但尚未退出。
3. 再启动 task-context put 线程；等待 20 ms，put 不得提前返回。
4. 释放 callback，等待 callback、注入线程和 put 线程退出；要求 callback 恰好一次且
   返回 128 字节，put 此时才返回。
5. 重新申请 channel 7，必须得到同一 handle，证明 drain 后资源完整复用。

实测关键数据：

```text
callback=1 put-returned=1 reused=0x60fc9e8c ret=0
[DMA-008] PASS ret=0
```

## 最终实测汇总

Ai-M64L-32S-Kit 使用 `/dev/ttyUSB2`、2000000 baud。测试固件
`nuttx.bin` SHA256 为
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

- 只支持 DMA0 mem2mem one-shot；没有外设 request consumer 证据。
- pause/resume、cyclic、LLI/link/insert 返回 `-ENOTSUP`。
- 硬件 error IRQ 缺少可重复注入方法；DMA-005 的 error 是 test-only 软件合同。
- stop 实测边界是 programmed-but-not-enabled；不声明线速传输中的精确取消时序。
- cache maintenance 属于 client；每个未来 consumer 必须独立定义 buffer ownership、
  descriptor 属性和 clean/invalidate 时机。
