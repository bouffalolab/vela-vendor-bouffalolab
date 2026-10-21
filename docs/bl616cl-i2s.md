# BL616CL I2S0 与 ES8388 音频适配与验证

本文说明 BL616CL I2S0 lower-half（NuttX `struct i2s_dev_s`）的能力范围、时钟与引脚
规则、DMA 流程、Kconfig、NuttX ES8388 驱动的修复，以及在 BL616CL-AVE-V10 上的播放、
录音实测和限制。板级接线与上板命令见
`boards/bl616cl/bl616cl-ave-v10/README.md`，测试命令见
`apps/mcu_peripheral_tests/i2s/README.md`。

## 背景

BL616CL 有一个 I2S 控制器（I2S0），LHAL `bflb_i2s` 提供格式、分频、FIFO 和 DMA 请求
控制。BL616CL-AVE-V10 板上 I2S0 接 ES8388 codec，codec 输出经 NS4150B 功放接喇叭，
板载模拟 MEMS 麦接 codec 的 LIN2。此前 OpenVela 没有 BL616CL I2S lower-half，DMA0
adapter 也只做 mem2mem。

本次补上 DMA0 的外设方向（见 [bl616cl-dma.md](bl616cl-dma.md)），实现 I2S0 master
lower-half，接 NuttX 自带 ES8388 驱动，注册 `/dev/audio/pcm0`（播放）和
`/dev/audio/pcm_in0`（录音）。

## 最大能力交集

| 能力 | 结论 | 边界 |
|---|---|---|
| I2S0 master，Philips I2S | 纳入，已实测 | LHAL left-justified 加 1 个 BCLK 的数据延迟、FS 取反；每帧两个 32 bit slot |
| 16 bit 双声道 | 纳入，已实测 | LR merge：一个 FIFO word 存一个立体声帧，低半字是左声道 |
| 32 bit 双声道 | 暂不支持 | 不开 LR merge 时，RX FIFO 的第一个 word 有时来自右声道 slot，整段录音左右对调（实测约 7%）；驱动对 16 bit 以外的位宽返回 `-EINVAL`，ES8388 的配置随之失败 |
| 采样率 11025–48000 Hz | 纳入，已实测 | 返回实际帧率；44.1 kHz 系列快约 1.2% |
| 播放（TX DMA） | 纳入，已实测 | 初始化时申请 DMA0 通道并一直持有（默认 CH0），见 [bl616cl-dma.md](bl616cl-dma.md) 的“通道分配” |
| 录音（RX DMA） | 纳入，已实测 | 同上（默认 CH1）；不同 MCLK/FS 比例下底噪不同，见下文 |
| 收发同时进行 | lower-half 支持，未实测 | TX、RX 各有通道和队列；ES8388 的两个实例不能同时用 |
| MCLK 输出 | 纳入，已实测 | 经 CLKOUT 输出 I2S 参考时钟 |
| slave、TDM、单声道 | 不纳入 | ES8388 驱动只需要 master 双声道 |
| `timeout` 参数 | 不实现 | 与 ESP32 lower-half 相同；buffer 一定会通过 DMA 完成或出错返回 |
| FIFO 欠载/溢出检测 | 不纳入 | 不使能 I2S 中断，没有错误上报 |

## 时钟与引脚

BL616CL 没有音频 PLL。I2S 参考时钟是 WIFIPLL 320 MHz 的整数分频
（320 MHz / (N + 1)，N 为 0..63），同一个时钟既是 MCLK，也是 BCLK 分频的源：
BCLK = MCLK / (div + 2)，FS = BCLK / 64。`setmclkfrequency()` 取最接近请求值的 N，
返回实际 MCLK；`tx/rxsamplerate()` 取最接近的 BCLK 分频，返回实际帧率。

ES8388 保留通用驱动的 MCLK 表：48 kHz 系列请求 12.288 MHz，44.1 kHz 系列请求
11.2896 MHz。帧长 64 BCLK，不同采样率采用不同的 BCLK 分频：

| 请求 | 请求 MCLK | 实际 MCLK | MCLK/FS | 实际帧率 | 偏差 |
|---:|---:|---:|---:|---:|---:|
| 48000 | 12.288 MHz | 12.3077 MHz | 256 | 48077 | +0.16% |
| 44100 | 11.2896 MHz | 11.4286 MHz | 256 | 44643 | +1.23% |
| 32000 | 12.288 MHz | 12.3077 MHz | 384 | 32051 | +0.16% |
| 24000 | 12.288 MHz | 12.3077 MHz | 512 | 24038 | +0.16% |
| 22050 | 11.2896 MHz | 11.4286 MHz | 512 | 22321 | +1.23% |
| 16000 | 12.288 MHz | 12.3077 MHz | 768 | 16026 | +0.16% |
| 12000 | 12.288 MHz | 12.3077 MHz | 1024 | 12019 | +0.16% |
| 11025 | 11.2896 MHz | 11.4286 MHz | 1024 | 11161 | +1.23% |

引脚规则来自 pinmux 表，驱动初始化时校验：

- I2S 功能（function 3）按 GPIO 号 `n % 4` 固定：0 BCLK、1 FS、2 DI、3 DO，没有交换。
- MCLK 只能经 CLKOUT 输出（function 31）。任意 GPIO 都能输出，但 GPIO n 只对应
  `CLKOUT[n % 4]`；驱动把这一路选成 I2S 参考时钟，该路不能再给别的功能用。AVE-V10 的
  MCLK 在 GPIO1（CLKOUT1）；GPIO3 的 CLKOUT3 是摄像头 MCLK。

## 声道顺序

LHAL 没有 Philips I2S 模式。left-justified 在 FS 为高时传左声道，Philips I2S 在 FS 为低
时传左声道，所以驱动在 left-justified 加 1 个 BCLK 数据延迟之外，把 FS 输出取反
（`I2S_IO_CONFIG` 的 `FS_INV`；`bflb_i2s_init()` 会清掉它，初始化后再置位）。不取反时
左右声道对调：播放的左声道进喇叭，录音的麦克风落在右声道。

当前 LHAL 镜像尚无 FS 反相命令，暂由 lower-half 在初始化后设置这一位；LHAL 提供
`I2S_CMD_FS_INVERSE` 后改用该接口。

## 调用链与代码归属

```text
/dev/audio/pcm0、/dev/audio/pcm_in0      audio upper-half
  -> nuttx/drivers/audio/es8388.c         两个驱动实例，codec 经 I2C0 配置
  -> I2S_SETMCLKFREQUENCY / *SAMPLERATE / *DATAWIDTH / I2S_SEND / I2S_RECEIVE
  -> chips/bl616cl/bl616cl_i2s.c          I2S0 lower-half
  -> DMA0（chips/bl616cl/bl616cl_dma.c）+ LHAL bflb_i2s、GLB 时钟与 CLKOUT
```

板级入口 `boards/bl616cl/bl616cl-ave-v10/src/bl616cl_ave_v10_audio.c`：取 I2C0（与
`/dev/i2c0` 共用，引用计数）、调用 `bl616cl_i2sbus_initialize()`、创建两个 ES8388
实例并注册。

## 缓冲与 DMA 流程

- 初始化时申请并配置两个 DMA0 通道（默认 TX CH0、RX CH1），之后一直持有，停流也
  不归还；通道号在 `DMA0 channel assignment` 菜单里设置。
- `send()` 和 `receive()` 引用 buffer，播放先 clean D-cache，录音先 invalidate，然后
  排队。方向空闲时清 FIFO、连上 DMA 请求，启动第一段后再使能该方向和 master。
- 播放接受 0 字节 buffer（包括 `AUDIO_APB_FINAL`），按队列顺序完成而不启动 DMA；
  不足一个立体声帧的尾部字节丢弃。空流不打开数据请求。
- 一次 DMA 最多 4095 个 32 bit word，大 buffer 分段传。DMA 完成回调在中断里立即启动
  下一段或下一个 buffer，FIFO（16 word）在 48 kHz 下约 333 µs，中断内续传来得及。
- 完成的 buffer 交给 HPWORK：录音 buffer 再 invalidate 一次并填 `nbytes`，先归还
  驱动内部的 buffer 槽位，再调用 client 回调、`apb_free()`。其余槽位都被占用时，
  回调里送下一个 buffer 也能用上刚归还的槽位，不会卡住。回调和释放都在任务上下文。
- 两个队列都空时，worker 关掉该方向和它的 DMA 请求，另一方向也停了才关 master。
  这些状态都在自旋锁里改，worker 不拿互斥锁，不会被持锁的 `send()`/`receive()` 挡住。
  BCLK 和 FS 只在有流时输出，ES8388 依赖这一点（见驱动修复第 4 条）。
- 录音 buffer 只用整 cache line，长度向下取整到 cache line 和帧长，避免 CPU 与 DMA
  共享 cache line。
- 有流在跑时改 MCLK 或采样率，返回 `-EBUSY`（MCLK）或 0（采样率）。

## Kconfig 与裁剪

| 选项 | 默认 | 作用 |
|---|---:|---|
| `CONFIG_BL616CL_I2S` | n | 编译 I2S0 lower-half；依赖 `AUDIO`、`BL616CL_DMA0`，选择 `I2S`、`SCHED_HPWORK` |
| `CONFIG_BL616CL_I2S_TX_DMA_CHANNEL` | 0 | 播放用的 DMA0 通道，在 `DMA0 channel assignment` 菜单里 |
| `CONFIG_BL616CL_I2S_RX_DMA_CHANNEL` | 1 | 录音用的 DMA0 通道，同上；与其它 SDK 驱动重复时编译失败 |
| `CONFIG_BL616CL_I2S_NBUFFERS` | 4 | 驱动同时持有的 buffer 数，两个方向共用 |
| `CONFIG_BL616CL_AVE_V10_AUDIO` | n | AVE-V10 板级注册 `pcm0`、`pcm_in0`；依赖 `AUDIO_ES8388`、`BL616CL_DMA0`，选择 I2C0 和 `BL616CL_I2S` |
| `CONFIG_ES8388_OUTPUT_CHANNEL_LINE2` | — | AVE-V10 `nsh-peripherals` 使用：只开 LOUT2/ROUT2，ROUT2 进功放；其它选择的效果见板级 README |
| `CONFIG_ES8388_INPUT_CHANNEL_LINE2` | — | AVE-V10 `nsh-peripherals` 使用：麦克风在 LIN2；其它选择的效果见板级 README |
| `CONFIG_BL_MCU_PERIPHERAL_TESTS_I2S` | n | 编译 `mcu_i2s_test`；依赖 `BL616CL_AVE_V10_AUDIO` |

功放的两个输入分别接 ROUT1、ROUT2，两路信号同相，同时打开（输出 Both）会在功放输入端
抵消、喇叭几乎无声，所以 `nsh-peripherals` 只开第二组。`bl616cl-ave-v10/nsh-peripherals` 打开以上全部选项；`nsh` 配置和
Ai-M64P-32S-Kit 的配置都不编译 I2S。

## NuttX ES8388 驱动修复

本板接入 `drivers/audio/es8388.c` 需要以下五项通用修复，集成固件时须确认所用 NuttX
包含这些改动。2026-10-17 检查时，上游 open-vela dev 和 Apache NuttX 尚无对应修复（第 5
项为 2026-10-20 检查）：

1. `es8388_readreg()` 第二条 I2C 消息长度写成 12，而缓冲只有 1 字节，读寄存器会越界
   写栈。改为读 1 字节。
2. `configure` 先保存请求的采样率、声道和位宽，再调用 `es8388_reset()`，后者把它们
   改回 44100/2/16，任何请求都按 44.1 kHz 跑。改为先 reset 再保存和设置。
3. 启动时无条件打开四路输出，复位时的输出通道选择也把 LOUT1 与 ROUT2 的位弄反，
   `CONFIG_ES8388_OUTPUT_CHANNEL` 不起作用。改为按配置只开对应的一组。
4. `start` 只在 `DACCONTROL21` 变化时复位状态机，而 `reset` 已写过相同的值，所以从不
   复位：状态机在 `reset` 里就已放开，之后才改 MCLK、采样率和格式。约 2% 的录音流里，
   每个采样的符号位来自上一个采样，值在过零处跳到接近满幅，整条流都错。改为每次
   `start` 都复位一次状态机和数字部分。ES8388 用户指南的录音、放音启动顺序都是先写
   `0x02=0xF3` 压住状态机和数字部分，设完格式和 MCLK/LRCK 比率后最后一步才放开；原生
   SDK 的 codec 初始化也是配置完才放开。复位发生在 DAC、ADC 上电和取消静音之前；本板
   A/B 盲听，每次复位与原逻辑在启停时都只有同样很轻的咔声，听不出差别。
5. `configure` 接受 16–32 bit，但不检查 I2S 驱动设置位宽的返回值。I2S 驱动不支持该位宽
   时，codec 按新位宽工作、I2S 仍按旧位宽，配置却返回成功，数据被错读。改为与 es8311
   相同：I2S 返回负的 errno 时 `configure` 失败，且不改 codec 的字长。树内其它 I2S
   驱动返回 0、位数或比特率，不受影响。

另外合入了上游的日志格式符修复，去掉编译 warning。仍未修、本板不受影响的问题：左声道
音量的 dB 计算用了右声道的值；`cancelbuffer` 为空实现；输入、输出两个实例共用一颗
codec，任一实例启动都会复位整颗芯片。

MCLK 表保持原样。本板在 MCLK/FS 为 256（44.1/48 kHz）时，安静环境麦克风 RMS 约
16；其它采样率使用 384–1024，比值不同时麦克风底噪升高，实测 RMS 约 800–4300
（满幅 32767，约 −32 至 −18 dBFS）。删除额外的 DLL 寄存器写入未消除这一现象，
内部机理仍未查明。其它比例下播放、录音可以工作，这项底噪差异作为本板限制记录。

## 实板验证

固件：BL616CL-AVE-V10 `nsh-peripherals`，编译器 0 warning。上电后 `ls /dev/audio`
列出 `pcm0`、`pcm_in0`。`mcu_i2s_test` 全部 PASS，实测帧率（buffer 归还时间推算，
存在计时误差）与上表一致。2026-10-19 恢复通用 MCLK 表后的播放结果：

| 请求 | 实测 | 请求 | 实测 |
|---:|---:|---:|---:|
| 48000 | 48066–48075 | 44100 | 44686 |
| 32000 | 32047 | 22050 | 22328 |
| 24000 | 24038 | 11025 | 11175 |
| 16000 | 16031 | 12000 | 12029 |

连续 5 次 300 ms 启停全部 PASS。48 kHz 录音时麦克风（左声道）RMS 16、参考端（右声道）
RMS 2；16 kHz 录音时分别为 1180、6（满幅 32767），录音回放流程 PASS。空 FINAL
测试覆盖空流、两个数据 buffer 后接 0/3/5 字节 FINAL，在 11025/32000/48000 Hz 下
均按顺序归还全部 buffer，再收到 COMPLETE。之后
`mcu_dma_test` 7 项 PASS，`mcu_i2c_test` 对 ES8388 的 probe、combined、concurrent
PASS。

此前排查启动错位时，压力测试按 8 个采样率轮流录音 300 ms，每次重新打开设备。
以下三组历史结果均使用降噪实验的 256 fs 配置，不代表恢复通用 MCLK 表后的结果：

| 版本 | 启动次数 | 数据错误 | 现象 |
|---|---:|---:|---|
| 修复第 4 条之前 | 128 | 2 | 一个声道的符号位来自上一个采样 |
| BCLK/FS 一直输出，每次 `start` 复位 codec（已放弃） | 256 | 6 | 两个声道都晚 1 个 BCLK，读到的最高位恒为 0 |
| 有流才输出 BCLK/FS，每次 `start` 复位 codec | 256 | 0 | — |

第一行用的是 `mcu_i2s_test` 的尖峰判定（见测试 README），出错的两次都判 FAIL。后两行
在加入该判定之前跑，按峰值离线判定：正常时两个声道峰值都在 320 以下，出错时达到满幅。
第二行说明在 BCLK/FS 运行时复位 codec 能消除符号位错误，却有约 2.5% 的流 codec 晚
1 个 BCLK 输出，所以驱动仍在有流时才输出 BCLK/FS。

2026-10-19 恢复通用 MCLK 表后，8 个采样率各启动录音 32 次，共 256 次均通过帧率与
尖峰判定，两个声道均未检出孤立尖峰。非 256 fs 下的高底噪仍保留，测试不把底噪
RMS 升高当成功能失败。

负向检查：录音中用 `i2c set -b 0 -a 10 -r 8 20` 反转 codec 的 BCLK 极性，两个声道共
15579 个尖峰，判 FAIL；恢复后再录 PASS。

恢复通用表后的负向复测使用高底噪的 16/24 kHz：同样反转 BCLK，分别检出 12040、
15663 个尖峰并判 FAIL；恢复寄存器后，两种采样率均 spikes=0、PASS。这验证了该
判定在这两种高底噪配置下仍能检出注入的位对齐错误，不代表覆盖所有数据错误。

2026-10-19 人工试听：`mcu_i2s_test tone -t 3000 -c left` 无声、`-c right` 有声，声道顺序
正确；`mcu_i2s_test record -t 5000 -p` 在 `speak now` 后说话，回放能听清，但底噪明显。
回放会把录音放大到峰值 −6 dBFS（最多 100 倍），麦克风本底噪声（安静时 RMS 约 15）一起
放大，测试也不做降噪。

2026-10-21 与 BLE 共存：临时配置在 `nsh` 上同时打开音频和 BLE，本板跑 `mible` 外设
角色，与对端连续收发（该用例本身限速，本板发约 1.6 KB/s、收约 0.4 KB/s）。用 `trace`
（noteram）抓 48 kHz 流中的调度和中断，每个 buffer（2048 帧，42.7 ms）HPWORK 里的
I2S worker 跑一次。DMA 中断后 worker 要等到下一个 tick 才运行（最长 1 ms）。

| 场景 | worker 每次 CPU 时间（均值/最大） | DMA 中断耗时（均值/最大） |
|---|---:|---:|
| 放音，无 BLE | 61/65 µs | 17/19 µs |
| 录音，无 BLE | 72/73 µs | 19/20 µs |
| 放音，BLE 收发 | 98/122 µs | 44/58 µs |
| 录音，BLE 收发 | 118/141 µs | 44/65 µs |

trace 窗口合计：无 BLE 各 0.64 s（15 个 buffer），BLE 收发各约 1.5 s（34 个）。trace
记录本身有开销，表中数字偏大。开 BLE 后 worker 和 DMA 中断都变慢，期间没有
中断嵌套，原因未查。worker 会被 ES8388 工作线程（优先级 252）抢占，每个 buffer 内
优先级高于 BLE controller 线程（200）的线程合计最多占用 391 µs。BLE controller 中断
到其线程开始运行均值 6–9 µs，碰上 I2S 处理时最长 177 µs。放音、录音各与本板发、
本板收组合，每种跑 2 次（音频 10 s，BLE 收发约 25 s，覆盖整段音频），BLE 数据字节数
和校验和全部一致、无断连，音频全部 PASS；BLE 收发期间跑 `restart` 也 PASS。

流结束时 ES8388 工作线程复位 codec，连续占用 CPU 约 4.4 ms，期间优先级低于 252 的
线程（含 HPWORK 和 BLE controller 线程）都要等。上面 13 次流结束都发生在 BLE 收发
期间，没有出错；trace 只抓到其中一次，那 4.4 ms 内恰好没有 BLE 中断。

同日与 BL616 对端按 mible 用例对测：广播、扫描、外设打流和中心打流（双向各 30 s）、
连接断开循环（12 次），分别在无音频、48 kHz 放音、48 kHz 录音（每段 15 s 循环）、
连续启停流（11025 Hz `restart` 循环，共 70 次停流，只配断开循环）下运行。广播、扫描
收到的包数在 250 个中 232–246，有无音频相差不超过 ±5；打流数据完整、无断连，重连
12/12；音频全部 PASS。另在流进行中定时读 `fifo_config_0`（0x2000ab80）的锁存错误位
（开流时清零，置位后保持）：放音、录音分别与上述 BLE 用例同时跑共 8 组，流内读到的值
只有 0x101、0x102，没有欠载或溢出。mible 打流本身限速，结果不代表 BLE 满速时的情况。

## 限制

- 44.1/22.05/11.025 kHz 快约 1.23%（音调偏高约 21 音分），是没有音频 PLL 的硬限制。
- 非 256 fs 配置下本板录音底噪升高，见前文测量范围。
- codec 启动后约 32000 帧内 ADC 输出有大幅瞬态（直流建立；48 kHz 约 0.7 s，12 kHz 约
  2.7 s），录音应丢弃开头。
- 右声道是麦克风参考端（RIN2），只有噪声；改寄存器试过 LIN2−RIN2 差分输入，噪声更大，不用。
  驱动的 Differential 选项取的是 LIN1−RIN1（喇叭回授），不是这条通路。
- 8000 Hz 不在 ES8388 驱动的 MCLK 表里，驱动不会设置 MCLK，不支持。
- 播放方向没有数据错误的自动检测（喇叭输出无法回采），codec 时序问题是在录音方向发现
  和验证的。
- `pcm0` 与 `pcm_in0` 不能同时用。
- ES8388 驱动用轮询 I2C 写 codec。开流的配置在调用 ioctl 的线程里执行；流结束时的
  复位在优先级 252 的工作线程里执行，连续占用 CPU 约 4.4 ms，期间 BLE controller
  线程也要等（见实板验证）。
- 没有欠载/溢出检测；播放数据供不上时 FIFO 欠载，没有错误上报。
- 下一段 DMA 由完成中断启动，48 kHz 下 FIFO 只能撑约 333 µs。中断不嵌套，BLE 中断实测
  最长 337 µs，与这段余量相当，碰上时可能欠载或溢出；对测中没有出现。
- 只支持 16 bit 采样。32 bit 不开 LR merge 时录音偶发整段左右对调（2026-10-20：连续 40
  次录音 3 次；每次开流前软复位 I2S 后 40 次 35 次），起始 slot 由 I2S 内部状态决定，原因
  未查明；播放方向是否同样对调未验证。找到可靠的对齐方法前不开放 32 bit。
