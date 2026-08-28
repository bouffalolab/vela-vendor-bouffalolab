# MCU Timer/PWM 外设测试

`timer_case_main.c` 编译为独立的 `libapps_mcu_timer_test.a`，注册 NSH 命令
`mcu_timer_test`。TIMER-001/002/005 操作 `/dev/timer0`；TIMER-003/004 操作 `/dev/pwm0`。

## 运行前检查

1. 固件启动后确认出现 `NuttShell (NSH)` 和 `nsh>`。
2. 执行 `ls /dev`，对应 case 必须存在 `timer0` 或 `pwm0`。
3. 001/002 加 `-g /dev/gpio12` 时，将示波器接到 GPIO12；方波周期为 timer 周期的 2 倍。
4. 003/004 将仪器接到 PWM 输出脚；软件 DONE 不能替代波形精度证据。

## TIMER-001 基本计数与周期精度

背景：验证周期通知、自动重装和准确性，避免只证明计数器能启动。

命令：`mcu_timer_test -c 001 -t 100000 -n 5 -e 0.5 -v`

流程：

1. 打开 timer0，以 SETTIMEOUT 设置 100000us，注册周期 SIGEV_SIGNAL 并阻塞该信号。
2. START 后 GETSTATUS；flags 应含 ACTIVE，timeout 应为 100000us。
3. 丢弃第一次到期信号作为 warm-up，排除启动延迟。
4. 连续等待 5 次信号，计算相邻 `CLOCK_MONOTONIC` 间隔和绝对误差。
5. 无 `-g` 时任一误差超过 500us 立即失败；有 `-g` 时软件时钟仅作参考。
6. STOP 并关闭 fd；必须打印 summary 并返回 `nsh>`。

完成判据：无丢失信号，最大误差不超过 0.5%，出现 `TIMER-001 PASS accuracy within tolerance`。

固件运行关键证据：

```text
[TIMER-001] overflow period accuracy timeout=100000us rounds=5 tol=0.50%
  status: flags=0x3 timeout=100000us timeleft=99938us
  RESULT max_err=188.0us (0.188%) tol=500.0us (0.50%)
  [TIMER-001] PASS accuracy within tolerance
Timer Summary: executed=1 passed=1 failed=0 -> PASS
nsh>
```

## TIMER-002 预分频

背景：验证 `BL616CL_TCIOC_SETCLOCKDIV` 实际改变频率，并在测试后恢复默认微秒口径。

命令：`mcu_timer_test -c 002 -t 500000 -a 39 -b 79 -v`

流程：

1. 打开 timer0 并 STOP，设置 div=39、固定 compare=500000，注册通知并 START。
2. 丢弃 warm-up，以连续两个到期信号测得 `period_a`，然后 STOP。
3. 设置 div=79，完全重复相同步骤得到 `period_b`；唯一变量必须是 divider。
4. 计算比值；理论值 `(79+1)/(39+1)=2.000`，允许 ±5%。
5. STOP 并恢复 div=39，否则后续 timer0 不再保持 1us 口径。
6. 可选 `-g` 时分别观察两个稳定方波区间。

完成判据：两组信号均收到，比值在 1.900~2.100，出现 `TIMER-002 PASS prescaler takes effect`。

固件运行关键证据：

```text
[TIMER-002] clock prescaler effect (div 39 vs 79)
  div=39 period=0.1999s
  div=79 period=0.4000s
  ratio period_b/period_a=2.001 (expected 2.000, tol +/-5%)
  [TIMER-002] PASS prescaler takes effect
Timer Summary: executed=1 passed=1 -> PASS
nsh>
```

## TIMER-003 PWM 频率与占空比

背景：验证 PWM 配置路径及 b16 duty 转换；串口只能证明 ioctl 成功。

命令：`mcu_timer_test -c 003 -f 2000 -D 40 -w 5`

流程：

1. 打开 pwm0；指定 `-f` 时测一个点，否则测 1kHz/50%、10kHz/25%、100Hz/75%。
2. 百分比转换为 b16 duty，调用 SETCHARACTERISTICS 和 START。
3. 保持 5 秒，记录频率、周期、高电平时间和占空比；每点结束后 STOP。
4. 频率误差按 ±1%、占空比误差按 ±2 个百分点判定。

完成判据：全部 ioctl 成功，仪器数据满足阈值，输出 `TIMER-003 DONE`。

仪器关键数据：历史 BL618G1 外设板测得 2kHz/40%=2.00kHz、500.0us、40.0%；
5kHz/50%=5.00kHz、200.0us、50.0%，PASS。当前 Ai-M64L-32S-Kit 固件尚未重跑该仪器流程，
因此这些数据只证明同一测试程序的历史 PWM 测量，不作为当前板复验结论。

## TIMER-004 PWM 占空比渐变

背景：验证频率不变时重复更新 duty 的 fast-path，无频率漂移或明显毛刺。

命令：`mcu_timer_test -c 004 -f 1000 -s 25 -i 1000 -n 5 -v`

流程：

1. 打开 pwm0，固定 1000Hz、初始 duty=0，SETCHARACTERISTICS 后 START。
2. 每周期按 0→25→50→75→100%，再按 100→75→50→25→0% 更新。
3. 每档保持 1000ms，记录频率、占空比和切换毛刺，重复 5 个周期。
4. 最后 STOP；100% 档应为稳定高电平而不是可测频率的方波。

完成判据：频率保持 1.00kHz，各档占空比符合设定，输出 `TIMER-004 DONE`。

仪器关键数据：历史 BL618G1 外设板测得 25%=25.20%、50%=50.00%、75%=75.20%，频率均为 1.00kHz；
100% 为稳定 3.3V，PASS。当前 Ai-M64L-32S-Kit 尚未重跑 PWM 仪器流程，不作为当前板复验结论。

## TIMER-005 生命周期与异常请求

背景：覆盖非法参数、重复 START/STOP、运行中改配置和错误后的恢复能力。

命令：`mcu_timer_test -c 005`

流程：

1. 打开 timer0 并保存初始 GETSTATUS。
2. SETTIMEOUT=1 必须返回 EINVAL；再次 GETSTATUS，timeout 必须未改变。
3. SETCLOCKDIV=256 必须返回 EINVAL。
4. 配置 50000us 并 START；重复 START 必须返回 EBUSY。
5. 运行中 SETCLOCKDIV=79 必须返回 EBUSY。
6. 运行中 SETTIMEOUT=25000 必须成功；等待到期信号，证明 live update 后中断仍工作。
7. GETSTATUS 必须为 ACTIVE 且 timeout=25000。
8. 首次 STOP 成功；第二次 STOP 返回 ENODEV；最终 GETSTATUS 不含 ACTIVE。
9. 恢复调用者 timeout 和 div=39，关闭 fd。

完成判据：所有 errno、状态保持、live 信号和清理步骤全部成立。

固件运行关键证据：

```text
[TIMER-005] Lifecycle and rejected requests
  rejected: timeout below hardware minimum errno=22
  rejected: clock divider above 8-bit range errno=22
  rejected: start while already active errno=16
  rejected: change divider while active errno=16
  rejected: stop while already inactive errno=19
  [TIMER-005] PASS rejected requests preserved state; live update fired; lifecycle recovered
Timer Summary: executed=1 passed=1 failed=0 -> PASS
nsh>
```

## all 模式

`mcu_timer_test -c all` 依次执行 001、002、005、003、004。003/004 仍需外部仪器判定，
所以软件 summary 全 PASS 不能独立证明 PWM 精度满足阈值。
