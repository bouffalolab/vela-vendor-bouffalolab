# MCU I2S 外设测试

`i2s_test_main.c` 编译为独立的 NSH 命令 `mcu_i2s_test`。测试只用 audio upper-half 的
公共接口：打开 `/dev/audio/pcm0`（播放）或 `/dev/audio/pcm_in0`（录音），经
`AUDIOIOC_RESERVE`、`CONFIGURE`、`ALLOCBUFFER`、`ENQUEUEBUFFER`、`START`、`STOP` 和消息
队列收发数据，不调用 I2S lower-half 或 codec 的私有接口。

## 背景与边界

两个节点由 BL616CL-AVE-V10 板级代码注册：NuttX ES8388 驱动的两个实例共用 I2S0 和板载
ES8388，codec 是 slave，BL616CL 输出 MCLK、BCLK 和 FS。喇叭只接 ROUT2（功放差分输入的
一端），只放右声道；麦克风接 LIN2，录到左声道，右声道是麦克风参考端，只有噪声。测试
播放时左右声道默认写相同数据（`-c` 只写一个声道），录音按声道统计，回放时把左声道
复制到两个声道。

自动判定有三项：

- 流能正常结束。
- 实测帧率与请求值相差不超过 2%（芯片没有音频 PLL，44.1 kHz 快 1.2%）。帧率按 buffer
  归还时间计算：从第一个归还的 buffer 算到最后一个计入的 buffer。ES8388 驱动在拿回
  最后一个 buffer 后用轮询 I2C 复位 codec，工作线程优先级 252 高于测试线程，最后两个
  buffer 的归还会晚几毫秒，所以播放不计最后两个 buffer，录音不计 `AUDIOIOC_STOP`
  之后归还的 buffer。因此一次播放至少要 4 个 buffer（每个 2048 帧）：不足时没有可计时
  的 buffer，判 `FAIL ret=-61`（`ENODATA`）；只有 4 个时只计 1 个 buffer，归还时间的
  抖动就可能超出 2%。
- 录音数据没有孤立尖峰：任一声道不能有幅度 ≥ 24576、前后两个采样都 < 4096 的采样。
  codec 的滤波器不会从真实声音产生这种单点跳变；I2S 与 codec 时序错位（符号位取自上一
  个采样，或 codec 晚 1 个 BCLK 输出）时会有几十到上万个。

喇叭是否出声、声音是否干净、录音时说话电平是否升高、回放是否可辨需要人工确认。

## 运行前检查

测试固件需要：

```text
CONFIG_AUDIO=y
CONFIG_AUDIO_ES8388=y
CONFIG_ES8388_OUTPUT_CHANNEL_LINE2=y
CONFIG_ES8388_INPUT_CHANNEL_LINE2=y
CONFIG_BL616CL_DMA0=y
CONFIG_BL616CL_AVE_V10_AUDIO=y
CONFIG_BL_MCU_PERIPHERAL_TESTS_I2S=y
```

测试依赖 `BL616CL_AVE_V10_AUDIO`，只在 AVE-V10 上编译；`AUDIO_MULTI_SESSION` 打开时
不编译（session 参数没有处理）。`bl616cl-ave-v10/nsh-peripherals` 已打开。

## 命令

```text
mcu_i2s_test [tone|rates|restart|record|final|width|all] [-r rate] [-f freq] [-t ms] [-v volume] [-c left|right] [-p]
```

| case | 内容 |
|---|---|
| `tone`（I2S-001） | 播放一个正弦，默认 48000 Hz、1000 Hz、3000 ms，幅度 -6 dBFS |
| `rates`（I2S-002） | ES8388 支持的 8 个采样率（11025、12000、16000、22050、24000、32000、44100、48000）各播 1000 ms |
| `restart`（I2S-003） | 连续 5 次播放，每次重新打开设备，检查启停后能再次出流。每次固定 14400 帧（48 kHz 下 300 ms，11.025 kHz 下 1306 ms），各采样率都是 8 个 buffer |
| `record`（I2S-004） | 从 `pcm_in0` 录音，先丢弃 codec 启动后的 36000 帧（48 kHz 0.75 s，11.025 kHz 3.3 s），丢完打印 `speak now`，再录 `-t` 指定时长（默认 3000 ms）；打印两个声道的峰值、RMS、直流、尖峰数和左声道每 250 ms 的 RMS，再打印一行 `record data spikes=<n>` 判定；`-p` 把录音放大到峰值 -6 dBFS（最多 100 倍）后从 `pcm0` 回放 |
| `final`（I2S-005） | 空流的 0 字节 FINAL，以及两个数据 buffer 后接 0/3/5 字节 FINAL；检查逐一按序归还全部 buffer，随后收到 COMPLETE，不依靠 STOP 结束；尾部不足一帧的字节丢弃 |
| `width`（I2S-006） | 分别用 24、32 bit 配置 `pcm0` 和 `pcm_in0`，`AUDIOIOC_CONFIGURE` 必须返回 `EINVAL`。I2S 驱动只支持 16 bit；ES8388 驱动接受 16–32 bit，配置成功就说明 codec 与 I2S 的位宽不一致 |
| `all`（默认） | 依次执行以上六项，`record` 不回放 |

`-r` 设采样率（`rates` 以外各项），`-f` 设正弦频率，`-v` 设输出音量 0..1000（不给时
用驱动默认值），`-c` 让单音只在一个声道（`tone`、`rates`、`restart`），`-p` 让
`record` 回放。每项打印一行 `[I2S-00N] ... PASS|FAIL ret=<errno>`，最后打印
`I2S summary: PASS|FAIL failed=<n>`。测试名只能是第一个参数：选项之后还有参数时（如
`mcu_i2s_test -r 16000 tone`）打印 `Unexpected argument` 和用法并返回失败，不执行任何
case。

人工检查建议：

```text
mcu_i2s_test tone -t 3000 -c left
mcu_i2s_test tone -t 3000 -c right
mcu_i2s_test rates
mcu_i2s_test record -t 5000 -p
```

喇叭只放右声道，第一条应无声、第二条有声。最后一条在 `speak now` 出现后对着麦克风
说话，`mic rms per 250 ms` 中说话时段应明显高于安静时段（安静时约 15），随后喇叭
回放录音。

## 实测结果

2026-10-20，BL616CL-AVE-V10 `nsh-peripherals`（ES8388 通用 MCLK 表、DMA 通道固定
分配），板子在屏蔽箱里，未人工说话：

```text
[I2S-001] tone=1000Hz rate=48000 ms=3000 buffers=71 measured=48074 ppm=1541 PASS ret=0
[I2S-002] tone=1000Hz rate=11025 ms=1000 buffers=6 measured=11175 ppm=13605 PASS ret=0
[I2S-002] tone=1000Hz rate=12000 ms=1000 buffers=6 measured=12028 ppm=2333 PASS ret=0
[I2S-002] tone=1000Hz rate=16000 ms=1000 buffers=8 measured=16030 ppm=1875 PASS ret=0
[I2S-002] tone=1000Hz rate=22050 ms=1000 buffers=11 measured=22327 ppm=12562 PASS ret=0
[I2S-002] tone=1000Hz rate=24000 ms=1000 buffers=12 measured=24038 ppm=1583 PASS ret=0
[I2S-002] tone=1000Hz rate=32000 ms=1000 buffers=16 measured=32047 ppm=1468 PASS ret=0
[I2S-002] tone=1000Hz rate=44100 ms=1000 buffers=22 measured=44634 ppm=12108 PASS ret=0
[I2S-002] tone=1000Hz rate=48000 ms=1000 buffers=24 measured=48117 ppm=2437 PASS ret=0
[I2S-003] tone=1000Hz rate=48000 ms=300 buffers=8 measured=48129 ppm=2687 PASS ret=0
（I2S-003 共 5 行，均 PASS）
  recording 3000 ms from /dev/audio/pcm_in0 after 750 ms for the codec to settle
  speak now
[I2S-004] record rate=48000 ms=3000 buffers=88 measured=48080 ppm=1666 PASS ret=0
  left peak=106 rms=19 dc=-2 spikes=0
  right peak=14 rms=2 dc=-1 spikes=0
  mic rms per 250 ms: 15 16 18 20 16 27 21 18 18 20 19 18
[I2S-004] record data spikes=0 PASS ret=0
[I2S-005] final data=0 bytes=0 returned=1/1 complete=1 PASS ret=0
[I2S-005] final data=2 bytes=0 returned=3/3 complete=1 PASS ret=0
[I2S-005] final data=2 bytes=3 returned=3/3 complete=1 PASS ret=0
[I2S-005] final data=2 bytes=5 returned=3/3 complete=1 PASS ret=0
I2S summary: PASS failed=0
```

44.1 kHz 系列快约 1.2%，是没有音频 PLL 的限制。之后连跑两次 `mcu_dma_test`（跳过
I2S 持有的 CH0/CH1，用其余 6 个通道），7 项全部 PASS，再跑 `restart` 仍 PASS。

FINAL 用例的负向验证：把 `send()` 改回拒收 0 字节 buffer，`final` 第一组打印
`final data=0 bytes=0 returned=0/1 complete=0 FAIL ret=-110`。这时 ES8388 驱动的
STOP 会一直等那个没归还的 buffer，命令不再返回，所以用例先打印结果再 STOP。

尖峰判定的验证：录音过程中用 `i2c set -b 0 -a 10 -r 8 20` 反转 codec 的 BCLK 极性，
得到 `record data spikes=15579 FAIL`；用不带 ES8388 第 4 条修复（见
`docs/bl616cl-i2s.md`）的固件按 8 个采样率轮流录音 128 次，2 次判 FAIL，正是该修复
要解决的符号位错误。带修复的固件 256 次启动没有数据错误。

2026-10-19 恢复通用 MCLK 表后，`final` 在 11025/32000/48000 Hz 下的四种边界
均 PASS，返回计数为 1/1 或 3/3，且 `complete=1`；普通播放、录音回放、DMA 七项和
I2C 三项回归通过。8 个采样率各录音启动 32 次，共 256 次通过帧率与尖峰判定；
16/24 kHz 反转 BCLK 的负向复测分别检出 12040/15663 个尖峰并判 FAIL，恢复后均 PASS。

2026-10-20 位宽检查：带 ES8388 第 5 条修复的固件 `width` 四项均 PASS，`all` 与
`record -r 16000 -t 1000 -p` 回归 PASS；去掉该修复后四项都打印 `rejected FAIL
ret=-71`（配置成功，`EPROTO`）。

2026-10-21 测试修正：`restart` 原来每次播放 300 ms，11025、16000 Hz 下只有 2、3 个
buffer，每次都判 `FAIL ret=-61`；22050 Hz 只计 1 个 buffer，5 次中 1 次 ppm=22131 判
FAIL。改为固定 14400 帧后，8 个采样率各 5 次全部 PASS，每次 8 个 buffer。
`mcu_i2s_test -r 16000 tone` 原来静默跑成 `all`，现在报 `Unexpected argument` 并返回 1。
把 `AUDIOIOC_REGISTERMQ` 的参数改成无效描述符来注入失败：检查返回值后 `tone`、`record`
立即报 `ret=-9`（`EBADF`）；不检查时要等 2 s 超时，报的是 `ret=-110`。

## 限制

- 8000 Hz 不在 ES8388 驱动的 MCLK 表里，测试不覆盖。
- 只测 16 bit；32 bit 暂不支持，原因见 `docs/bl616cl-i2s.md` 的“限制”。
- 两个节点不能同时打开流：任一实例启动都会复位整颗 codec。
- 欠载、溢出没有检测手段，测试不覆盖；`timeout` 参数驱动未实现。
- `AUDIOIOC_REGISTERMQ` 失败时该 case 以它的 errno 判 FAIL；这条路径用公共接口触发不到，测试不覆盖。
- 播放数据是否被 codec 正确接收没有自动判定，只能人工听。
