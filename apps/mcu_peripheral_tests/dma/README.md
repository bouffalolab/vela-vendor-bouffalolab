# BL616CL DMA0 测试说明

## 背景

本目录只有一个测试入口 `mcu_dma_test`。测试程序通过 OpenVela 通用
`dma_dev_s` API 验证 BL616CL DMA0 的公共适配，而不是直接调用 LHAL。测试编译为
独立的 `libapps_mcu_dma_test.a`；chip adapter `bl616cl_dma.c.o` 进入
`libarch.a`，LHAL `bflb_dma.c.o` 进入 `libbl_lhal.a`。

BL616CL DMA0 有 8 个 channel，共用一个 raw IRQ 31（NuttX IRQ 47）。adapter 支持
memory-to-memory 和 memory-to-peripheral、peripheral-to-memory：1/2/4-byte 同宽访问，
源和目的 step 只能是 0 或 width。超过 4095 transfer units 的 one-shot、cyclic 和
cyclic 的 pause/resume 用 LLI 节点（`CONFIG_BL616CL_DMA0_LLI_NODES`，默认 16）。本测试
只做 mem2mem 数据传输和外设方向的参数校验；外设方向的实际传输由 I2S 播放和录音
（`mcu_i2s_test`）覆盖。`start_link` 不支持，硬件 error 无法注入。

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

程序按 DMA-001 到 DMA-007、DMA-009 到 DMA-014 顺序执行（编号 DMA-008 不再使用）。
每个 case 自己申请固定 channel、配置、启动、等待完成并释放；任一断言失败会打印该
case 的负 errno，但仍继续运行其余 case，最终以 `DMA0 summary` 汇总。完成判据是十三
项均打印 `PASS ret=0`，汇总为 `passed=13 failed=0 skipped=0`，且没有 panic、assert
或意外复位（跑完后 `resetcause` 应为 `power_on`）。LLI 节点少于 6 个时不编译
DMA-009..013，启动时打印 `DMA0 test: <n> LLI nodes, DMA-009 to DMA-013 need 6`，汇总为
`passed=8 failed=0 skipped=5`。

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

DMA-001、DMA-004、DMA-007 的「实测关键数据」来自用例精简前的固件（当时 8 项），输出
格式未变的行予以保留；DMA-002、DMA-003 和 DMA-009..014 的数据来自 2026-10-22、10-23
的固件。2026-10-20 改为按空闲通道运行后，在 AVE-V10（6 个通道）和 Ai-M64P-32S-Kit
（8 个通道）上各连跑两次，均为 `passed=7 failed=0`；2026-10-23 的最终固件两板各连跑
五次，均为 `passed=13 failed=0 skipped=0`。记录见 `docs/bl616cl-dma.md`。

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
2. 传输 `4095 * 4` 字节（单段，不占节点），等待一次 TC callback（result 为请求字节数、
   residual=0），逐字节比较并检查保护区。
3. 在相同有效配置下请求 `4096 * 4` 字节：有 LLI 节点时拆成两段，同样要求只回调一次、
   result 为请求字节数、residual=0、数据和保护区正确；节点数为 0 时必须在写硬件前
   返回 `-E2BIG`（输出里该项归一为 0）。
4. 释放 channel。

实测关键数据：

```text
units=4095 ret=0; units=4096 ret=0 (LLI nodes 16)
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
5. 对有效配置调用 cyclic（`len=64`、`period_len=24`，不是整数个 period），须返回
   `-EINVAL`（节点数为 0 时为 `-ENOTSUP=-138`）；对没有运行中传输的通道调用 pause、
   resume，须返回 `-EINVAL`。
6. 要求 pre-start residual=0、失败位图 `contract=0x00000`，证明每个 errno 都符合合同。
7. 使用最初的有效配置完成 64 字节传输并比较数据和保护区，证明失败请求没有污染旧
   配置。

实测关键数据：

```text
config invalid=-22/-22/-22/-22/-22/-22 unsupported=-138/-138/-138
start invalid=-22/-22/-22/-22/-22 ops=-22/-22/-22 retained=0 residual=0 contract=0x00000
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

## DMA-009：超过 4095 units 的 one-shot

流程：

1. 申请第 1 个空闲通道，轮流做 5 种传输各 12 次（共 60 次），每次换 pattern：1-byte
   `4064*2+1`（3 段，末段 1 unit）、1-byte `4064*3`（3 个整段）、1-byte `4064*3+100`
   （4 段）、2-byte 16384 字节（3 段）、4-byte 16384 字节（2 段）。每次都要求 callback
   恰好一次（完成后再等 2 ms 确认没有第二次）、result 为请求字节数、residual=0，数据
   与保护区正确。
2. 60 次之后节点必须全部归还：固定地址的 4-byte 传输 `(节点数+1)*4064` units 占满整个
   池，须完成一次；再多 1 unit 须返回 `-E2BIG`。
3. 20 次占满整个池的传输，每次在上一次的 callback 里启动；20 次都须完成，说明节点在
   callback 之前已经归还。

实测关键数据：

```text
60 transfers ret=0; 16 nodes ret=0; 17 nodes ret=-7; 20 chained ret=0
[DMA-009] PASS ret=0
```

## DMA-010：通道之间共用节点池

流程：

1. 第 2 个空闲通道启动固定地址的 cyclic（15 个 period，每个 4064 units，占 15 个节点）
   并暂停。
2. 第 3 个空闲通道：3 段（需要 2 个节点）返回 `-ENOMEM`；2 段（1 个节点）完成；再请求
   3 段仍返回 `-ENOMEM`，说明失败的请求不改变通道和池。
3. stop 第 2 个通道后，第 3 个通道占满整个池的传输须完成。
4. 第 2 个通道重新启动 cyclic，在运行中直接 put；之后第 3 个通道占满整个池的传输
   须完成，说明 put 也归还节点。

实测关键数据：

```text
hold=0 3-seg=-12 2-seg=0 3-seg=-12 after-stop=0 after-put=0
[DMA-010] PASS ret=0
```

## DMA-011：cyclic

在第 4 个空闲通道上依次跑 4 个环，覆盖单段 period、多段 period（含 36 units 的短
末段）和三种 width：

| width | period（字节） | period 数 | 每 period 段数 | stop 方式 |
|---:|---:|---:|---|---|
| 1 | 4064 | 4 | 1 | callback 内 |
| 1 | 8164 | 2 | 3（4064+4064+36） | task |
| 2 | 8192 | 2 | 2（4064+32 units） | task |
| 4 | 8192 | 2 | 1 | task |

流程（每个环）：

1. 源 buffer 填不含 0、每 251 字节重复的 pattern（period 长度都不是 251 的整数倍，段
   写错位置能看出来）。callback 检查 `len` 按顺序为下一个 period 结束处的偏移。
2. 启动后靠中断运行到装载超过 1100 个节点（LLI 计数至少回绕一次）。
3. 长窗口：暂停，等 5 ms 让已结束 period 的中断处理完，记下 callback 数和位置
   （`len - residual`）。然后一步一步走：每步关中断、resume 后紧接 pause、开中断，
   在暂停状态下读位置，把位置差累加成走过的距离，直到走过 1100 个节点以上，计数在
   窗口内回绕；最后再等 5 ms。callback 增量必须等于走过的 period 结束次数，最后一次
   的 `len` 必须是最后经过的 period 结束处的偏移。这一步独立于驱动的回调计数，能发现
   计数回绕时多发或漏发的回调。
   - DMA 只在 resume 到 pause 之间运行，关中断保证这一段不被拉长；读位置放在暂停后，
     读寄存器的耗时不算进 DMA 运行时间。每步走过的距离要小于半圈，否则报
     `-ERANGE`，避免一步跨过整圈而少算。输出里的 `max-step` 是每步的最大距离，
     实测 1 字节宽 240–296 字节、4 字节宽 512–1184 字节，环长约 16 KB。
   - 早期版本在整个窗口内关中断、在 DMA 运行中读位置。最快的 4 字节宽环在读位置
     较慢时会在两次读之间跑完整圈，距离少算后窗口一直关着中断走到读次数上限，喂狗
     被耽误，模组被看门狗复位（`resetcause` 为 `rtc_watchdog`）。
4. 4 个短窗口：做法同上，但先把目的 buffer 清零，只走 1.5 个 period。除 callback 检查
   外，要求两次暂停位置之间的字节等于源、其余字节仍为 0，用内存实际写入独立验证
   residual。每个环的长短窗口共做约 3.6 万次 pause/resume。
5. 再跑一圈多一个 period（整个 buffer 重新写满），按表中方式 stop。之后 20 ms 内不再
   有 callback、residual 不变且不超过 `len`，整个 buffer 等于源、保护区不变；callback
   内 stop 时 callback 数正好停在设定值。

实测关键数据（Ai-M64P-32S-Kit）：

```text
width=1 periods=4 x 4064 callbacks=2237 toggles=36269 max-step=296 mismatches=0 residual=10268 stop=callback ret=0
width=1 periods=2 x 8164 callbacks=1119 toggles=36359 max-step=296 mismatches=0 residual=4448 stop=task ret=0
width=2 periods=2 x 8192 callbacks=2227 toggles=36250 max-step=480 mismatches=0 residual=2400 stop=task ret=0
width=4 periods=2 x 8192 callbacks=4509 toggles=36194 max-step=1184 mismatches=0 residual=15728 stop=task ret=0
[DMA-011] PASS ret=0
```

失败时打印窗口起点、走过的距离和第一个不符的字节或 callback 数，例如
`window 10724+12904: byte 3200 is 0x00`。

## DMA-012：pause/resume

流程：

1. 第 5 个空闲通道启动 1-byte cyclic（4 个 period，每个 4064 字节），等到 10 次
   callback 后暂停；再等 2 ms 后记下 callback 数和 residual，20 ms 内两者都不能变。
2. resume，再等 20 次 callback。运行中 `DMA_START` 和 `DMA_CONFIG` 须返回 `-EBUSY`。
   stop 后 callback 序列没有错位，整个 buffer 等于源。
3. one-shot 不能暂停：固定地址 4-byte 传输 `8*4064` units，在它运行时 pause 须返回
   `-ENOTSUP=-138`，传输照常完成并回调。

实测关键数据：

```text
paused at 26 residual=4768 resumed to 53 mismatches=0 busy=-16/-16 one-shot pause=-138 ret=0
[DMA-012] PASS ret=0
```

## DMA-013：长传输中途 stop

流程：

1. 第 1 个空闲通道，1-byte 16384 字节（5 段），源不含 0。轮询 residual 到一半以下后
   stop，再读 residual。
2. 20 ms 后：没有 callback；residual 非 0；目的端开头的非 0 字节数等于
   `len - residual` 且与源一致，其余字节仍为 0。
3. 同一通道再完成一次 16384 字节传输并比较数据；最后一次占满整个池的传输须完成，
   说明之前各 case 都没有遗留节点。

实测关键数据：

```text
residual=7668 copied=8716 callbacks=0
[DMA-013] PASS ret=0
```

## DMA-014：通道被前面的回调重启

一次 DMA 中断按通道号从小到大处理状态位。前一个通道的回调可能停掉后面的通道并启动
新传输，而后面通道的状态位是它上一次传输留下的，不能算到新传输头上。

流程：

1. 前两个空闲通道（编号小的 A、编号大的 B）各启动 128 字节短传输。关中断等两个都在
   硬件上完成，再开中断，让一次中断同时看到两个完成位。
2. A 的回调里 stop B，并在 B 上启动 4-byte 16380 字节的新传输（单段，不需要 LLI
   节点）。
3. B 的新传输必须真正传完才回调，且只回调一次：result 为 16380、residual=0、目的
   数据全部正确。

修复前同一固件实测：B 的回调立刻到来，报告 16380 字节完成，实际只写了 1776 字节。

实测关键数据：

```text
restart ret=0 callbacks=1; new transfer callbacks=1 result=16380 residual=0 copied=16380 ret=0
[DMA-014] PASS ret=0
```

## 最终实测汇总

2026-10-23 的最终固件，两板 `nsh-peripherals` 各连跑五次 `mcu_dma_test`，均为
`DMA0 summary: passed=13 failed=0 skipped=0`，全程没有复位（`resetcause` 为
`power_on`）；AVE-V10 随后 `mcu_i2s_test all` 为 `I2S summary: PASS failed=0`。
`nuttx.bin` SHA256：

```text
Ai-M64P-32S-Kit  ed3631fb059974b1f56aec9202c1924dda7b317b23ba3eb06c63cf090edf2428
BL616CL-AVE-V10  f680935587b1284c9c9ffe77b4af67fa247b2eaaa7486290a931b2398938f4aa
```

把 `CONFIG_BL616CL_DMA0_LLI_NODES` 设为 0 的临时配置在 Ai-M64P-32S-Kit 上为
`passed=8 failed=0 skipped=5`（DMA-002 的 4096 units 返回 `-E2BIG`，DMA-003 的 cyclic
返回 `-ENOTSUP`）。

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

- 外设方向目前只有 I2S0 一个 consumer（TX、RX 各一个通道，初始化后一直持有），用
  单段 one-shot；长 one-shot、cyclic 和 pause/resume 只在 mem2mem 上实测。
- `start_link` 不支持；pause/resume 只用于 cyclic，one-shot 返回 `-ENOTSUP`。
- 硬件 error IRQ 触发不到，error 回调（`-EIO`、residual）没有测试覆盖。2026-10-23 在
  Ai-M64P-32S-Kit 上用临时程序试过：总线译码错误应答关闭（产品默认）和打开两种设置
  下，DMA 读 RAM 末尾之后、`0x30000000`，读跨 RAM 末尾的两段，写 ROM、写 4 MB flash
  之外的 XIP 窗口，全部正常完成，没有 DMA error，也没有总线错误中断。
- 单段 mem2mem 太快，无法在 RUNNING 状态下观测；传输中 stop/residual 和 RUNNING 时
  `-EBUSY` 靠长传输和 cyclic 覆盖（DMA-012、DMA-013）。
- put 等待 in-flight callback 的路径在 BL616CL 上走不到：callback 都在 DMA 中断里执行，
  芯片单核、没有开中断嵌套（`CONFIG_ARCH_HIPRI_INTERRUPT`），task 里的 put 不会与
  callback 同时执行。callback 中 put 被拒绝是针对编程错误的 `DEBUGASSERT`，开断言的
  固件会直接 panic，也不做测试。
- 节点数为 0 的路径只用临时配置验证过，没有常设配置。
- cache maintenance 属于 client；每个未来 consumer 必须独立定义 buffer ownership、
  descriptor 属性和 clean/invalidate 时机。
