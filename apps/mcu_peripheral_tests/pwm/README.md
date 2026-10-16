# MCU PWM 外设测试

本目录验证 BL616CL PWM controller 与 OpenVela PWM upper-half 的首版能力交集。
`pwm_test_main.c` 编译为独立测试 archive，注册 NSH 命令 `mcu_pwm_test`；它不进入
`libarch.a`。chip lower、板级注册和 PWM LHAL 对象由各自 Kconfig 控制并进入对应内核
archive。测试 app 只通过 `/dev/pwm0` 的标准 PWM ioctl 访问驱动，通道和输出引脚取自
板级 Kconfig（`CONFIG_AI_M64P_KIT_PWM_CHn`、`_POS_PIN`、`_NEG_PIN`）。PWM-001～008
针对主通道（编号最小的已配通道），PWM-009 验证多通道和互补输出；用例编号 007 不使用。
`nsh-peripherals` 的分配为 CH0 正 8/负 9、CH1 正 10/负 11、CH2 只用负输出 21、CH3 正
6/负 7，主通道为 CH0。

覆盖整数频率、连续输出、b16 duty、`cpol`/`dcpol`、运行中更新、多 fd 生命周期、
多通道（`PWM_MULTICHAN`）和互补输出。pulse count、deadtime、brake、center-aligned、
ADC trigger、fixed-point frequency 和 PWM DMA 不属于本轮声明。

程序判据是 ioctl 返回值，以及 `PWMIOC_GETCHARACTERISTICS` 读回与请求一致。用 `-g`
指定一个接到主通道输出的 GPIO 输入后，再检查输出是否在翻转和 STOP 后的停止电平；
不给 `-g` 时这些电平检查报 SKIP，不计为通过。波形频率、占空比、互补关系和更新毛刺由
逻辑分析仪测量，程序不判定。

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

产品配置应关闭测试 app。`-g` 需要 `CONFIG_DEV_GPIO`。lower 与 `PWM_PULSECOUNT`、
`PWM_FREQUENCY_FIXED`、`PWM_DEADTIME` 不兼容，Kconfig 不允许形成这些 ABI 组合。
启用板级 PWM 后，已配的 PWM 引脚从 GPIO 测试节点移交给 PWM；I2C、SPI、UART1 任一信号
配置到这些引脚都必须在编译期失败。

## 运行前检查

1. 固件启动后确认出现 `NuttShell (NSH)` 和 `nsh>`。
2. 执行 `help`，必须能找到 `mcu_pwm_test`。
3. 执行 `ls /dev`，必须有 `pwm0`，不能有 `pwm1` 和已配 PWM 引脚的 `gpioN`。
4. `mcu_pwm_test -c all` 会真实驱动所有已配 PWM 引脚。
5. 电平检查需要一根跳线：选一个没有接其它信号的 `/dev/gpioN`（`nsh-peripherals` 可用
   `/dev/gpio22`），把该 GPIO 接到主通道输出（`nsh-peripherals` 为 CH0 正输出
   GPIO8），运行时加 `-g /dev/gpioN`。程序先把该节点设为输入。不要用已经与 GPIO23
   短接的 GPIO20，否则 GPIO23 的输出会与 PWM 输出对冲。
6. 波形验收时将逻辑分析仪或示波器探头接已配 PWM 引脚，共地，量程适配 3.3V。
7. 串口日志、板载 LED 和程序 PASS 不能替代频率、占空比或毛刺证据。

## 命令

```text
mcu_pwm_test -h
mcu_pwm_test -c all -g /dev/gpio22
mcu_pwm_test -c 001 -g /dev/gpio22
mcu_pwm_test -c 002
mcu_pwm_test -c 003 -w 3
mcu_pwm_test -c 004 -w 3
mcu_pwm_test -c 005 -w 3 -g /dev/gpio22
mcu_pwm_test -c 006 -w 3
mcu_pwm_test -c 008
mcu_pwm_test -c 009 -w 3 -g /dev/gpio22
```

启动时先打印每个已配通道的正/负引脚（-1 表示不用）、主通道编号和回环 GPIO 读的是哪个
引脚。`-d` 可覆盖 PWM 节点，默认 `/dev/pwm0`；`-g` 指定回环 GPIO 节点；`-w` 是每个
可观测波形档位的保持秒数，默认 1 秒。指定单个 case 时任一步失败立即返回非零；`all`
会继续执行并在末尾汇总，汇总行的 `level_checks_skipped` 是因没有 `-g` 而跳过的电平
检查数。

## PWM-001 open/close 与多 fd 生命周期

背景：PWM upper-half 只在第一次 open 调用 lower `setup`，只在最后一次 close 调用
`shutdown` 并清除 `started`。中间 fd 关闭不能停止仍由其他 fd 使用的输出；运行中关闭
最后一个 fd 后，重新 open 并 START 必须再次启动 lower。

命令：`mcu_pwm_test -c 001 -g /dev/gpio22`

流程：

1. 打开 `/dev/pwm0` 两次（fd1、fd2），都必须成功。
2. 用 fd1 设置 1kHz、50%、CPOL_HIGH/DCPOL_LOW 并 START；ioctl 返回 0，GET 与请求一致。
3. 关闭 fd1；fd2 GET 必须仍与请求一致，回环 GPIO 必须读到高、低两种电平（输出仍在
   运行）。
4. 用 fd2 STOP 两次，都必须返回 0；关闭 fd2。
5. 重新 open，直接 START（upper 保留了 characteristics），在输出运行时关闭这个唯一的 fd。
6. 再次 open 并 START，返回 0，GET 与请求一致，回环 GPIO 必须读到两种电平；STOP、close。

完成判据：所有 ioctl 返回 0、GET 一致；有 `-g` 时两次运行检查通过。setup/shutdown 的
调用次数和 lower 的 pin/clock 释放没有公开接口，不在判据内。

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
5. 设置有效 1kHz/50% 并 START，返回 0、GET 一致。
6. 运行态 SET 零频率；SET 必须返回 `EINVAL`，GET 必须显示 upper 已覆盖为零频率。
7. 运行态重新 SET 有效配置恢复，再 STOP、close。

`duty` 的 ABI 类型是 `ub16_t`，所有可表达值 0..65535 都是有效边界，没有可构造的
“大于最大值”输入；0 和 65535 由 PWM-004 覆盖，不能伪造不存在的非法 duty case。

完成判据：所有 errno 与 upper 状态覆盖语义一致。运行态非法请求没有改动输出，需由
逻辑分析仪确认，程序不判定。

## PWM-003 频率与占空比档位

背景：lower 在 START/SET 内求解 divider、period、threshold 并回读寄存器，不一致时
ioctl 返回 `EIO`，超出范围返回 `ERANGE`。实际频率和占空比由仪器测量。

命令：`mcu_pwm_test -c 003 -w 3`

流程：

1. 依次设置 100Hz/75%、1kHz/25%、1kHz/50%、1kHz/75%、10kHz/25%。
2. 首点 SET 后 START；后续 SET 在运行中触发 live update。每点 ioctl 返回 0、GET 一致。
3. 每点保持 `-w` 秒供仪器记录，全部结束后 STOP、close。

完成判据：程序判据为每点 ioctl 返回 0、GET 一致；仪器判据为频率误差不超过 1%、占空比
误差不超过 2 个百分点。

## PWM-004 duty 全边界与 live update

背景：验证 0、常用档位和 b16 最大值的请求路径。lower 把 65535/65536 映射为
`period-1` 的 threshold，不是恒定 100% 高电平。

命令：`mcu_pwm_test -c 004 -w 3`

流程：

1. 固定 1kHz，按 0、25%、50%、75%、65535/65536、25% 依次 SET。
2. 首点 START；其余点在运行态 SET，每点返回 0、GET 一致，保持 `-w` 秒。
3. 最后 STOP、close。

完成判据：每点 ioctl 返回 0、GET 一致；各档位占空比由仪器确认。

## PWM-005 CPOL/DCPOL 与停止电平

背景：`cpol` 决定正向通道 active polarity，`dcpol` 需结合 `cpol` 转换成硬件
active/inactive stop-state，不能把枚举值直接写入 bit。

命令：`mcu_pwm_test -c 005 -w 3 -g /dev/gpio22`

流程：

1. 固定 1kHz/50%，遍历 LOW/LOW、LOW/HIGH、HIGH/LOW、HIGH/HIGH 四种组合。
2. 每种组合 SET/START 返回 0、GET 一致；保持 `-w` 秒，仪器记录运行波形初始极性。
3. STOP 返回 0 后读回环 GPIO：接正输出时必须等于该组合的 dcpol，接负输出时必须相反
   （STOP 后引脚是 GPIO 输出，保持停止电平）；随后进入下一组。

完成判据：ioctl 和 GET 全部通过；有 `-g` 时四次停止电平正确。cpol/dcpol 写入寄存器
的映射没有公开回读，由仪器上的运行极性和停止电平确认。

## PWM-006 同 divider 与跨 divider 更新

背景：同 divider 的 duty 更新应走一次 update gate；跨 divider 允许受控 stop/reinit，
本轮不宣称跨 divider 无毛刺。

命令：`mcu_pwm_test -c 006 -w 3`

流程：

1. START 1kHz/25%；重复 START 必须返回 0。
2. 运行中更新为 1kHz/75%（同 divider），返回 0、GET 一致。
3. 更新为 100Hz/50%，再更新为 10kHz/50%（跨 divider），返回 0、GET 一致。
4. 仪器记录同 divider 更新，异常短脉冲不得超过一个 PWM 周期；跨 divider 记录允许
   存在的停顿，不作无毛刺结论。
5. STOP 后重复 STOP 必须返回 0；再 START/STOP 一次，证明生命周期可恢复。

完成判据：所有 ioctl 返回 0、GET 一致。lower 实际走了哪条更新路径没有公开回读，由
仪器上的停顿区分。

## PWM-008 设备节点与引脚归属

背景：每个 PWM 引脚只能由 PWM 或 GPIO/I2C/SPI/UART1 一方拥有；节点按配置出现。

命令：`mcu_pwm_test -c 008`

流程：

1. 打开 `/dev/pwm0` 必须成功；打开 `/dev/pwm1` 必须失败并返回 `ENOENT`。
2. 打开每个已配 PWM 引脚的 `/dev/gpioN` 必须失败并返回 `ENOENT`，证明 GPIO 测试节点
   已移除。
3. 设置并启动 1kHz/50%，返回 0、GET 一致；STOP、close。单端/差分引脚布局没有公开
   回读，由仪器在对应引脚上看到输出确认。
4. 配置期分别让 I2C0/I2C1 SCL/SDA、SPI0 CLK/MISO/MOSI/CS0/CS1、UART1 TX/RX 使用已配
   PWM 引脚；每组必须在编译期失败。PWM 引脚不在所选通道槽位（单端正 `pin%4==ch`；
   差分正 `pin%8==2*ch`、负 `pin%8==2*ch+1`）、一个通道都没选，或不开 `PWM_MULTICHAN`
   时选了多个通道，也必须编译失败。合法默认 pin 配置必须 clean build 通过。
5. off/product/test 三组制品分别检查 source、archive、map、ELF symbol 和 `help`：off
   无 lower/LHAL/board/app；product 有 `/dev/pwm0` 但无 app；test 多出测试 app。
6. board 注册失败路径只做代码审查：失败前 lower 必须是静态、未 acquire 状态。不要在
   运行时重复 `pwm_register()`，因为通用 upper 的已知 allocation leak 会污染测试环境。

完成判据：运行时节点和归属断言通过，配置冲突矩阵与三态裁剪证据完整。

## PWM-009 多通道与互补输出

背景：四个通道共用 frequency，各自 duty；负输出是正输出的反相。START 中没提到的通道
必须保持原状态，非法通道请求不得改动硬件。只在 `PWM_MULTICHAN` 开启且已配不少于两个
通道时运行，否则 SKIP。

命令：`mcu_pwm_test -c 009 -w 3 -g /dev/gpio22`

流程：

1. 全部已配通道按通道顺序取 20%、40%、60%、80%（便于在仪器上区分），cpol HIGH、
   dcpol HIGH，1kHz START，返回 0、GET 一致；STOP 后读回环 GPIO：主通道正输出为 1、
   负输出为 0。
2. dcpol 改为 LOW，再次全部通道 1kHz START，返回 0、GET 一致，保持 `-w` 秒。
3. 只更新最后一个通道到 90%（请求里第一项 `channel=0` 跳过），返回 0、GET 一致，
   保持 `-w` 秒。
4. 只提第一个通道，改为 10kHz/50%，返回 0、GET 一致，保持 `-w` 秒。
5. 未知通道号、未配置通道（全部已配时 SKIP）、重复通道、空请求都必须返回 `EINVAL`。
6. STOP 后读回环 GPIO：主通道正输出为 0、负输出为 1；close。

完成判据：ioctl、GET 和 errno 断言通过，有 `-g` 时两次停止电平正确。其余通道在单通道
更新时保持不变、各通道频率一致、duty 正确、负输出与正输出逐样本反相、非法请求不改动
输出，以及非主通道的 STOP 电平，由仪器确认。

## 当前实测数据

2026-10-16 当前程序在 Ai-M64P-32S-Kit `nsh-peripherals` 上执行 `mcu_pwm_test -c all -w 0`（未接 `-g` 回环）：8 个 case 全部 PASS，8 项电平检查 SKIP（`level_checks_skipped=8`）；带 `-g` 的电平检查尚未实板运行。以下是 2026-10-13 的实板记录。

多通道版本在 `nsh-peripherals`（4 通道，见开头分配）上执行 `-c all`：PWM-001～006、
008、009 均 PASS。不开 `PWM_MULTICHAN`、只配 CH2 负输出的临时配置上 009 SKIP，其余
PASS。逻辑分析仪数据见 `docs/bl616cl-pwm.md` 的“多通道与互补输出实测”。

单通道（CH3+/GPIO22）时期，测试固件 `nsh` 在 `/dev/ttyUSB2`、2,000,000 baud 启动并
进入 NSH。`help` 包含 `mcu_pwm_test`；`ls /dev` 有 `pwm0`，无 `pwm1` 和 `gpio22`。
同一固件继续执行 GPIO edge、TIMER-001/002/005、oneshot、WDT-002/003 和 RTC 回归，
均通过；TIMER-001 最大误差 253 us（0.253%），TIMER-002 周期比 2.000，WDT-002
在 3026 ms 内 keepalive 6 次，RTC 时间从 `00:00:21` 递增到 `00:00:23`，最终
系统存活检查返回 0。

NuttX upper 曾在最后一次 close 后不清 `started`，重新 open 后 START 不再调用 lower；
修复后 active close/reopen 能重新启动输出。PWM-001 的运行检查（`-g`）覆盖这一场景。

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
