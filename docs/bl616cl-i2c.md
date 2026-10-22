# BL616CL I2C0/I2C1 Master 适配与验证

本文说明 BL616CL I2C master 与 OpenVela I2C 子系统的最大能力交集、实现边界、
Kconfig 裁剪、构建检查和运行验证。当前已完成 I2C1-only 无 target 超时路径与现役外设回归，
并用逻辑分析仪确认 I2C0、I2C1 在 GPIO4/5 上的无 target 地址波形。2026-10-16 起在
BL616CL-AVE-V10 上以板载 ES8388 完成正向读写验证（见“正向 target”）；多字节读和
双总线仍缺实物 target。2026-10-22 起默认用中断方式传输，调用线程等传输时让出 CPU，地址或
数据无应答立即返回；轮询方式仍可选（`CONFIG_I2C_POLLED=y`，见“传输方式”）。

## 背景

BL616CL LHAL 提供 I2C0/I2C1 两个控制器的同步 polling master 和中断模式（消息带
`I2C_M_INT` 时只配置并启动，按第一条消息的方向打开 FIFO 中断，数据和子地址由调用者在
中断里用 `bflb_i2c_fifo_write/read` 搬运），
但 OpenVela 适配层此前没有 chip lower-half、board pin owner 或 `/dev/i2cN` 节点。LHAL
位于 CI 同步的 `vendor/bouffalolab/drivers/`，本适配只通过父仓 CMake wrapper 条件编入
`bflb_i2c.c`，不修改同步驱动仓。中断方式需要 drivers 2.3.38 及以后（LHAL 从这一版起带
BL616CL 的 I2C 中断模式），轮询方式不需要。

OpenVela `I2C_TRANSFER()` 接受消息数组并要求整组传输原子。公共 `NOSTOP`、
`NOSTART` 和 10-bit flag 与 LHAL 私有消息语义并不等价，因此 chip lower-half 必须
先验证和翻译，再访问控制器。

## 最大能力交集

| 能力 | 当前结论 | 依据和边界 |
|---|---|---|
| I2C0/I2C1 master | 纳入 | 两个实例有独立 base、IRQ、idx 和独立 mutex |
| 7-bit address | 纳入 | 地址 0x00..0x7f；实物 probe 只允许普通地址 0x08..0x77 |
| 单消息 read/write | 纳入 | 1..1024 字节；0 和 1025 在硬件前拒绝 |
| 100/400 kHz | 纳入 | 两个频率均在 AVE-V10 的 ES8388 上验证；其它频率当前拒绝 |
| 中断传输 | 纳入，默认 | LHAL 中断模式启动，chip 中断服务搬运 FIFO，调用线程睡眠等完成；无应答立即返回 |
| 轮询传输 | 纳入，`I2C_POLLED` 时 | LHAL 同步 polling，调用线程忙等到完成；无应答等满 LHAL 的 100 ms |
| write + repeated-start + read | 纳入 | 严格二元消息；write prefix 1..16，read 1..1024 |
| 同总线原子性 | 纳入 | 一把实例 mutex 覆盖完整 OpenVela transfer |
| 双总线并发 | 纳入，未实测 | 两实例各一把 mutex，互不串行；实物双总线待资源冻结 |
| 10-bit address | 后续项 | 寄存器有字段，当前没有真实 10-bit target 验证 |
| 三段以上 restart | 后续项 | LHAL 私有 restart 与公共 ABI 不同，尚未逐段证明线序 |
| `NOSTART` continuation | 写续写纳入 | 1..16 字节写 + 带 `NOSTART` 的写，翻译成 LHAL 子地址 + 写；读续读和三段以上的链返回 `-EOPNOTSUPP` |
| trace、bus reset | 后续项 | 缺 trace 和 GPIO recovery 验证闭环 |
| DMA | 后续项 | 计划作为第三种传输方式；LHAL DMA 路径只配置并启动，channel、完成等待和 cache 合同未做 |
| slave、1 MHz、3.4 MHz | 排除 | LHAL 无 slave API，频率文档上限为 400 kHz |
| zero-length probe | 排除 | LHAL packet length 会发生 `length - 1` 下溢 |

## 实现和所有权

调用链为：

```text
board_late_initialize()
  -> bl616cl_bringup() -> bl616cl_board_initialize()
  -> ai_m64p_kit_i2c_initialize() 或 bl616cl_ave_v10_i2c_initialize()
  -> bl616cl_i2cbus_initialize(bus, scl, sda)
  -> i2c_register(bus, lower)
  -> /dev/i2c0 或 /dev/i2c1

I2CIOC_TRANSFER
  -> OpenVela i2c upper-half
  -> bl616cl_i2c_transfer()
  -> 参数和消息组验证
  -> 实例 mutex
  -> 每组消息：
       中断方式：LHAL configure + 中断模式启动，
                 睡眠等中断服务完成（END 或错误时停控制器、记录 raw status）
       轮询方式：LHAL configure + 同步 polling 到完成
  -> unlock
```

某条总线初始化或注册失败时，板级代码记一条 `ERROR` 日志，其它总线和后面的外设照常
初始化。

代码归属：

- `chips/bl616cl/bl616cl_i2c.c`：实例资源、消息翻译、mutex、两种传输方式和错误窗口；
- `boards/bl616cl/ai-m64p-32s-kit/src/ai_m64p_kit_i2c.c`：pin owner 和节点注册；
- `boards/bl616cl/bl616cl-ave-v10/src/bl616cl_ave_v10_i2c.c`：AVE-V10 的 I2C0 节点注册。
  ES8388 驱动（`bl616cl_ave_v10_audio.c`）再调用一次 `bl616cl_i2cbus_initialize(0, ...)`，
  拿到同一个实例，实例按引用计数共享，两边的传输由同一把实例 mutex 串行；
- `cmake/bl616cl_lhal.cmake`：仅在实例开启时编入同步仓的 `bflb_i2c.c`；
- `apps/mcu_peripheral_tests/i2c/`：独立 `libapps_mcu_i2c_test.a` 和一个测试 main；
- `drivers/`：保持未修改。

适配对象进入 `libarch.a`，board 注册进入 `libboard.a`，LHAL 对象进入
`libbl_lhal.a`，测试命令进入独立 `libapps_mcu_i2c_test.a`。chip 代码不会放入
`libbl_std.a`。

## Kconfig 与资源裁剪

主要选项如下：

| 选项 | 作用 | 关闭效果 |
|---|---|---|
| `BL616CL_I2C` | chip 总开关，OpenVela I2C master | 无实例时 chip/LHAL 对象不进入 archive |
| `I2C_POLLED` | NuttX 通用选项（Device Drivers → I2C Driver Support → Polled I2C），选轮询方式 | 默认关闭，即中断方式 |
| `BL616CL_I2C0/1` | 控制器实例 | 只生成对应实例私有数据和符号 |
| `AI_M64P_KIT_I2C` | board I2C 资源总开关 | 不执行 board 注册 |
| `AI_M64P_KIT_I2C0/1` | board 实例、pin 和 `/dev/i2cN` | 未选择实例不占 pin、不注册节点 |
| `BL616CL_AVE_V10_I2C0` | AVE-V10 的 I2C0（固定 GPIO20/11）和 `/dev/i2c0` | 不注册节点 |
| `BL_MCU_PERIPHERAL_TESTS_I2C` | 测试 app | 无测试 archive、命令和字符串 |

Ai-M64P-32S-Kit 板级默认关闭 I2C：

- I2C0 默认 GPIO4=SCL、GPIO5=SDA。数据手册引脚表中 GPIO4/5 的 function 5 就是
  I2C0 SCL/SDA，没有 32.768 kHz 晶振或 DC-DC 功能。早先在另一块模组上记录过这两脚
  冲突、启用后无启动日志；在 Ai-M64P-32S-Kit 上未复现，启用 I2C0 后正常启动，
  波形见“逻辑分析仪实测”。
- I2C1 默认 GPIO12=SCL、GPIO13=SDA。开启后这两脚从 GPIO lower-half 表移除，
  不生成 `/dev/gpio12`、`/dev/gpio13`，避免双 owner。
- USB GPIO32/33、UART0 GPIO34/35、boot GPIO36 不作为 I2C pin。
- 板上没有已确认的 I2C target 和独立上拉。实物测试需外接 3.3 V EEPROM 和
  2.2 kOhm 至 4.7 kOhm 上拉。

BL616CL-AVE-V10 只有 I2C0，硬连到 GPIO20（SCL）、GPIO11（SDA），板上 4.7 kOhm 上拉到
3.3 V，挂 ES8388 codec（地址 0x10）和摄像头排线的 SCCB。

## 消息和错误合同

硬件访问前统一检查 `count > 0`、非空 buffer、7-bit 地址、100/400 kHz 和
1..1024 字节。非法结构返回 `-EINVAL`；合法但当前未支持的 `TEN`、单独或接在读后面的
`NOSTART`、终止 `NOSTOP`、17 字节 prefix、三段无 STOP 链，以及中断方式下前缀加数据超过
1024 字节的续写返回 `-EOPNOTSUPP`。

两种双消息在地址和频率一致时合成一次 LHAL 调用，第一段都是 1..16 字节写（prefix）：

- `WRITE|NOSTOP -> READ`：第二段是 1..1024 字节读，prefix 作为 LHAL 的 `NOSTOP` 子地址，
  线上为 prefix 后 repeated start 再读。
- `WRITE[|NOSTOP] -> WRITE|NOSTART`：第二段是写。线上为一次 START、地址+W、prefix、数据、
  STOP，和把两段拼成一条写消息相同。中断方式就按一条写消息发（不用子地址，原因见“中断
  方式”），前缀加数据合计不超过 1024 字节；轮询方式把 prefix 作为 LHAL 子地址，数据
  1..1024 字节。NuttX `i2c set` 写寄存器就是这种消息（寄存器号带 `NOSTOP`，数值带
  `NOSTART`）。

`WRITE|NOSTOP -> WRITE`（不带 `NOSTART`）在公共语义里要 repeated start，LHAL 子地址
做不到，仍返回 `-EOPNOTSUPP`。STOP 分隔的普通消息仍在同一 mutex 内分别调用 LHAL，
防止其它 client 插入整组 transfer。

chip/arch 与 app 使用的 `ENOTSUP` 数值不同，本实现跨 archive 合同固定使用两侧
一致的 `EOPNOTSUPP=95`。地址或数据 NACK、仲裁丢失、FIFO 错误、控制器超时和等待超时
都返回 NuttX `-ETIMEDOUT`（110），两种方式相同，不细分 errno；结束或失败时采样 raw
status，`CONFIG_DEBUG_I2C_ERROR` 打开时随错误日志输出。无 target 预期 `-ETIMEDOUT`。

## 传输方式

默认中断方式。在 menuconfig 的 Device Drivers → I2C Driver Support 里打开
`Polled I2C (no interrupts)`（`CONFIG_I2C_POLLED=y`）改用轮询方式，两条总线一起切换。
两种方式的消息合同和返回值相同，差别如下：

| | 中断方式 | 轮询方式 |
|---|---|---|
| 等传输时 | 调用线程睡眠，其它线程可以运行 | 调用线程在 LHAL 里忙等，比它优先级低的线程都不能运行 |
| 地址或数据 NACK | 收到即返回（无 target probe 整条命令约 6 ms） | 等满 LHAL 的 100 ms 超时 |
| 续写 | 前缀 + 数据按一个包发，合计 ≤ 1024 字节 | 前缀作为 LHAL 子地址，数据 ≤ 1024 字节 |
| 中断资源 | 占用 I2C0/I2C1 中断 | 不用中断 |
| drivers 版本 | 2.3.38 及以后 | 无要求 |

轮询方式下，AVE-V10 停流复位 ES8388 时优先级 252 的工作线程连续占用 CPU 约 4.4 ms（见
“中断方式实测”的 trace 表），用 I2S 或对时延敏感时选中断方式。轮询方式还有两处按 LHAL
代码推断、没有在总线上确认的风险：单条写（含前缀）超过 16 字节时，LHAL 在使能控制器前
就要把整条数据放进 4 个 word 的 TX FIFO，会等到超时；子地址 + 写时，LHAL 先使能再填数据，
这期间来了较长的中断，就会出现下面说的 TX FIFO 下溢。

### 中断方式

每组消息（单条消息，或子地址 + 读/续写的二元组）走一次 LHAL 中断模式：

1. `bflb_i2c_init()` 配置频率，数据消息带 `I2C_M_INT` 调用 `bflb_i2c_transfer()`。LHAL
   配置包长和子地址长度，按第一条消息的方向打开 FIFO 中断，使能控制器后返回。读带 prefix
   时第一条是子地址写，LHAL 打开的是 TX FIFO 中断，不替调用者写子地址。
2. 线程打开 END、NACK、仲裁、FIFO 错误和控制器超时中断后睡眠等待。
3. 中断服务在 TX FIFO 有空位时按 32 bit word 填写：写操作先 prefix 后数据，跨界的 word
   拼起来写；读带 prefix 时只写 prefix，写完屏蔽 TX FIFO 中断、打开 RX FIFO 中断。在 RX
   FIFO 有数据或 END 时取读数据；出现 END 或任一错误时停控制器（同时清 FIFO 和状态、屏蔽
   中断）并唤醒调用线程。一次写进 2 次中断（填 FIFO、END）。
4. 调用线程最多等 100 ms 加两倍名义总线时间（每字节 9 bit，每条消息算 1 个地址字节），
   超时由线程自己停控制器。

写操作不用子地址，是因为子地址之后控制器立刻要写数据：此时 TX FIFO 为空会置 FIFO 错误
（下溢），而且总线上可能发出旧数据。在“子地址 + 数据”的写里人为推迟填数据，从 LHAL 返回
算起，400 kHz 下在 45–61 µs 之间、100 kHz 下在 121–222 µs 之间开始出现 FIFO 错误，大约是
子地址字节发完的时刻。出错时多数情况仍是
正确数据，但有一次把 0x41 写成了 0x0e。单个写包不一样：第一个 word 进 FIFO 之前不开始
发数据，推迟 500 µs 填仍然正确、无 FIFO 错误。所以按单个包发、全部交给中断服务填，首次
填充没有时限，线程也不用关中断。

TX FIFO 实测 4 个 word（16 字节）：控制器关闭时连写 4 次 `tx_fifo_cnt` 从 4 降到 0，
第 5 次置溢出位。数据手册写的深度 2 不对；RX FIFO 深度未测。地址或数据收到 NACK 时，
控制器同时置 END 并停下，FIFO 里剩下的数据不再发送。写超过 4 个 word 时，中断服务要在
FIFO 发空前续填；中断延迟超过约 3 个 word 的总线时间（400 kHz 约 270 µs）就会下溢，
传输以 `-ETIMEDOUT` 失败，从机可能已收到错误字节。BLE 中断实测最长 337 µs（见
`bl616cl-i2s.md`），BLE 运行时 400 kHz 下超过 16 字节的写有这个风险。

## 中断方式实测

2026-10-22，两板 `nsh-peripherals`（默认中断方式）。

| 板 | 命令 | 结果 |
|---|---|---|
| AVE-V10 | `hw probe` 100/400 kHz、`hw combined` 100/400 kHz | PASS |
| AVE-V10 | `hw eeprom`：0x10 单条写和 `--split-write` 100 kHz；0x1A 单条写 400 kHz、`--split-write` 100 kHz | PASS |
| AVE-V10 | `i2c set`/`i2c get` 100/400 kHz（400 kHz 连写 3×20 次）、`i2c dump`、`hw concurrent` | PASS；`i2c set -s` 按预期失败 |
| AVE-V10 | `time "mcu_i2c_test hw probe --bus 0 --addr 0x52 --no-target"` | PASS `ret=-110`，0.0063 s（轮询时 0.1066 s） |
| AVE-V10 | `time "i2c dev -b 0 08 77"` | 0x10、0x5D 应答，约 0.08 s（轮询时每个空地址等 100 ms，约 11 s） |
| AVE-V10 | `mcu_i2s_test all` | PASS |
| M64P | I2C0、I2C1 各在 100/400 kHz 下 `hw probe --addr 0x52 --no-target` | 均 PASS `ret=-110`，约 0.006 s |

0x1A 的 400 kHz `--split-write` 命令要 17 个参数，超过 NSH 的 16 个，没有跑；同样的
消息结构（1 字节子地址 + `NOSTART` 1 字节写）由 400 kHz 的 `i2c set` 覆盖。0x5D 在轮询
固件上同样应答（2026-10-16 扫描时没有），来源未确认。

2026-11-04 drivers 同步到 2.3.38 后，两板按上表重跑中断方式和轮询方式（轮询用
nsh-peripherals 加 `CONFIG_I2C_POLLED=y` 的临时配置），结果与 2026-10-22 逐项相同，含
下面的 `hw boundary` 两组和 `mcu_i2s_test all`：中断方式无 target 0.0062–0.0066 s、
`i2c dev` 0.078 s，轮询方式约 0.106 s、11.1 s。2.3.38 的 LHAL 对子地址 + 读打开 TX FIFO
中断而不预填子地址；中断服务若按读只等 RX FIFO，子地址没人写、TX FIFO 中断反复进入，
`i2c get` 卡住直到看门狗复位（实测）。中断服务因此先写子地址再切到 RX FIFO。


`hw concurrent` 的子地址默认 2 字节。对 ES8388 不带 `--addr-width 1` 时，前缀 `00 00` 的
第二个字节被当成数据写进寄存器 0x00，跑完寄存器 0x00 从 0x12 变成 0x00（“正向 target”表里
的 concurrent 命令就是这样，轮询固件上同样如此）。对 ES8388 要用
`mcu_i2c_test hw concurrent --addr 0x10 --offset 0 --addr-width 1 --length 1 --iterations 32 --freq 400000`
（省掉 `--bus 0` 才不超过 NSH 的 16 个参数），跑完寄存器不变。

多字节读用 `hw boundary` 在 ES8388 上测：`--offset 0 --addr-width 1 --length 256` 在
100/400 kHz 下 1–256 字节全部 PASS；`--offset 18 --addr-width 2 --length 1024`（前缀
`00 12` 把寄存器 0x00 写回它的现值 0x12）在 100/400 kHz 下 1–1024 字节全部 PASS。ES8388
读不自动递增，首字节是寄存器值、其后是 0xFF，所以这只验证了 RX FIFO 分批取数、字节数和
100 kHz 下约 92 ms 的长读不被控制器超时截断，没有验证数据内容。

另用临时插桩在中断服务停控制器之前读总线忙标志：开机 codec 初始化和上述读、写、地址
NACK 共约 480 次传输，每次都已空闲（STOP 已发完），所以中断服务处理 END 时停控制器不会
截断 STOP。

AVE-V10 音频停流时，ES8388 驱动在优先级 252 的工作线程里复位 codec（43 次 I2C 传输）。
用 noteram trace 抓 5 次 48 kHz 放音的停流：

| | 工作线程单次运行 CPU 最大 | 优先级 >200 连续占用最大 |
|---|---:|---:|
| 轮询 | 4279–4287 µs | 4385–4397 µs |
| 中断 | 353–356 µs | 371–375 µs |

中断方式下复位约 5.5 ms，每次写进 2 次中断、读进 1 次（中断服务平均约 12 µs，最长
24 µs），传输之间低优先级线程可以运行。工作线程最长的一次在复位结束之后，不是等 I2C。

## 构建与裁剪验证

以下矩阵是历史裁剪证据；当前复测使用 `nsh-peripherals`，不再从已删除的
`nsh-i2c` 派生。

在 SDK 根目录分别执行：

```bash
vendor/bouffalolab/vela clean \
  bl616cl/ai-m64p-32s-kit/configs/nsh
vendor/bouffalolab/vela build \
  bl616cl/ai-m64p-32s-kit/configs/nsh -j6

# 将目标依次替换为：
# nsh
# nsh-peripherals
# nsh
```

2026-08-30 fresh clean build 数据：

三个启用 I2C 测试 app 的配置使用 `CONFIG_NSH_LINELEN=256`，以完整输入 EEPROM
测试命令；no-instance 保持默认 64。该设置只扩大 NSH 命令行缓冲，不改变 I2C
lower-half 的能力或传输长度边界。

| 配置 | 构建 | chip | board | LHAL | test app | 实例符号 | `nuttx.bin` |
|---|---:|---:|---:|---:|---:|---|---:|
| no-instance | 1228/1228 | 0 | 0 | 0 | 0 | 无 | 479648 B |
| I2C0-only | 1234/1234 | 1 | 1 | 1 | 1 | 仅 `g_bl616cl_i2c0` | 515840 B |
| I2C1-only | 1234/1234 | 1 | 1 | 1 | 1 | 仅 `g_bl616cl_i2c1` | 515824 B |
| dual | 1234/1234 | 1 | 1 | 1 | 1 | `g_bl616cl_i2c0/1` | 516400 B |

对应 `nuttx.bin` SHA256：

```text
no-instance c0c40873d14c6da68f51363fbabc4273a3cfd494982ee5da1178b8c99c4f5911
I2C0-only  d006ef702fca6c9a9fdc1fbc68601b83236ac294b85a8cf2a5f6c8fbdc779ab6
I2C1-only  cbed36c91f46631dbe693341bc5dfd31e4d9875908cc35788222c681a54e52fe
dual       3bb8c27210cebf56e02ab1053e45b01d42e0b19f6b00efbf35f452069b72b7bb
```

no-instance 的 `libarch.a` 无 `bl616cl_i2c.c.o`，`libboard.a` 无
`ai_m64p_kit_i2c.c.o`，`libbl_lhal.a` 无 `bflb_i2c.c.o`，测试 archive 不存在。
三个实例配置的 map 均把对象归入上述四个独立 archive；单实例 `nm` 只出现自身
实例私有符号。

## I2C1-only 无 target 实测

运行环境：Ai-M64P-32S-Kit，CH340 `1a86:7523`，`/dev/ttyUSB2`，2,000,000 baud。
I2C1-only 固件烧录时 host/device SHA256 均为
`cbed36c91f46631dbe693341bc5dfd31e4d9875908cc35788222c681a54e52fe`，读取校验
长度为 515824 B。复位后出现 `NuttShell (NSH)` 和 `nsh>`，`/dev/i2c1` 存在。

在未连接 target 的 GPIO12/13 上运行真实控制器负向 probe：

```text
mcu_i2c_test hw probe --bus 1 --addr 82 --no-target
I2C_TEST RESULT mode=hw case=probe PASS bus=1 addr=0x52 freq=100000 expected=no-target ret=-110
nsh>
```

这证明了 OpenVela `/dev/i2c1`、chip adapter、LHAL polling 和超时 errno 规范化路径。
该记录当时没有分析仪，波形见下节；它不能解释为正向 target 通过。

### 逻辑分析仪实测

2026-10-13，Ai-M64P-32S-Kit，DSLogic U3Pro32（10 MHz 采样、阈值 1.6 V）接 GPIO4/5，
不接 target，也不接外部上拉，只有芯片内部上拉。分别在 `nsh-peripherals`（I2C1 在
GPIO4/5）和临时配置（I2C0 在 GPIO4/5、关闭 I2C1）上执行：

```text
mcu_i2c_test hw probe --bus 1 --addr 0x52 --freq 100000 --no-target
mcu_i2c_test hw probe --bus 1 --addr 0x52 --freq 400000 --no-target
mcu_i2c_test hw probe --bus 0 --addr 0x52 --freq 100000 --no-target
```

三条命令都返回 `PASS ... ret=-110`，sigrok I2C 解码均为
Start、Address write 0x52、NACK、Stop。临时配置启动到 `nsh>`，`/dev/i2c0` 存在。

| 实例 | 请求 | SCL 实测 |
|---|---|---|
| I2C1 | 100 kHz | 92.6 kHz |
| I2C1 | 400 kHz | 303.0 kHz |
| I2C0 | 100 kHz | 92.6 kHz |

SCL 低于请求值，不超过请求值。当时只有内部上拉，接外部 2.2 kOhm 至 4.7 kOhm 上拉后的
频率未测，原因未验证。

### 正向 target：BL616CL-AVE-V10 上的 ES8388

2026-10-16，BL616CL-AVE-V10，`nsh-peripherals`，I2C0 接板载 ES8388（地址 0x10）。

| 命令 | 结果 |
|---|---|
| `mcu_i2c_test hw probe --bus 0 --addr 0x10 --freq 100000`、`--freq 400000` | 均 PASS（ACK） |
| `mcu_i2c_test hw probe --bus 0 --addr 0x52 --freq 100000 --no-target` | PASS，`ret=-110` |
| `i2c dev -b 0 0x08 0x77` | 只有 0x10 应答 |
| `i2c get -b 0 -a 10 -r <reg>`，reg 0x00–0x34 | 53 个寄存器全部等于 NuttX `es8388.h` 的复位默认值 |
| `mcu_i2c_test hw combined --bus 0 --addr 0x10 --offset 0x00 --addr-width 1 --length 1 --freq 400000` | PASS，读到 0x06 |
| `mcu_i2c_test hw eeprom --addr 16 --offset 26 --addr-width 1 --capacity 53 --length 1 --page-size 1 --allow-write`，另加 `--freq 400000` 再跑一次 | 均 PASS，写入、读回、恢复都已校验 |
| 同上，`--offset 16` | PASS |
| `mcu_i2c_test hw concurrent --bus 0 --addr 0x10 --offset 0 --length 1 --iterations 32 --freq 400000` | PASS，128 次组合读一致 |

`hw eeprom` 在这里只是借用它的“备份、单消息写、读回、恢复”流程；`--length 1
--page-size 1` 保证每次只写一个寄存器。上面的 0x1A（LDACVOL）、0x10（LADCVOL）写入
值分别是 0x40、0x4A，测试后恢复为默认 0xC0。

这组结果证明了单字节读、写地址后 repeated-start 读、单消息写和同一总线的并发互斥，
在 100/400 kHz 下对真实从机都正确。没有覆盖的部分：

- 多字节读：ES8388 读操作不自动递增寄存器地址（`i2c dump` 只有首字节有效），不能用它
  验证多字节读和 `hw boundary`，仍需要 EEPROM。
- 波形：这次没有接逻辑分析仪，接 4.7 kOhm 外部上拉时的 SCL 实际频率和 ACK 时序未测。
- `hw dual`：AVE-V10 只有 I2C0。

同日驱动支持 `NOSTART` 续写后，在同一板上补测：

| 命令 | 结果 |
|---|---|
| `i2c set -b 0 -a 10 -r 1a c4`，再 `i2c get` | 读回 0xC4；写回 0xC0 后读回 0xC0 |
| 同上加 `-f 400000`，写 0xC8 | 读回 0xC8；写回 0xC0 后读回 0xC0 |
| `mcu_i2c_test hw eeprom ... --allow-write --split-write`，0x1A 在 100/400 kHz、0x10 在 100 kHz | 均 PASS，备份、读回、恢复已校验 |
| `i2c set -s -b 0 -a 10 -r 1a c4` | 按预期失败，0x1A 仍为 0xC0 |

`i2c set -s` 把寄存器号和数值分成两次调用，第一次以 `NOSTOP` 结束，要求总线跨调用
保持占用，本驱动不支持。i2ctool 的 `-s/-n` 等选项会保留到之后的命令，用过 `-s`
后 `i2c get` 也会失败，要加一次 `-n` 恢复。只验证了 1 字节 prefix；更长的 prefix
写续写需要 EEPROM。

## 现役外设回归

I2C1 使用 GPIO12/13 后，GPIO 回归改用仍存在的 `/dev/gpio19`，不复用已经被移除的
`/dev/gpio12`。同一 I2C1-only 固件实测：

```text
mcu_gpio_test -c edge --out /dev/gpio19 -n 16 -r
[GPIO-edge] PASS rejected invalid operations and recovered for 16 cycles

mcu_timer_test -c 001 -t 100000 -n 5 -e 0.5 -v
RESULT max_err=360.0us (0.360%) tol=500.0us (0.50%)
[TIMER-001] PASS accuracy within tolerance

mcu_timer_test -c 002 -t 500000 -a 39 -b 79 -v
div=39 period=0.4998s
div=79 period=0.9999s
ratio period_b/period_a=2.001 (expected 2.000, tol +/-5%)
[TIMER-002] PASS prescaler takes effect

mcu_timer_test -c 005
[TIMER-005] PASS rejected requests preserved state; live update fired; lifecycle recovered

mcu_wdt_test -c 002 -t 3000 -p 9000 -i 1000 -v
fed #9 elapsed=9017ms timeleft=3000ms
PASS: fed 9 times over 9017ms, no reset; watchdog stopped

mcu_wdt_test -c 003 -t 3000
PASS: invalid/live changes rejected; duplicate lifecycle preserved state

oneshot -d 100000 /dev/oneshot
Starting oneshot timer with delay 100000 microseconds
Finished

ls /dev/rtc0
 /dev/rtc0
date
Mon, Jan 01 00:00:02 2018

echo ST021_I2C1_REGRESSION_ALIVE
ST021_I2C1_REGRESSION_ALIVE
nsh>
```

回归期间没有意外 assert、panic 或 watchdog reset。GPIO-010 依赖 gpio8，当前
I2C1-only 临时配置没有该节点，因此该 case 不计为通过。RTC 本轮只验证节点和读时，
没有启用 `mcu_rtc_test` 重跑 Alarm 全集；最终实物验收仍需执行完整 RTC 回归。

## 实物总线完整流程

开始前记录模组变体、总线、SCL/SDA、target 型号/容量/地址、供电、上拉阻值和逻辑
分析仪通道。建议 I2C1 使用 AT24C32、0x50、4096 B、GPIO12/13 和 4.7 kOhm 上拉。

1. `ls /dev/i2c1`，确认节点；检查 gpio12/13 节点不存在。
2. 分别以 100 kHz、400 kHz 执行 `hw probe`；分析仪记录实际 SCL 和 ACK。
3. 对确认不存在的普通地址执行 `hw probe --no-target`；必须是 `-ETIMEDOUT`。
   其它本地错误或意外 ACK 均失败。
4. 执行 `hw combined`；分析仪必须看到一次 START、地址+W、prefix、
   repeated-start、地址+R、数据和 STOP，中间不得出现 STOP。
5. 执行 `hw boundary --length 1024`，逐项覆盖 1、4、31、32、33、255、256、1024。
6. 明确 EEPROM `--capacity` 后执行 `hw eeprom --allow-write`；依次保存 backup、
   分页写 pattern、整段读回、恢复 backup、再次读回。pattern 和 restore 都验证成功
   才 PASS。
7. 执行 `hw concurrent`，四线程每线程 32 次 combined read，所有数据必须与参考一致。
8. 只有两条总线和两个 target 都冻结后执行 `hw dual`，并记录两路同时活动的波形。
9. 重跑 GPIO、timer、oneshot、WDT、RTC，最后确认 `nsh>` 存活。

逐 case 参数和内部动作见 `apps/mcu_peripheral_tests/i2c/README.md`。该 README 直接
保存命令、流程、判据和运行关键数据，不依赖外部任务日志。

## 当前限制和验收状态

- I2C1-only 的构建、裁剪、烧录、启动和真实无 target 超时路径已通过。
- I2C0-only 在 GPIO4/5 上已确认启动和无 target 地址波形。正向 target 已在 AVE-V10
  的 ES8388 上验证（见“正向 target”），M64P 上仍缺。
- 2026-10-14 起 `nsh-peripherals` 同时注册 I2C0（SCL 30、SDA 31）和 I2C1（GPIO4/5）：
  两条总线的 `hw probe --no-target`（`ret=-110`）都 PASS；
  `hw dual` 仍缺两路 target，GPIO30/31 上也还没有逻辑分析仪波形。
- M64P 的外接 EEPROM 和上拉尚未冻结，除无 target probe 外的 `hw` case 在 M64P 上均未
  取得实测 PASS。AVE-V10 上 `hw probe`、`hw combined`、单字节 `hw eeprom` 和
  `hw concurrent` 已对 ES8388 PASS；`hw boundary` 在 ES8388 上能跑完 1–1024 字节，但读出
  数据只有首字节可核对，数据内容仍需 EEPROM。
- 中断方式下实测过的是 1–1024 字节读（ES8388 只能核对字节数和首字节）、2 字节以内的写
  和地址/数据 NACK。超过 16 字节的写（中断服务续填 TX FIFO）、仲裁丢失和控制器超时没有
  实物验证：ES8388 只收 1 个数据字节，长写和读数据内容需要 EEPROM；后两者需要能制造总线
  冲突或拉住 SCL 的接线。两种方式出错中止后都不做总线恢复（不补 STOP、不发 9 个 SCL），
  `I2C_RESET` 仍不支持。
- 串口 PASS 只证明协议调用成功，不证明实际 SCL 频率、ACK/NACK、repeated-start
  波形、电气质量或 EEPROM 数据保持。
- 完成 I2C 验收仍需补齐 I2C1 实物总线、oneshot/RTC 回归；若要验收双实例，还需
  在 GPIO4/5 和 GPIO30/31 上各接一路 target。
