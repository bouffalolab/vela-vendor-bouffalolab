# MCU PWM 外设测试

本目录验证 BL616CL PWM controller 与 OpenVela PWM upper-half 的首版能力交集。
`pwm_test_main.c` 编译为独立测试 archive，注册 NSH 命令 `mcu_pwm_test`；它不进入
`libarch.a`。chip lower、板级注册和 PWM LHAL 对象由各自 Kconfig 控制并进入对应内核
archive。测试 app 打开 `/dev/pwm0`，输出引脚由板级 Kconfig 决定。PWM-001～008 针对
主通道（编号最小的已配通道），PWM-009 验证多通道和互补输出。`nsh-peripherals` 的分配为
CH0 正 8/负 9、CH1 正 10/负 11、CH2 只用负输出 21、CH3 正 6/负 7，主通道为 CH0。

覆盖整数频率、连续输出、b16 duty、`cpol`/`dcpol`、运行中更新、多 fd 生命周期、
timeout 清理、多通道（`PWM_MULTICHAN`）和互补输出。pulse count、deadtime、brake、
center-aligned、ADC trigger、fixed-point frequency 和 PWM DMA 不属于本轮声明。

## 配置与裁剪

产品配置只启用：

```text
CONFIG_PWM=y
CONFIG_BL616CL_PWM=y
CONFIG_AI_M64P_KIT_PWM=y
```

并用 `CONFIG_AI_M64P_KIT_PWM_CHn`、`_POS_PIN`、`_NEG_PIN` 选通道和引脚（默认只开 CH3、
正输出 GPIO11）。所有通道都不配负脚时用单端布局（正输出 `pin%4==ch`），有负脚时用
差分布局（正 `pin%8==2*ch`、负 `pin%8==2*ch+1`），见 `docs/bl616cl-pwm.md`。选多个
通道时还要启用 `CONFIG_PWM_MULTICHAN` 和 `CONFIG_PWM_NCHANNELS`（PWM-009 需要不小于
2）。

测试配置再启用：

```text
CONFIG_BL_MCU_PERIPHERAL_TESTS_PWM=y
```

测试 app 自动选择 `CONFIG_BL616CL_PWM_TEST=y`，从而编入诊断和 timeout fault hook；
产品配置必须同时关闭测试 app 与 hook。lower 与 `PWM_PULSECOUNT`、
`PWM_FREQUENCY_FIXED`、`PWM_DEADTIME` 不兼容，Kconfig 不允许形成这些 ABI 组合。
启用板级 PWM 后，已配的 PWM 引脚从 GPIO 测试节点移交给 PWM；I2C、SPI、UART1 任一信号
配置到这些引脚都必须在编译期失败。

## 运行前检查

1. 固件启动后确认出现 `NuttShell (NSH)` 和 `nsh>`。
2. 执行 `help`，必须能找到 `mcu_pwm_test`。
3. 执行 `ls /dev`，必须有 `pwm0`，不能有 `pwm1` 和已配 PWM 引脚的 `gpioN`。
4. 纯软件合同可执行 `mcu_pwm_test -c all`；该命令会真实驱动所有已配 PWM 引脚。
5. 波形验收时将逻辑分析仪或示波器探头接已配 PWM 引脚，共地，量程适配 3.3V。
6. USB2 串口日志、板载 LED 和软件 PASS 不能替代频率、占空比、停止电平或毛刺证据。

## 命令

```text
mcu_pwm_test -h
mcu_pwm_test -c all
mcu_pwm_test -c 001
mcu_pwm_test -c 002
mcu_pwm_test -c 003 -w 3
mcu_pwm_test -c 004 -w 3
mcu_pwm_test -c 005 -w 3
mcu_pwm_test -c 006 -w 3
mcu_pwm_test -c 007
mcu_pwm_test -c 008
mcu_pwm_test -c 009 -w 3
```

启动时先打印每个已配通道的正/负引脚（-1 表示不用）和主通道编号。`-d` 可覆盖 PWM 节点，
默认 `/dev/pwm0`；`-w` 是每个可观测波形档位的保持秒数，默认
1 秒；`-v` 打印每个寄存器诊断点。指定单个 case 时任一步失败立即返回非零；`all` 会
继续执行并在末尾汇总。

## PWM-001 open/close 与多 fd 生命周期

背景：PWM upper-half 只在第一次 open 调用 lower `setup`，只在最后一次 close 调用
`shutdown`。中间 fd 关闭不能停止仍由其他 fd 使用的输出。

命令：`mcu_pwm_test -c 001`

流程：

1. 清空 test-only 诊断计数，第一次打开 `/dev/pwm0`；`setup_calls` 必须从 0 变为 1。
2. 第二次打开同一节点；`setup_calls` 必须仍为 1。
3. 用 fd1 设置 1kHz、50%、CPOL_HIGH/DCPOL_LOW 并 START。
4. 读诊断，通道和 lower `started` 必须为 true。
5. 关闭 fd1；用 fd2 GET 已保存配置，通道必须仍在运行。
6. 用 fd2 STOP；重复 STOP 必须无害。关闭最后一个 fd。
7. `shutdown_calls` 必须恰为 1，通道、pin、clock 和 lower `started` 必须全部清理。

完成判据：首开/末关各一次，中间 close 不停止输出，最终无资源残留。

## PWM-002 无配置启动与非法请求

背景：lower 必须拒绝无 characteristics、零/过高频率及未定义 polarity。OpenVela upper
会先保存 SET 参数再调用 lower，因此失败后的 GET 行为必须按真实 ABI 记录。

命令：`mcu_pwm_test -c 002`

流程：

1. 新固件首次运行时新开 fd，不执行 SET，直接 START；必须返回 `EINVAL`。由于
   OpenVela upper 会跨 close 保留 characteristics，重复运行时若 GET 已有非零频率，
   程序会明确打印 SKIP；需要重启固件重新取得零状态证据。
2. 停止态 SET `frequency=0`；SET 本身返回成功，GET 读回零值，随后 START 返回 `EINVAL`。
3. 对 `frequency=UINT32_MAX` 重复上一步；START 必须返回 `ERANGE`，证明超过
   `PBCLK/2` 的请求被拒绝。
4. 分别用 `PWM_CPOL_NDEF`、`PWM_DCPOL_NDEF` 和范围外 polarity 重复停止态 SET/START。
5. 设置有效 1kHz/50% 并 START，保存硬件诊断快照。
6. 运行态 SET 零频率；SET 必须返回 `EINVAL`，硬件诊断不得切换到非法参数，但 GET
   必须显示 upper 已覆盖为零频率。
7. 运行态重新 SET 有效配置恢复，再 STOP、close，并检查资源清理。

`duty` 的 ABI 类型是 `ub16_t`，所有可表达值 0..65535 都是有效边界，没有可构造的
“大于最大值”输入；0 和 65535 由 PWM-004 覆盖，不能伪造不存在的非法 duty case。

完成判据：所有 errno 与 upper 状态覆盖语义一致，非法请求不进入硬件输出。

## PWM-003 频率、占空比与寄存器回读

背景：验证 PBCLK、divider、period 和 threshold 的求解结果，不以 ioctl 成功代替硬件
配置正确。

命令：`mcu_pwm_test -c 003 -w 3 -v`

流程：

1. 依次设置 100Hz/75%、1kHz/25%、1kHz/50%、1kHz/75%、10kHz/25%。
2. 每点 SET 后 START；后续 SET 在运行中触发 live update。
3. test-only 诊断回读 source clock、divider、period、threshold 和实际频率。
4. 检查 divider/period 在 1..65535 和 2..65535；divider 必须是放得下 16-bit period 的
   最小值，period 为 `round(source/(frequency*divider))`；threshold low 为 0，high
   小于 period。
5. 由 `source/(divider*period)` 重算频率，软件误差必须不超过 1%；由
   `(high-low)/period` 重算 duty，量化误差必须不超过一个 counter tick。
6. 每点保持 `-w` 秒供外部仪器记录，全部结束后 STOP、close。
7. test-only 诊断的求解次数必须为 3：只有频率改变（100Hz、1kHz、10kHz）时才重新
   求解，同频率只改 duty 时沿用上次结果。

完成判据：寄存器回读满足求解和量化合同；G4 还要求仪器频率误差不超过 1%、占空比
误差不超过 2 个百分点。

## PWM-004 duty 全边界与 live update

背景：验证 0、常用档位和 b16 最大值的映射，特别避免把 65535/65536 误写成 100%。

命令：`mcu_pwm_test -c 004 -w 3 -v`

流程：

1. 固定 1kHz，按 0、25%、50%、75%、65535/65536 依次 SET。
2. 首点 START；其余点在运行态 SET，逐次读取 threshold。
3. duty=0 必须得到 `(low, high)=(0,0)`。
4. 中间档位的 `high-low` 必须符合 period 量化结果。
5. ABI 最大 duty 的 high 必须为 `period-1`，不得声明恒定 100% 高电平。
6. 再更新回 25%，证明最大边界后仍能恢复；最后 STOP、close。
7. 六次 SET 频率相同，test-only 诊断的求解次数必须为 1。

完成判据：所有 threshold 边界成立，更新后硬件与请求一致且最终无资源残留。

## PWM-005 CPOL/DCPOL 与停止电平

背景：`cpol` 决定正向通道 active polarity，`dcpol` 需结合 `cpol` 转换成硬件
active/inactive stop-state，不能把枚举值直接写入 bit。

命令：`mcu_pwm_test -c 005 -w 3 -v`

流程：

1. 固定 1kHz/50%，遍历 LOW/LOW、LOW/HIGH、HIGH/LOW、HIGH/HIGH 四种组合。
2. 每种组合 SET/START 后回读 CONFIG1 诊断；active-high bit 必须等于 `cpol==HIGH`。
3. stop-active 必须满足 `(cpol==HIGH && dcpol==HIGH) ||
   (cpol==LOW && dcpol==LOW)`。
4. 每种组合保持 `-w` 秒；仪器记录运行波形初始极性。
5. STOP 后从 GPIO 输入寄存器读回所有已配引脚：正输出必须等于该通道 dcpol，负输出必须
   相反（STOP 后引脚是 GPIO 输出，输入寄存器才跟随引脚）；重新 START 后进入下一组。
6. 最后一组 close 后再次记录停止电平和资源清理状态。

完成判据：四种寄存器映射和停止电平回读全部正确；实物阶段还需仪器确认 STOP/close 电平
与请求一致。

## PWM-006 同 divider 与跨 divider 更新

背景：同 divider 的 duty 更新应走一次 update gate；跨 divider 允许受控 stop/reinit，
本轮不宣称跨 divider 无毛刺。

命令：`mcu_pwm_test -c 006 -w 3 -v`

流程：

1. START 1kHz/25%，记录 divider；重复 START 必须成功且不重复 lower start。
2. 运行中更新为 1kHz/75%；divider 必须不变，threshold 更新，频率保持。
3. 更新为 100Hz/50%，再更新为 10kHz/50%；至少一处 divider 必须改变。
4. 仪器记录同 divider 更新，异常短脉冲不得超过一个 PWM 周期。
5. 跨 divider 记录允许存在的停顿，不作无毛刺结论。
6. STOP 后重复 STOP 必须成功；再 START/STOP 一次，证明生命周期可恢复。

完成判据：两条更新路径均成功，重复 START/STOP 幂等，最终资源清理。

## PWM-007 LHAL timeout、强制清理与 active close 恢复

背景：LHAL `init/start/stop/deinit` 返回 void，并在等待硬件状态超过 100ms 后静默返回；
chip adapter 必须回读状态并转成 `ETIMEDOUT`。

命令：`mcu_pwm_test -c 007`

流程：

1. 正常 open 和 SET 后注入 init 状态 timeout；START 必须返回 `ETIMEDOUT`，通道、pin、
   clock、started 必须全为 false。setup 只取得资源，LHAL init 在 START 路径执行。
2. 清除 fault 并正常 open/SET；注入 start timeout，START 必须返回 `ETIMEDOUT`，且同样
   强制清理。清除 fault 后必须能重新 START。
3. 注入 stop timeout，STOP 必须返回 `ETIMEDOUT`；虽然 upper 无条件清 started，lower 也
   必须关闭通道并释放 pin/clock。清除 fault 后重新 START/STOP。
4. 重新 START，注入独立 deinit timeout 后 close；shutdown 会走 deinit 状态门禁。
   OpenVela close 固定返回成功，不能期待 close 传播 errno，只通过内部诊断检查
   `last_error=ETIMEDOUT` 和强制清理。
5. 清除所有 fault，重新 open 后直接 START；lower `start_calls` 必须增加一次且输出状态
   恢复，再 STOP/close。该步骤验证 active close 后 upper/lower 生命周期一致。

完成判据：init/start/stop ioctl 的 errno 和所有清理位正确；close 只检查诊断，不虚构
shutdown errno 传播；active close 后 reopen+START 必须真实调用 lower 并恢复输出状态。

## PWM-008 owner、节点与关闭态裁剪

背景：每个 PWM 引脚只能由 PWM 或 GPIO/I2C/SPI/UART1 一方拥有；测试配置还必须证明
节点和 test-only 资源按配置出现。

命令：`mcu_pwm_test -c 008`

流程：

1. 打开 `/dev/pwm0` 必须成功；打开 `/dev/pwm1` 必须失败并返回 `ENOENT`。
2. 打开每个已配 PWM 引脚的 `/dev/gpioN` 必须失败并返回 `ENOENT`，证明 GPIO 测试节点
   已移除。
3. 设置并启动 1kHz/50%，诊断回读的 GLB `reg_pwm1_io_sel` 必须在有负脚时为 1、全是正
   输出时为 0，并打印 `pin layout`；STOP、close 后 channel/pin/clock/started 全部清理。
4. 配置期分别让 I2C0/I2C1 SCL/SDA、SPI0 CLK/MISO/MOSI/CS0/CS1、UART1 TX/RX 使用已配
   PWM 引脚；每组必须在编译期失败。PWM 引脚不在所选通道槽位（单端正 `pin%4==ch`；
   差分正 `pin%8==2*ch`、负 `pin%8==2*ch+1`）、一个通道都没选，或不开 `PWM_MULTICHAN`
   时选了多个通道，也必须
   编译失败。合法默认 pin 配置必须 clean build 通过。
5. off/product/test 三组制品分别检查 source、archive、map、ELF symbol 和 `help`：off
   无 lower/LHAL/board/app/hook；product 有 `/dev/pwm0` 但无 app/hook；test 才有全部测试符号。
6. board 注册失败路径只做代码审查：失败前 lower 必须是静态、未 acquire 状态。不要在
   运行时重复 `pwm_register()`，因为通用 upper 的已知 allocation leak 会污染测试环境。

完成判据：运行时 owner 和清理断言通过，配置冲突矩阵与三态裁剪证据完整。

## PWM-009 多通道与互补输出

背景：四个通道共用 frequency，各自 duty；负输出是正输出的反相。START 中没提到的通道
必须保持原状态，非法通道请求不得改动硬件。只在 `PWM_MULTICHAN` 开启且已配不少于两个
通道时运行，否则 SKIP。

命令：`mcu_pwm_test -c 009 -w 3 -v`

流程：

1. 全部已配通道按通道顺序取 20%、40%、60%、80%（便于在仪器上区分），cpol HIGH、
   dcpol HIGH，1kHz START，逐通道回读 threshold、PEN/NEN、polarity 和 stop-state；
   STOP 后读回引脚：正输出为 1、负输出为 0。
2. dcpol 改为 LOW，再次全部通道 1kHz START 并回读，保持 `-w` 秒。
3. 只更新最后一个通道到 90%（请求里第一项 `channel=0` 跳过）；其余通道 threshold
   不变，不重新求解频率，保持 `-w` 秒。
4. 只提第一个通道，改为 10kHz/50%；divider 必须改变，其余通道按原 duty 在新 period
   上恢复，保持 `-w` 秒。
5. 未知通道号、未配置通道（全部已配时 SKIP）、重复通道、空请求都必须返回 `EINVAL`，
   且硬件诊断不变。
6. STOP 后读回引脚：正输出为 0、负输出为 1；close 后资源清理。

完成判据：所有回读与 errno 断言通过；实物阶段由仪器确认各通道频率一致、duty 正确、
负输出与正输出逐样本反相，以及 STOP 电平。

## 当前实测数据

2026-10-13 多通道版本在 `nsh-peripherals`（4 通道，见开头分配）上执行
`mcu_pwm_test -c all -w 1 -v`：PWM-001～009 共 9/9 PASS，PWM-009 的两次 STOP 回读
GPIO6–11、21 的电平都与 dcpol 一致。不开 `PWM_MULTICHAN`、只配 CH2 负输出的临时配置上
为 8 PASS、009 SKIP。逻辑分析仪数据见 `docs/bl616cl-pwm.md` 的“多通道与互补输出实测”。

以下是单通道（CH3+/GPIO22）时期的数据。

测试固件 `nsh` 在 `/dev/ttyUSB2`、2,000,000 baud 启动并进入 NSH。
`help` 包含 `mcu_pwm_test`；`ls /dev` 有 `pwm0`，无 `pwm1` 和 `gpio22`。

执行命令：

```text
mcu_pwm_test -c all -w 0 -v
```

逐项关键结果：

```text
PWM-001  setup=1 shutdown=1 active-after-first-close=yes  PASS
PWM-002  invalid request errno=22/34; hardware preserved  PASS
PWM-003  PBCLK=160000000Hz; actual=100/1000/10000Hz      PASS
PWM-004  duty=0/25/50/75/65535; high max=period-1       PASS
PWM-005  CPOL/DCPOL four register mappings              PASS
PWM-006  1k=3/53333 100=25/64000 10k=1/16000            PASS
PWM-007  timeout errno=110; active-close/reopen restart PASS
PWM-008  pwm0=yes pwm1=no gpio22=no                     PASS
PWM Summary: executed=8 passed=8 failed=0 -> PASS
```

PWM-006 一行是 2026-10-13 改为最小 divider 后在 `nsh-peripherals` 上复跑的结果（原为
1k=4/40000），其余行不变。

同一固件继续执行 GPIO edge、TIMER-001/002/005、oneshot、WDT-002/003 和 RTC 回归，
均通过；TIMER-001 最大误差 253 us（0.253%），TIMER-002 周期比 2.000，WDT-002
在 3026 ms 内 keepalive 6 次，RTC 时间从 `00:00:21` 递增到 `00:00:23`，最终
系统存活检查返回 0。

active close 修复前，未修复 NuttX 得到 `executed=8 passed=7 failed=1`，唯一失败为
`active close/reopen did not restart lower`。NuttX upper 在最后 close 后清除
`started` 后，同一 USB2、固件配置和命令得到 8/8 PASS；同次 TIMER-001 最大误差
275 us（0.275%），其余上述回归和系统存活检查继续通过。

## 波形数据

2026-10-13 用逻辑分析仪在 GPIO22 测量（2 MHz 采样、阈值 1.6 V，运行
`mcu_pwm_test -c 003`），环境和逐周期数据见 `docs/bl616cl-pwm.md` 的“实物波形数据”：

```text
GPIO22  100Hz / 75%      measured frequency=100.00Hz  duty=75.00%
GPIO22    1kHz / 25%     measured frequency=999.97Hz  duty=25.00%
GPIO22    1kHz / 50%     measured frequency=999.97Hz  duty=50.00%
GPIO22    1kHz / 75%     measured frequency=999.97Hz  duty=75.00%
GPIO22   10kHz / 25%     measured frequency=9999.68Hz  duty=25.00%（闭式求解后复测）
CPOL/DCPOL 四组合       STOP level=0/1/0/1 与 dcpol 一致  close level=1（HIGH/HIGH）
                        running polarity: START 后首个相位与 cpol 一致，LOW/LOW 无法区分
同 divider duty update   longest abnormal pulse=一个周期（476.5us 或 1118us）
跨 divider update        observed pause=约 114us（100Hz→1kHz）
```

判定阈值为频率误差不超过 1%、占空比误差不超过 2 个百分点；同 divider 更新不得出现
超过一个 PWM 周期的异常脉冲。已测项目满足阈值。10kHz 点和 CPOL/DCPOL 来自闭式求解后的
复测（`-c 003 -w 2`、`-c 005 -w 1`），停止态切换的短脉冲等细节见 `docs/bl616cl-pwm.md`
“闭式求解后的复测”。

上述采集所用镜像上，`-w N` 时每点实际持续约 N+1 s，当时 lower 每次 SET 都遍历全部
divider（之后的镜像复测约 0.35 s，早先多出的耗时原因未查明）。lower 改为直接计算
最小 divider 后，SET 不再有明显耗时。用仪器分段时按实际边沿切分，不要按 `-w` 推算。
