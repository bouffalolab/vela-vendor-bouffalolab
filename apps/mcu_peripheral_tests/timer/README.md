# MCU Timer/PWM 外设测试

`timer_case_main.c` 编译为独立的 `libapps_mcu_timer_test.a`，注册 NSH 命令
`mcu_timer_test`。TIMER-001/002/005~008/010 操作 `-d` 指定的普通 timer；
TIMER-009 同时操作 `/dev/timer0` 和 `/dev/timer1`；TIMER-003/004 操作
`/dev/pwm0`。测试 archive 不属于 `libarch.a`，chip lower 的
`bl616cl_tim.c.o` 才进入 `libarch.a`。

本轮为 TIMER1 增加与 TIMER0 共用的一组普通 timer ops，继续使用 1 MHz
微秒口径。TIMER1 与 `/dev/oneshot` 使用相同 counter 和 IRQ，因此配置期互斥；
TIMER0 可与任一 owner 并存。`BL616CL_TIMER_TEST` 只为 TIMER-010 暴露 raw lower
hook，生产配置必须关闭。

## 运行前检查

1. 固件启动后确认出现 `NuttShell (NSH)` 和 `nsh>`。
2. 执行 `ls /dev`；单实例 case 必须存在 `-d` 指定节点，TIMER-009 必须同时
   存在 `timer0` 和 `timer1`，PWM case 必须存在 `pwm0`。
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

## TIMER-006 tick ioctl 换算与边界

背景：验证 OpenVela tick ABI 与 BL616CL 微秒 lower 的量化关系，并覆盖零值和
乘法溢出，避免把 tick 数直接当作微秒值。

命令：`mcu_timer_test -d /dev/timer1 -c 006`

流程：

1. 停止所选 timer，以微秒接口设置 `USEC_PER_TICK+1=1001us`。
2. `TICK_GETSTATUS` 必须向上量化为 2 ticks，不能截断为 1 tick。
3. `TICK_SETTIMEOUT=3` 后，同时读取 tick 和微秒状态；结果必须分别为 3 ticks
   和 3000us。
4. 分别读取 tick/微秒最大 timeout；tick 最大值必须等于微秒最大值整除
   `USEC_PER_TICK`。
5. `TICK_SETTIMEOUT=0` 必须返回 `EINVAL`；`UINT32_MAX` 必须在乘法前以
   `ERANGE` 拒绝。
6. 恢复调用者 timeout 并关闭 fd。

实测输出：

```text
[TIMER-006] Tick ioctl conversion and boundaries
  rejected: zero tick timeout errno=22
  rejected: tick timeout multiplication overflow errno=34
  tick=1000us rounded=1001us->2 ticks set=3 ticks max=4294967
  [TIMER-006] PASS tick conversion and boundaries
Timer Summary: executed=1 passed=1 failed=0 -> PASS
nsh>
```

## TIMER-007 poll、单次通知与单 waiter

背景：验证 timer upper-half 的 poll 唤醒、`periodic=false` 单次通知和只允许一个
poll waiter 的资源边界。

命令：`mcu_timer_test -d /dev/timer1 -c 007`

流程：

1. 设置 50ms timeout，注册非周期 `SIGEV_SIGNAL`，阻塞该信号并 START。
2. 对 timer fd 执行 1s `poll(POLLIN)`；必须正好返回一个可读事件。
3. 用 `sigtimedwait` 取得同一次通知，再读状态；ACTIVE 必须清除，证明单次通知
   不会自动重装。
4. 同一次 `poll()` 传入两个指向同一 fd 的 waiter；第一个占用 upper-half poll
   slot，第二个必须返回 `POLLERR`。
5. STOP 清理即使返回 inactive 错误也不改变判定，然后关闭 fd。

实测输出：

```text
[TIMER-007] Poll and one-shot notification
  poll revents=0x1 second-waiter revents=0x8
  [TIMER-007] PASS poll, one-shot and single-waiter boundary
Timer Summary: executed=1 passed=1 failed=0 -> PASS
nsh>
```

## TIMER-008 多 fd 共享状态与 close 生命周期

背景：OpenVela timer 的 timeout、callback 和 lower 是设备级共享状态，关闭一个
fd 不应停止仍由另一个 fd 使用的硬件。

命令：`mcu_timer_test -d /dev/timer1 -c 008`

流程：

1. 连续打开同一 timer 两次，通过 fd1 设置 60ms 周期通知。
2. 通过 fd2 读回 timeout，必须看到同一个 60000us 配置。
3. 用 fd1 START 后立即关闭 fd1；close 只减少引用，不调用 STOP。
4. 等待一次信号，通过 fd2 读取状态；必须仍为 ACTIVE。
5. 由 fd2 显式 STOP，再关闭最后一个 fd，防止状态泄漏给后续 case。

实测输出：

```text
[TIMER-008] Multi-fd shared state and close lifetime
  shared timeout=60000us active-after-close=yes
  [TIMER-008] PASS shared state and close lifetime
Timer Summary: executed=1 passed=1 failed=0 -> PASS
nsh>
```

## TIMER-009 TIMER0/TIMER1 双实例隔离

背景：两个实例共用 TIMER 寄存器基址，LHAL 对共享位域使用 read-modify-write；
本 case 验证芯片临界区保护、独立 IRQ/callback 和停止隔离。

命令：`mcu_timer_test -c 009`

流程：

1. 同时打开 timer0/timer1，分别配置 40ms/70ms 和不同信号。
2. 同时 START，必须分别收到两个信号，不能由一个 IRQ 冒充另一个实例。
3. 运行中交错更新为 30ms/50ms，再次分别等待通知并读回各自 timeout。
4. STOP timer0；timer1 必须继续到期且保持 ACTIVE。
5. 无论中途失败与否，退出路径都分别 STOP 并关闭两个 fd。

实测输出：

```text
[TIMER-009] TIMER0/TIMER1 dual-instance isolation
  timer0=30000us timer1=50000us timer1-active-after-timer0-stop=yes
  [TIMER-009] PASS dual IRQ, state and stop isolation
Timer Summary: executed=1 passed=1 failed=0 -> PASS
nsh>
```

## TIMER-010 raw callback 合同

背景：应用 signal 只能间接观察 callback；该 test-only case 直接验证 lower callback
返回 true/false 和写入 next interval 的合同。

配置：`CONFIG_BL_MCU_PERIPHERAL_TESTS_TIMER_RAW=y`，由它选择
`CONFIG_BL616CL_TIMER_TEST=y`。生产固件关闭这两个选项。

命令：`mcu_timer_test -d /dev/timer1 -c 010`

流程：

1. 通过 test hook 取得所选 lower，先 STOP 并设置 50ms timeout。
2. 安装 raw callback：第一次回调写入 30ms 并返回 true，要求继续运行；第二次
   返回 false，要求停止。
3. 最多等待 1s；回调计数必须恰为 2。
4. GETSTATUS 必须为 inactive，timeout 必须已更新为 30000us。
5. 无条件 STOP、清空 callback/arg、恢复调用者 timeout，避免 test hook 状态泄漏。

实测输出：

```text
[TIMER-010] Raw lower-half callback contract
  callbacks=2 next=30000us active=no
  [TIMER-010] PASS true reload, next interval and false stop
Timer Summary: executed=1 passed=1 failed=0 -> PASS
nsh>
```

## 构建与裁剪矩阵

```text
nsh-timer-off      1223/1223  无普通 timer lower
nsh-timer0         1223/1223  仅 TIMER0
nsh-timer1         1223/1223  仅 TIMER1，含 test-only raw hook
nsh-timer-dual     1223/1223  TIMER0 + TIMER1
nsh-timer-oneshot  1224/1224  TIMER0 + oneshot，无 TIMER1 普通实例
```

`nsh-timer-off` 保留 oneshot 和测试命令，用于证明普通 `bl616cl_tim.c.o` 可裁剪；
它不是“所有 timer API 和测试 app 全关闭”。TIMER0/TIMER1 实例由各自 Kconfig
控制；同一个 `bl616cl_tim.c.o` 包含公共 ops，最终实例和 bringup 节点按配置裁剪。

## all 模式

`mcu_timer_test -c all` 会执行 001、002、005~010、003、004。009 只适用于 dual
配置，010 只适用于 raw test 配置；在其他配置应按编号单独选择适用 case，不能把
预期缺少资源写成硬件失败。003/004 仍需外部仪器判定，所以软件 summary 全 PASS
不能独立证明 PWM 精度满足阈值。USB2 日志能证明 callback、状态和软件计时合同，
不能证明绝对 IRQ 边沿精度或硬件抖动。
