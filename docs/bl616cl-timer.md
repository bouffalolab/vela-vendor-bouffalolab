# BL616CL 普通 Timer 与 TIMER1 验证

## 背景

BL616CL 现有普通 timer lower-half 只注册了 `/dev/timer0`，而 TIMER1 被
`/dev/oneshot` 独占。芯片 device table 实际为 TIMER0/TIMER1 提供独立 idx、
compare0、counter 和 IRQ；两个实例却共享 TCCR/TCDR/TCMR/TCER 的寄存器位域。
本次适配的目标是复用 OpenVela `timer_lowerhalf` 合同，按配置选择 TIMER0、
TIMER1 或双实例，同时保持 oneshot owner 不冲突。

USB-UART 只能证明固件启动、节点、callback、状态和软件计时。没有示波器/逻辑
分析仪时，本文不把串口输出解释为绝对 IRQ 边沿精度、抖动或电气波形证据。

## 方案与配置控制

### 能力交集

| 能力 | BL616CL 依据 | 本次处理 |
|---|---|---|
| start/stop/status | `bflb_timer_start/stop/get_countervalue` | TIMER0/TIMER1 均接入 |
| 微秒 timeout | XTAL、div39、compare0 | 最小 2us，动态重装 |
| periodic/one-shot callback | compare0 IRQ、OpenVela notification | 通过 `TCIOC_NOTIFICATION` 验证 |
| tick ioctl | OpenVela timer upper-half | lower 提供 tick ops，向上取整 |
| poll | OpenVela upper-half | 单 waiter 唤醒，第二 waiter 报 `POLLERR` |
| 多 fd | timer upper 共享 lower | 验证 timeout/active 共享、close 不停止 |
| raw callback | lower callback 槽位 | 仅 test-only Kconfig 暴露 |
| input capture/DMA | capture 仅 TIMER0，DMA 还缺 cache/通道闭环 | 延后，不冒充 TIMER1 能力 |

### Kconfig 与 owner

- `CONFIG_BL616CL_TIMER` 控制普通 timer lower 总体。
- `CONFIG_BL616CL_TIMER0`、`CONFIG_BL616CL_TIMER1` 分别控制实例和 bringup
  节点；TIMER1 依赖 `!BL616CL_ONESHOT`。
- `CONFIG_BL616CL_TIMER_TEST` 只导出 `bl616cl_timer_test_lower()`；
  `CONFIG_BL_MCU_PERIPHERAL_TESTS_TIMER_RAW` 选择它并启用 TIMER-010，生产配置
  保持关闭。
- `BL616CL_TCIOC_SETCLOCKDIV` 已移到 `_TCIOC(0x0040)`，避开 OpenVela timer
  与 oneshot 的保留号段 `0x0001..0x003f`。
- TIMER0/TIMER1 的 stop、IRQ mask、configure、attach、start 和 live update
  在芯片临界区内执行，避免共享寄存器 read-modify-write 互相覆盖。
- `/dev/oneshot` 仍由 TIMER1 owner；TIMER0 可与 oneshot 同时打开。WDT
  `BY_TIMER` automonitor 不在本次范围内，避免 lower owner 和毫秒/微秒单位缺口。

## 构建命令与裁剪门禁

在 SDK 根目录逐个执行 clean/build：

```bash
python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh-timer
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh-timer -j14

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

本轮 clean build 结果：

| 配置 | 结果 | 归档/节点检查 |
|---|---:|---|
| `nsh` | 1223/1223 | `libarch.a` 无 `bl616cl_tim.c.o`，只有 oneshot；无 timer0/1 |
| `nsh` | 1223/1223 | `bl616cl_tim.c.o` 仅有 TIMER0，节点为 timer0 |
| `nsh` | 1223/1223 | 仅有 TIMER1，含 test hook，节点为 timer1 |
| `nsh-timer` | 1223/1223 | TIMER0/TIMER1 均存在，含 test hook |
| `nsh` | 1224/1224 | TIMER0 与 oneshot 存在，无 TIMER1 普通实例 |

测试 main 的归档路径为 `apps/vendor/bouffalolab/apps/mcu_peripheral_tests/timer/`
`libapps_mcu_timer_test.a`，不属于 `libarch.a`；chip lower 才进入
`arch/libarch.a`。`nsh` 仍编译测试 main，是为了保持命令接口在
oneshot 基线可见；它不代表普通 timer lower 被启用。

## 串口流程

以下流程均从固件运行态开始，不包含烧录。串口固定为 `/dev/ttyUSB2`、
2000000 baud；打开后立即恢复运行态控制线，并在同一 fd 内完成命令和收尾。

1. 使用 `bl_module_reset.py --port /dev/ttyUSB2 --baudrate 2000000` 复位，
   必须匹配 `NuttShell (NSH)` 与 `nsh>`。
2. 执行 `ls /dev`，按配置确认 timer0/timer1/oneshot 节点。
3. 对单实例配置，001/002/005~008/010 使用 `-d` 指定实际节点；009 只在 dual
   配置执行；oneshot 配置执行 `oneshot -d 100000 /dev/oneshot`。
4. 每条命令等待自身的 PASS/完成标志和 `nsh>`，不能用上一条命令的旧 prompt
   作为成功判据。
5. 非复位 case 在退出路径显式 STOP，并清空 callback；最终执行 `echo alive`
   验证 NSH 仍存活。

## 用例流程

测试入口只有一个 `timer_case_main.c`，用 `-c` 选择以下 case。

### TIMER-001 周期计时

命令：`mcu_timer_test -d /dev/timer1 -c 001 -n 2`

流程：设置 100000us、注册周期 signal、START，读 ACTIVE 状态；丢弃 warm-up，
连续收集两个间隔并与 `CLOCK_MONOTONIC` 比较；最大误差必须小于 0.5%；STOP
并关闭 fd。TIMER0 回归使用同一流程。

实测（dual 固件，TIMER1）：

```text
[TIMER-001] overflow period accuracy timeout=100000us rounds=2 tol=0.50%
  status: flags=0x3 timeout=100000us timeleft=99935us
  RESULT max_err=337.0us (0.337%) tol=500.0us (0.50%)
  [TIMER-001] PASS accuracy within tolerance
Timer Summary: executed=1 passed=1 failed=0 -> PASS
nsh>
```

### TIMER-002 分频

命令：`mcu_timer_test -c 002 -t 100000 -a 39 -b 79`

流程：分别以 div39/div79 配置 100000us compare，每组丢弃 warm-up 并测稳定
周期；比值应接近 `(79+1)/(39+1)=2.000`，容差 +/-5%；最后恢复 div39。

实测（dual 固件，TIMER0）：

```text
[TIMER-002] clock prescaler effect (div 39 vs 79)
  div=39 period=0.0998s
  div=79 period=0.1999s
  ratio period_b/period_a=2.002 (expected 2.000, tol +/-5%)
  [TIMER-002] PASS prescaler takes effect
Timer Summary: executed=1 passed=1 failed=0 -> PASS
nsh>
```

### TIMER-005 生命周期异常

命令：`mcu_timer_test -c 005`

流程：验证 timeout=1 返回 `EINVAL` 且状态不变；divider=256 返回 `EINVAL`；
START 后重复 START 返回 `EBUSY`；运行中改 divider 返回 `EBUSY`；运行中改 timeout
仍能收到 live signal；首次 STOP 成功、重复 STOP 返回 `ENODEV`；最后读状态确认
ACTIVE 清除并恢复 timeout/divider。

实测（dual 固件，TIMER0）：

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

### TIMER-006 tick ABI

命令：`mcu_timer_test -d /dev/timer1 -c 006`

流程：设置 `USEC_PER_TICK+1=1001us`，确认 tick status 向上量化为 2；设置 3
ticks 并同时读回 3 ticks/3000us；比较两个 MAXTIMEOUT；验证 0 返回 `EINVAL`、
`UINT32_MAX` 在乘法前返回 `ERANGE`；恢复并关闭。

实测：

```text
[TIMER-006] Tick ioctl conversion and boundaries
  rejected: zero tick timeout errno=22
  rejected: tick timeout multiplication overflow errno=34
  tick=1000us rounded=1001us->2 ticks set=3 ticks max=4294967
  [TIMER-006] PASS tick conversion and boundaries
Timer Summary: executed=1 passed=1 failed=0 -> PASS
nsh>
```

### TIMER-007 poll 与单次通知

命令：`mcu_timer_test -d /dev/timer1 -c 007`

流程：设置 50ms，注册 `periodic=false` signal 并 START；一个 poll waiter 必须在
1s 内返回 `POLLIN`；取走 signal 后 GETSTATUS 必须 inactive；同一 fd 的第二个
poll waiter 必须得到 `POLLERR`；STOP/close 清理。

实测：

```text
[TIMER-007] Poll and one-shot notification
  poll revents=0x1 second-waiter revents=0x8
  [TIMER-007] PASS poll, one-shot and single-waiter boundary
Timer Summary: executed=1 passed=1 failed=0 -> PASS
nsh>
```

### TIMER-008 多 fd

命令：`mcu_timer_test -d /dev/timer1 -c 008`

流程：打开同一节点两次，fd1 配 60000us 并 START；fd2 必须读到相同 timeout；
关闭 fd1 后仍等待到期并由 fd2 读到 ACTIVE；fd2 显式 STOP，再关闭最后 fd。

实测：

```text
[TIMER-008] Multi-fd shared state and close lifetime
  shared timeout=60000us active-after-close=yes
  [TIMER-008] PASS shared state and close lifetime
Timer Summary: executed=1 passed=1 failed=0 -> PASS
nsh>
```

### TIMER-009 双实例

命令：`mcu_timer_test -c 009`

流程：同时打开 timer0/timer1，分别配 40ms/70ms 和不同信号；同时 START 并分别
收取到期；交错更新为 30ms/50ms 后再次收取并核对 status；停止 timer0 后 timer1
仍必须 ACTIVE 并继续到期；退出路径分别 STOP/close。

实测：

```text
[TIMER-009] TIMER0/TIMER1 dual-instance isolation
  timer0=30000us timer1=50000us timer1-active-after-timer0-stop=yes
  [TIMER-009] PASS dual IRQ, state and stop isolation
Timer Summary: executed=1 passed=1 failed=0 -> PASS
nsh>
```

### TIMER-010 raw callback

命令：`mcu_timer_test -d /dev/timer1 -c 010`

流程：从 test hook 取得 lower，设置 50ms；第一次 callback 写入 30ms 并返回 true，
第二次返回 false；最多等待 1s，要求计数恰为 2、状态 inactive、timeout=30000us；
最后 STOP、清空 callback/arg 并恢复 timeout。该 case 只在 raw test Kconfig 下出现。

实测：

```text
[TIMER-010] Raw lower-half callback contract
  callbacks=2 next=30000us active=no
  [TIMER-010] PASS true reload, next interval and false stop
Timer Summary: executed=1 passed=1 failed=0 -> PASS
nsh>
```

## oneshot 互斥实测

`nsh` clean build 为 `1224/1224`，`libarch.a` 含
`bl616cl_oneshot.c.o`、`bl616cl_tim.c.o` 和 TIMER0 实例，但没有 TIMER1 普通
实例。USB2 烧录后 `ls /dev` 输出包含 `oneshot`、`timer0`，不包含 `timer1`；
执行以下命令完成 TIMER1 owner 回归：

```text
nsh> oneshot -d 100000 /dev/oneshot
Opening /dev/oneshot
Maximum delay is 4294967295
Starting oneshot timer with delay 100000 microseconds
Waiting...
Finished
nsh> echo alive
alive
nsh>
```

## 限制与后续入口

- 没有分析仪时，不声明 compare IRQ 的绝对时序、抖动、边沿或电气波形；PWM-003/
  004 仍需外部仪器。
- `close()` 不隐式 STOP；当前 board 没有 runtime unregister 入口，因此没有伪造
  teardown 结果。
- TIMER0 capture、compare DMA、TIMER1 作为 WDT automonitor 后端分别等待输入
  pin、DMA/cache 资源和独立 owner/单位方案，不能由本次普通 timer 结果替代。
- 2026-08-30 实板使用 `/dev/ttyUSB2`、2 Mbps；dual、TIMER1 和 oneshot
  配置的烧录/启动输出均回到 `nsh>`，现役 GPIO/WDT/RTC 回归未出现 assert、
  panic 或意外复位。
