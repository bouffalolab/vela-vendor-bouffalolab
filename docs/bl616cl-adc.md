# BL616CL GPADC（ADC）适配与验证

本文说明 BL616CL GPADC 接入 OpenVela ADC upper-half 的范围、读数与 ioctl 合同、
两种传输方式、为 PWM 触发和 DMA 预留的结构、Kconfig 与引脚、构建裁剪和实板结果。
只写有源码、构建或实测依据的结论；需要接线的精度验收单独列出。

## 背景

BL616CL 的 GPADC 在 LHAL 中是 `adc_v2_0`（寄存器在 GLB，结果 FIFO 在 GPIP），
有 12 个外部通道：CH0–CH7 在 GPIO0–7，CH8、CH9 在 GPIO12、13，CH10、CH11 在
GPIO32、33（Ai-M64P-32S-Kit 上是 USB）。LHAL 第一次 `bflb_adc_init()` 用 efuse
trim 和内部 GND、20 mV 两点做一次校准（约 6.6 ms），之后的 init 不再校准；
`bflb_adc_parse_result()` 用这些系数把原始码换成 mV，内部用浮点。

OpenVela ADC upper-half（`drivers/analog/adc.c`）提供 `/dev/adcN`、多 fd 共享的
接收队列、`read()`、`poll()` 和 `ANIOC_*` ioctl；lower-half 用 `au_receive_batch()`
入队，用 `au_reset()` 报溢出。

## 范围

| 能力 | 处理 | 说明 |
|---|---|---|
| 外部通道单端扫描 | 已做 | 板级选通道，按通道号顺序扫描；一个 `/dev/adc0` |
| 校准后 mV 读数 | 已做 | `am_data` 为 0–3200 mV 整数 |
| 轮询单轮扫描 | 已做（默认） | 每次 `ANIOC_TRIGGER` 扫一轮并入队 |
| 中断连续扫描 | 已做（可选） | `ANIOC_TRIGGER` 启动，FIFO 中断 + HPWORK 搬运 |
| PWM 触发 | 预留 | 每个 PWM 事件扫一轮；结构见“预留” |
| DMA | 预留 | DMA0 循环环；结构见“预留” |
| 12/14 bit | 不做 | 固定 16 bit（16 倍过采样档）；12 bit 噪声大、读数偏低 |
| 差分、注入通道、PGA 增益 | 不做 | 没有 OpenVela 公共接口 |
| 内部通道（VBAT/2、温度、VREF） | 不做 | 需要另定接口 |
| 外部基准 | 不做 | 板上没有外部 1.25 V 基准 |

## 调用链与 archive 归属

```text
board_late_initialize()
  -> bl616cl_board_initialize()
  -> ai_m64p_kit_adc_initialize()
  -> bl616cl_adc_initialize(channels, nchannels)  /* 引脚、时钟、校准、扫描表 */
  -> adc_register("/dev/adc0", dev)

open / ANIOC_TRIGGER / read / poll / close
  -> OpenVela ADC upper-half
  -> BL616CL lower: ioctl / shutdown
  -> bflb_adc_*()
```

- `chips/bl616cl/bl616cl_adc.c`：lower-half，编入 `libarch.a`；
- `boards/bl616cl/ai-m64p-32s-kit/src/ai_m64p_kit_adc.c`：按 Kconfig 生成通道表，
  注册 `/dev/adc0`，编入 `libboard.a`；
- `apps/mcu_peripheral_tests/adc/`：测试 app，独立 archive；
- LHAL 的 `bflb_adc_v2.c` 一直在 `libbl_lhal.a` 里编译，ADC 关闭时不被链接。

初始化在 late bringup 完成，先于注册：GPIO 设为模拟输入（`GPIO_ANALOG |
GPIO_SMT_EN | GPIO_DRV_0`）、ADC 时钟选 XCLK 并分频、打开 GPADC 外设时钟、
`bflb_adc_init()`（含校准）、写扫描表。之后 `open()` 不再动硬件；最后一次
`close()` 停止转换、清 FIFO，时钟和校准保留。

## Kconfig 与引脚

芯片层：

```text
CONFIG_BL616CL_ADC=y              # select ANALOG、ADC
CONFIG_BL616CL_ADC_POLL=y         # 传输方式二选一（默认）
# CONFIG_BL616CL_ADC_IRQ=y        # 中断连续扫描，select SCHED_HPWORK
CONFIG_BL616CL_ADC_CLK_DIV=20     # ADC 时钟 = 40 MHz XCLK / N，20–64；轮询默认 20，中断默认 64
```

板级（Ai-M64P-32S-Kit）：`CONFIG_AI_M64P_KIT_ADC=y`（select `BL616CL_ADC`），
`CONFIG_AI_M64P_KIT_ADC_CH0`–`CH9` 逐路选择，默认只开 CH0。`nsh-peripherals`
开 CH0–CH3（GPIO0–3），这四个脚在该配置里没有别的用途。

引脚所有权在编译期检查：

- 一路都不选时报 `Select at least one ADC channel`；
- I2C、SPI0（含第二个片选）、PWM0、UART0（控制台，TX/RX 可配）任一信号配到已选
  ADC 通道的 GPIO 上时，分别报 `I2C0/I2C1 pin conflicts with an ADC input`、
  `SPI0 pin conflicts with an ADC input`、`PWM0 output conflicts with an ADC input`、
  `UART0 pin conflicts with an ADC input`；UART1 固定在 GPIO14/15，不会冲突；
- 已选通道的 GPIO 不再注册为 `/dev/gpioN` 测试节点（CH8、CH9 会占掉 GPIO12、13）。

BL616CL-AVE-V10 没有空闲 GPIO，不提供 ADC 板级选项。

## 读数与 ioctl 合同

`read()` 用 `struct adc_msg_s`（5 字节，packed）数组：`am_channel` 是该样本实际的
物理通道号（取自 FIFO 字的通道标签），`am_data` 是校准后的 mV（0–3200，输入高于
3.2 V 时饱和在 3199）。缓冲区长度取 5 的整数倍；upper-half 按长度推断消息格式，
4 字节倍数只返回数据。

| ioctl | 轮询方式 | 中断连续方式 |
|---|---|---|
| `ANIOC_TRIGGER` | 同步扫一轮，整组入队后返回 0；超时 `ETIMEDOUT`（10 ms）；队列放不下一组 `EOVERFLOW`（不入队）；样本通道顺序不对 `EIO` | 启动连续扫描并返回 0（含约 0.2 ms 的重新初始化）；已在运行或正在停止 `EBUSY` |
| `ANIOC_GET_NCHANNELS` | 返回扫描的通道数 | 同左 |
| `ANIOC_RESET_FIFO`、`ANIOC_SAMPLES_ON_READ` | upper-half 自己处理：丢弃队列、返回已入队数 | 同左 |

其它命令返回 `ENOTTY`。没有私有 ioctl。

连续方式下样本丢失（硬件 FIFO 溢出，或上层队列满）时驱动停止扫描并报溢出：
`read()` 先返回丢失前已入队的样本，然后返回一次 `EIO`，之后（非阻塞）返回
`EAGAIN`；再次 `ANIOC_TRIGGER` 从第一个通道重新开始。开 `CONFIG_DEBUG_ANALOG_WARN`
时驱动打印 `ADC scan stopped, FIFO overrun` 或 `queue full`。

upper-half 的两点限制：所有 fd 共用一个队列，一个 fd 读走的样本另一个 fd 读不到；
溢出标志只在队列空时由 `read()` 报告，`poll()` 不报告，溢出发生时正在 `poll()`
或 `read()` 等待的线程会被驱动唤醒，之后再开始的 `poll()` 要等新数据，应直接调用
`read()`。同一节点的多个线程同时 `ANIOC_TRIGGER` 不做串行化。

## 传输方式

### 轮询单轮扫描（默认）

GPADC 配成单轮扫描。`ANIOC_TRIGGER` 清 FIFO、启动转换，在调用任务里轮询 FIFO
计数直到一轮的样本到齐，读出、停止、换算、核对通道标签，再整组入队。不用中断和
工作队列。上层队列（驱动静态提供）能放两轮最多 12 个通道的样本。

4 个通道、XCLK/20 时一次触发平均约 0.7 ms，最长不到 1 ms：其中 LHAL 启动转换
固定延时 100 µs，每个 16 bit 样本约 129 µs。

### 中断连续扫描（`BL616CL_ADC_IRQ`）

GPADC 配成循环扫描，FIFO 到 8 个样本时发 FIFO_RDY 中断。中断处理在中断控制器层关掉
GPADC IRQ 并把搬运交给 HPWORK（优先级 224）。worker 读空 FIFO（最多 16 个），只清
FIFO_RDY，再读溢出标志，在任务上下文里换算（`bflb_adc_parse_result()` 用浮点，NuttX
RISC-V 进中断不保存 FPU 寄存器），核对标签，入队，再开 IRQ。

- FIFO_RDY 和 OVERRUN 在 GPADC 里一直不屏蔽：屏蔽后的状态位读出来是 0，worker 就
  看不到溢出。实测在控制器层关 IRQ 时，worker 能读到 OVERRUN。
- 溢出标志只在停止扫描时清。worker 在读空 FIFO 之后读它，所以 worker 被更高优先级
  的任务拖住期间发生的溢出也会报出来；读完之后再溢出，标志留着，IRQ 会再来一次。
- 扫描有三个状态：停止、运行、正在停止。worker 发现丢样后先置“正在停止”，这期间
  `ANIOC_TRIGGER` 返回 `EBUSY`；停完 GPADC 后，在同一个 `ad_spinlock` 临界区里标记
  溢出、回到“停止”、提交丢样前的样本（放不下就提交 0 个样本，只唤醒读者）。读者先读到
  这些样本、再读到一次 `EIO`，读到 `EIO` 时已经可以重新触发。最后一次 close 先置
  “正在停止”并关 IRQ，再等 worker 读完 FIFO，最后停 GPADC。
- worker 必须在剩下 8 个样本的时间内读走 FIFO：XCLK/20 约 1 ms，XCLK/64 约 3 ms。
  默认 XCLK/64，实测样本率约 2440/s。
- 上层队列 64 项；读者要在约 25 ms（XCLK/64）内取走数据，否则队列满、扫描停止。
- 每次启动前 `bflb_adc_deinit()` 后重新 init 和写扫描表（不重新校准，约 0.2 ms）。
  原因：清 `CONV_START` 不会结束当前一轮，GPADC 会把这一轮剩下的通道转完，这些样本
  在停止清空 FIFO 之后才进 FIFO（XCLK/64 每个约 0.41 ms）。一轮结束前重新启动，最先
  读到的就是上一轮剩下的通道。init 里的 GPADC 软复位会丢掉这一轮的剩余部分。

## 为 PWM 触发和 DMA 预留

驱动分三层，后加的方式只在前两层加代码，交付层和用户 ABI 不变：

| 层 | 现在 | 加 PWM 触发 | 加 DMA |
|---|---|---|---|
| 节拍（谁启动转换） | 轮询：每次触发一轮；中断：循环扫描 | `ADC_CMD_PWM_TRIG_EN`，扫描不循环，每个 PWM 事件一轮 | 不变 |
| 搬运（FIFO 到暂存） | 轮询：触发任务读 FIFO；中断：HPWORK 读 FIFO | 同中断 | DMA0 循环环（非 cache RAM，请求 `DMA_REQUEST_ADC`，源 `DMA_ADDR_ADC_RDR`），period 回调交给同一个 worker，用 `DMA_RESIDUAL` 判追尾 |
| 交付（共用） | `bl616cl_adc_convert()` 换算并核对标签，`bl616cl_adc_queue()` 入队；溢出停流并 `au_reset()` | 不变 | 不变 |

- Kconfig 名字已定：传输方式 choice 以后加 `BL616CL_ADC_DMA`（依赖 `BL616CL_DMA0`），
  另加 `BL616CL_ADC_PWM_TRIGGER`（依赖非轮询方式和 `BL616CL_PWM`）；DMA 通道登记到
  “DMA0 channel assignment”菜单和 `bl616cl_dma.h` 的通道表。
- PWM 触发源字段在 PWM 寄存器里（`PWM_CMD_SET_TRIG_ADC_SRC`，CH0–3 的高/低电平沿或
  周期事件），归 PWM 驱动所有，由 PWM 驱动按 Kconfig 写入。使用顺序是先
  `ANIOC_TRIGGER` 布防，再启动 PWM；PWM 停止只是没有新样本。
- 原生 SDK 例程已在 Ai-M64P 上确认：5 个 PWM 周期触发 5 轮扫描；ADC→DMA0 通路可用。
- PWM 触发方式下扫描每轮自然结束，但停止时同样可能停在一轮中间，启动前的重新 init
  对它同样适用。

## 构建与裁剪

| 配置 | ADC | 结果 |
|---|---|---|
| ai-m64p-32s-kit/nsh、wifi、ble | 关 | 构建通过，未见本改动新增 warning；`libarch.a`、`libboard.a`、`System.map` 中没有 ADC 驱动、`bflb_adc_*` 或测试符号，没有测试 archive |
| ai-m64p-32s-kit/nsh-peripherals | 轮询 + CH0–3 + 测试 | 构建通过，未见本改动新增 warning；没有中断处理和 worker 符号 |
| 临时：中断方式 + 测试 | 开 | 构建通过，未见本改动新增 warning |
| 临时：ADC 开、测试关 | 开 | 构建通过，没有测试代码 |
| 临时：只选 CH0、CH2 | 开 | 构建通过 |
| 临时：一路不选；CH4 撞 I2C1；CH6 撞 PWM CH3；CH8 撞 SPI0 CS；UART0 TX 改到 GPIO0 撞 CH0 | 开 | 均在编译期报对应错误 |
| bl616cl-ave-v10/nsh、nsh-peripherals | 无此选项 | 构建通过 |

## 实板结果（Ai-M64P-32S-Kit）

轮询方式（`nsh-peripherals`）：ADC-001、002、004、005、006、010、011 通过；ADC-002
1000 次触发每次恰好一组，平均 0.70 ms，最长 0.95 ms。只选 CH0、CH2 的临时配置同样
通过，读数标签为 0、2。打开 ADC 后 `nsh-peripherals` 其余外设全量回归 39 项全部通过；
ADC 关闭的 `nsh` 开机后没有 `/dev/adc0`。

中断方式（临时配置，XCLK/64，开 `DEBUG_ANALOG_WARN`，CH0–3 和只选 CH0 各一份）：
ADC-001、004、005、007、008、009、010、011 通过。ADC-007 三轮 10 s 各 24,413–24,422
个样本（约 2440/s），没有报错；样本数和标签只能说明没有发现丢样，单通道或整轮丢失
要靠驱动的溢出标志发现。ADC-008 每次 EIO 前驱动都报 `queue full`，读到 57–59 个
样本；ADC-009 每次都报 `FIFO overrun`，读到 16 个样本（硬件 FIFO 深度 16）。

停止和溢出的时序用临时同步点验证过（同步点不在提交里，单通道配置）：
- worker 开始后被拖住 10 ms，FIFO 在这期间溢出：驱动报 `FIFO overrun`，读者读到
  之前的 16 个样本、一次 `EIO`、然后 `EAGAIN`，再触发能继续采样。
- worker 停止扫描时在“正在停止”状态停 20 ms：这期间另一线程的 `ANIOC_TRIGGER`
  都返回 `EBUSY`；它第一次成功时 `EIO` 已经在队列里，新扫描之后 200 ms 内持续出数，
  没有被旧的停止流程停掉。
- worker 读 FIFO 前停 20 ms，期间最后一次 close：close 等 worker 读完 FIFO（16 个
  样本还在）再停 GPADC；重开后从 CH0 开始出数。

EN 复位 3 次（轮询方式，复位后直接跑 ADC-001、004）：每次都注册了 `/dev/adc0`，
ADC-001 通过，CH3 每轮均值 0–0.1 mV。悬空的 CH0、CH1 复位后最初读数偏高（第一组
约 2.5 V、2.9 V），256 组内回落到 0.65 V、0.74 V 左右；开机后先等 3 s 再采，曲线
相同，说明它跟转换次数有关、跟上电时间无关，推测是悬空引脚上的残留电荷被逐次采样
放掉。接了已知电压的通道有没有这个过程，要等 ADC-003 确认。

Wi-Fi 同开（临时配置：`wifi` 加 ADC 和测试 app；STA 连 2.4 GHz AP，后台每 10 ms
ping 一次网关；在改停止状态和溢出标志之前的驱动上测得，结论只限 STA + ping）：轮询
方式试 3 次，ADC-002 每次 4000 个样本通过，触发平均 0.63 ms，最长 0.72–0.95 ms
（Wi-Fi 空闲时 0.69 ms），ping 三次都是 400/400。中断方式 ADC-005、007（3 轮 × 10 s，
每轮 24,417–24,418 个样本，未发现丢样）、008、010、011 通过，两次的 ping 是
3999/4000、1999/2000。CH3 在轮询方式读 0；在中断方式（XCLK/64）Wi-Fi 关时读 9 mV，
有流量时 10.4–10.7 mV。这几路都不是已知电压，不能据此判断精度。

Wi-Fi 同开 iperf（最终驱动，临时配置同上；STA 连 2.4 GHz AP，每种方式 3 轮，每轮
DUT 作 TCP 发送端、接收端各 30 s，期间 ADC 用例一个接一个地跑）。吞吐都取 DUT 端：
发送取 DUT 客户端的整段报告，接收取 DUT 服务端前 6 个 5 s 区间。

- 轮询方式：ADC-002 共 54 次，全部通过，触发平均约 0.63 ms，最长 2.1–3.7 ms（用例
  上限 10 ms）。这样连续触发时 TCP 发送 15.70–15.79 Mbit/s、接收 17.43–17.53
  Mbit/s；同一镜像不跑 ADC 时是 19.31–19.35、21.41–21.45 Mbit/s，都低约 18%，推测
  是忙等占用了 CPU。
- 中断方式：发送窗口跑 ADC-007（3 × 10 s，9 段各 24,409–24,418 个样本，没有
  `EIO`），接收窗口轮流跑 008、009、011 共 21 次，全部通过。008 每次报
  `queue full`，009 每次报 `FIFO overrun`，此外没有溢出告警。009 在 `EIO` 前读到
  16 或 26 个样本：读的是上层队列，多出的应是忙等前已入队的样本，不说明硬件 FIFO 大于
  16。TCP 发送 18.42–18.47 Mbit/s、接收 20.68–20.75 Mbit/s；同一镜像不跑 ADC 时是
  19.31–19.35、21.30–21.37 Mbit/s，分别低约 4.5%、3%。

以上只说明这次负载下没有发现异常，不代表测出了 HPWORK 的最坏延迟。

免接线用例已验证，精度待测：ADC-003 要在引脚上加已知电压，两种方式都测；拔电冷启动
只做了 EN 复位。

## 硬件实测事实

- 规则 FIFO 深度 16（数据手册写 32）。满了之后新样本直接丢弃，旧样本保留；读走后
  扫描接着进行，不保证从第一个通道开始，所以必须靠每个样本的通道标签核对顺序。
  测法：GPIO0–3 配成输出、由芯片自己给 CH0–3 高低电平，4 通道连续扫描，屏蔽 ADC
  中断、不读 FIFO，测了 16 bit XCLK/20、16 bit XCLK/64、14 bit XCLK/20、12 bit
  XCLK/20 四组，各跑两次。等计数到 32，200 ms 超时也没到；再等约 600 ms，四组都是
  计数 16，`gpadc_config`（GPIP 偏移 0x0）读出 `0x0110700c`：计数字段 16，
  FIFO_FULL、FIFO_NE 置位。两组 16 bit 随后把 4 个输入全部取反，等 5 ms 后
  读出的 16 个样本都是取反前的电平；再等 3 ms 读到的是新电平，首样本 XCLK/20 时是
  CH2，XCLK/64 时是 CH0。Vela 下 ADC-009 用最高优先级线程忙等 50 ms、挡住 HPWORK
  搬运，Wi-Fi 关时读者每次在 `EIO` 前拿到 16 个样本。
- 屏蔽状态下 FIFO_RDY、OVERRUN 状态位读出 0；不屏蔽时每次溢出都会触发中断。
- FIFO_RDY 阈值 8 时，计数到 8 即触发。
- 16 bit 样本时间：XCLK/20 约 129 µs，XCLK/64 约 376 µs（连续扫描实测约 410 µs/样本）。
- 扫描模式下清 `CONV_START` 后，当前一轮剩下的通道照常转换、进 FIFO，然后才停；
  一轮结束后再启动从第一个通道开始。4 通道停在一轮内不同位置（每种 8 次），停止后
  约 1 ms 重启、只清 FIFO 时，凡是这一轮还没转完的，首样本都是上一轮剩下的通道；
  等 3 ms 后重启都从第一个通道开始。
- `SOFT_RST` 丢掉进行中的一轮，清零 RAW_RESULT；CMD1、CONFIG1/2、扫描表等配置
  寄存器不变，GPIP 的 FIFO 计数也不变（FIFO 要另外清）。拉低再拉高
  `GLOBAL_EN` 或 `SCN_EN`、重写扫描表、只重新配置（GPADC 已使能时的 init）都不会
  丢掉剩余的一轮。
- 首次 init 校准约 6.6 ms，再次 init 约 5 µs；deinit 后的完整 init（不校准）连同
  写扫描表约 0.2 ms（实测 193–288 µs）。
- 校准会改动 MIX、HBN 中几个模拟寄存器；同一代码布局下对比，未见对 Wi-Fi 吞吐的影响。
  打开 ADC 会改变代码布局，UDP 吞吐对布局敏感，比较时要用 `.text` 大小相同的镜像。

## 限制

- 只做单端外部通道、16 bit、内部参考；PWM 触发和 DMA 只有结构预留。
- 轮询方式在调用任务里忙等一轮扫描（4 通道约 0.7 ms）。
- 连续方式每次启动多约 0.2 ms（重新 init）。
- 多线程同时 `ANIOC_TRIGGER` 不串行化；所有 fd 共用一个队列。
