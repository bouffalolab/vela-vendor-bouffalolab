# BL616CL WDT 测试说明

## 背景与目标

本目录只有一个测试入口 `mcu_wdt_test`，不同场景由 `-c` 选择，不拆分多个
main。测试程序编译为独立的 `libapps_mcu_wdt_test.a`，通过
`/dev/watchdog0` 的 OpenVela 标准 watchdog ioctl 验证 BL616CL 主 TIMER WDT。

本轮测试不只验证固定顺序的启动和喂狗，还覆盖超时边界、运行中重装、重复
操作、双 fd 共享状态、capture handler 替换与取消、automonitor 接管，以及
取消 capture 后恢复真实硬件复位。

## 能力方案与边界

BL616CL LHAL 的主 WDT 支持 reset/interrupt 两种模式、16-bit compare、counter
读取/清零和 WICLR。当前适配固定使用 32768 Hz 时钟源和 divider 31，实际计数
频率为 1024 Hz，每 tick 为 0.9765625 ms。毫秒转 tick 使用向上取整，因此合法
timeout 为 1~63999 ms；64000 ms 已超出 65535 tick。

`CONFIG_BL616CL_WDT` 控制 lower-half 总体；
`CONFIG_BL616CL_WDT_CAPTURE` 只控制 capture 状态、ISR 和 WDT IRQ 资源；当
OpenVela 选择 `WATCHDOG_AUTOMONITOR_BY_CAPTURE` 时由 Kconfig 自动启用该能力，
避免生成没有 capture lower-half 的致命配置。
生产基线不启用 automonitor；专用配置使用 OpenVela 的
`WATCHDOG_AUTOMONITOR_BY_WDOG`，不占用 TIMER0/TIMER1。

本轮明确不包含 HBN WDT1、window/mintime、外部 GPIO 时钟和
automonitor oneshot/timer 后端。BY_CAPTURE 与用户 `WDIOC_CAPTURE` 并存时，上游
upper-half 仍可能被用户替换 handler 而不停止 monitor，因此本轮不把 BY_CAPTURE
作为交付配置；验证固定使用 BY_WDOG。USB-UART 日志能证明命令、回调计数、启动
和复位原因，不能单独证明 IRQ 边沿时序或硬件时钟精度。

## 配置与构建

四个可重复配置分别验证关闭、基础 reset、capture 和 automonitor：

| 配置 | WDT | capture | automonitor | clean build |
|---|---:|---:|---:|---:|
| `nsh-wdt-off` | n | n | n | 1219/1219 |
| `nsh` | y | n | n | 1224/1224 |
| `nsh-wdt-capture` | y | y | n | 1224/1224 |
| `nsh-wdt-automonitor` | y | n | BY_WDOG | 1224/1224 |

在 SDK 根目录执行：

```bash
python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh-wdt-off
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh-wdt-off -j14

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh-wdt-capture
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh-wdt-capture -j14

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh-wdt-automonitor
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh-wdt-automonitor -j14
```

裁剪检查结果：

- off 配置不含 `bl616cl_wdt.c.o`、`bl616cl_wdt_initialize`、测试 archive 或
  `mcu_wdt_test_main`。
- base 和 automonitor 配置含 WDT lower-half，但不含
  `bl616cl_wdt_handler`/`bl616cl_wdt_capture`。
- capture 配置包含 `bl616cl_wdt_handler` 和 `bl616cl_wdt_capture`。
- automonitor 配置包含 `watchdog_automonitor_wdog`，并确认 timeout=3 秒、
  ping interval=1 秒。
- 启用测试时 `libapps_mcu_wdt_test.a` 独立存在，不属于 `libarch.a`；
  lower-half 的 `bl616cl_wdt.c.o` 属于 `arch/libarch.a`。

## 运行前检查

以下流程从固件运行态开始，不包含烧录：

1. 串口使用 `/dev/ttyUSB2`、2000000 baud；打开后立即保持运行态控制线，并尽量
   在同一 fd 内完成命令和复位后的确认。
2. 启动必须出现 `NuttShell (NSH)` 和 `nsh>`。
3. `ls /dev` 必须存在 `watchdog0`。
4. WDT-004 必须使用 `nsh-wdt-capture`；WDT-005 必须使用
   `nsh-wdt-automonitor`。
5. 非复位用例必须显式 STOP；WDT-001 和 WDT-005 的后半段故意不喂狗，正常
   结果是设备复位而不是命令返回。

实测设备节点：

```text
/dev:
 ...
 oneshot
 rtc0
 timer0
 watchdog0
 ...
nsh>
```

## WDT-001：超时复位与复位原因

命令：

```text
mcu_wdt_test -c 001 -t 1000
# 设备自动重启并重新出现 nsh> 后：
mcu_wdt_test -c 001 -s
```

流程：

1. 首先通过 `BOARDIOC_RESET_CAUSE` 读取上一轮复位原因。首次运行不是 WDT 原因
   只作记录，不判本轮失败。
2. 打开 `/dev/watchdog0`，设置 1000 ms timeout，执行 `WDIOC_START`。
3. 不调用 KEEPALIVE；程序以 100 ms 间隔等待，正常情况下硬件复位，原命令不会
   返回 `nsh>`。
4. 等待重新出现 `NuttShell (NSH)` 和 `nsh>`，确认系统完整重启。
5. 执行 `-s`，该模式只读取 reset cause，不打开或启动 WDT。
6. reset cause 必须为 `BOARDIOC_RESETCAUSE_SYS_RWDT`；若三倍 timeout 后仍未
   复位，则报告 `ETIMEDOUT`。

实测输出：

```text
[WDT-001] Timeout reset (report previous round, then trigger)
  Previous reset cause = 1 (not WATCHDOG)
  Arming watchdog (1000ms), NOT feeding. Device will reset now.

NuttShell (NSH) NuttX-3.6.1
nsh> mcu_wdt_test -c 001 -s
[WDT-001] Timeout reset (report previous round, then trigger)
  PASS: previous reset cause = WATCHDOG (SYS_RWDT)
  status-only: watchdog not armed
All selected WDT cases passed
nsh>
```

完成判据：发生真实重启；重启后的 status-only 查询为 `SYS_RWDT`；查询本身不再
布防下一次复位。

## WDT-002：周期喂狗与安全停止

命令：

```text
mcu_wdt_test -c 002 -t 1000 -p 3000 -i 500 -v
```

流程：

1. 先检查 feed interval 小于 timeout；本次为 500 ms < 1000 ms。
2. 设置 timeout 并 START，记录 `CLOCK_MONOTONIC` 起点。
3. 每 500 ms KEEPALIVE；`-v` 在每次喂狗后 GETSTATUS，输出 elapsed 和
   timeleft。
4. 持续到 elapsed 至少 3000 ms，期间不得出现启动日志或 reset cause。
5. 显式 STOP、关闭 fd，返回 NSH；随后至少观察一个 timeout 窗口，确认没有
   延迟复位。

实测输出：

```text
[WDT-002] Periodic keepalive, no reset
  Device: /dev/watchdog0 Timeout: 1000ms Interval: 500ms Duration: 3000ms
  fed #1 elapsed=501ms timeleft=504ms
  fed #2 elapsed=1003ms timeleft=502ms
  fed #3 elapsed=1505ms timeleft=502ms
  fed #4 elapsed=2007ms timeleft=502ms
  fed #5 elapsed=2509ms timeleft=502ms
  fed #6 elapsed=3011ms timeleft=502ms
  PASS: fed 6 times over 3011ms, no reset; watchdog stopped
All selected WDT cases passed
nsh>
```

完成判据：六次 KEEPALIVE 均成功，3000 ms 内无复位，STOP 后继续保持 NSH。

## WDT-003：边界、live timeout 与共享生命周期

命令：

```text
mcu_wdt_test -c 003 -t 1000
```

流程：

1. `SETTIMEOUT=0` 和 `64000` 必须返回 `ERANGE`，状态不能被污染。
2. `SETTIMEOUT=1` 和 `63999` 必须成功，覆盖完整可表达边界。
3. 恢复 1000 ms 并 START；重复 START 必须返回 `EBUSY`。
4. active 状态把 timeout 改为 1001 ms，必须成功并原子重装，而不是沿用旧
   compare。
5. GETSTATUS 必须同时满足 ACTIVE、timeout=1001、timeleft<=1001；等待 20 ms
   后 timeleft 必须下降。
6. active 状态恢复 1000 ms，然后打开第二个 fd；第二个 fd 必须看到同一 ACTIVE
   和 timeout，证明硬件状态是设备全局而非 per-fd。
7. 关闭第二个 fd，首 fd 仍必须看到 ACTIVE；重新打开第二个 fd，由它执行
   KEEPALIVE 和 STOP。
8. 首 fd执行重复 STOP 和 inactive KEEPALIVE；两者都不得隐式重新启动 WDT。
9. 最终 GETSTATUS 必须清除 ACTIVE，timeout 保持 1000 ms；关闭 fd 后观察一个
   timeout 窗口，不得复位。

本 case 的 lifecycle timeout 必须至少 100 ms；1 ms 和 63999 ms 边界只在停止态
验证可接受性，避免 1 ms 参数在用户态调度期间造成预期外复位。

实测输出：

```text
[WDT-003] Boundaries, live timeout and lifecycle, no reset
  PASS: boundaries rejected; live update and lifecycle preserved shared state
All selected WDT cases passed
nsh>
```

首次实板运行曾在 active 重装后的第一笔 GETSTATUS 读到旧 WVR，表现为
`first=0 second=985`。lower-half 现以硬件 counter 为主，并在 WCR 同步窗口读到
超出新 compare 的旧值时，使用最近一次 counter reset 的系统 tick 兜底；修正后
本 case 通过。

## WDT-004：capture、handler 替换与 reset 恢复

命令：

```text
mcu_wdt_test -c 004 -t 1000
```

流程：

1. 安装 handler0；返回的 oldhandler 必须为 NULL。
2. START 后等待最多两倍 timeout；handler0 必须且只能累计一次。ISR 中只计数，
   不打印和阻塞。
3. GETSTATUS 必须同时含 ACTIVE 和 CAPTURE，且不含 RESET。
4. KEEPALIVE 后将 handler0 替换为 handler1；oldhandler 必须准确返回 handler0，
   且替换前后的 timeleft 不得增加，证明 capture 操作没有隐式喂狗。
5. 等待 handler1 到达；最终 handler0/handler1 计数必须为 1/1，证明旧 handler
   不再接收新的到期中断，也没有 IRQ storm。
6. 以 NULL handler 取消 capture；oldhandler 必须返回 handler1，timeleft 同样不得
   增加。
7. GETSTATUS 必须恢复 RESET、清除 CAPTURE；STOP 后返回 NSH。
8. 随后执行 WDT-001 的 1000 ms 流程，重启后必须读取到 `SYS_RWDT`，证明取消
   capture 不只是修改软件 flags，而是真正恢复硬件 reset mode。

实测输出：

```text
[WDT-004] Interrupt capture and reset restore
  callback0 count=1 flags=0x5
  PASS: handlers replaced; capture cancelled; reset restored
All selected WDT cases passed
nsh>

[WDT-001] Timeout reset (report previous round, then trigger)
  Arming watchdog (1000ms), NOT feeding. Device will reset now.
NuttShell (NSH) NuttX-3.6.1
  PASS: previous reset cause = WATCHDOG (SYS_RWDT)
```

`flags=0x5` 表示 ACTIVE|CAPTURE。本测试证明 IRQ callback 单次到达、替换和取消
合同；没有外部逻辑分析仪，因此不声明 1000 ms 中断时序精度。

## WDT-005：automonitor 稳态与用户接管

命令：

```text
mcu_wdt_test -c 005 -t 1000 -p 5000
# 自动重启并重新出现 nsh> 后：
mcu_wdt_test -c 005 -s
```

流程：

1. 使用 `nsh-wdt-automonitor` 启动；OpenVela 在注册 watchdog 时设置 3 秒
   timeout、START，并由 BY_WDOG 每 1 秒 KEEPALIVE。
2. 打开设备并 GETSTATUS；必须为 ACTIVE|RESET。
3. 静置 5000 ms，跨过至少五个 ping 周期和一个完整硬件 timeout；系统必须存活，
   再次 GETSTATUS 仍为 ACTIVE。
4. 设置用户 timeout=1000 ms；active live settimeout 必须成功。
5. 用户执行 `WDIOC_START`；upper-half 先停止 automonitor，再由用户重新启动
   lower-half，完成喂狗责任移交。
6. 测试不再 KEEPALIVE；设备必须复位。重启后执行 WDT-005 status-only，reset
   cause 必须为 `SYS_RWDT`。

实测输出：

```text
[WDT-005] Automonitor stability and handoff
  monitor active flags=0x3; waiting 5000ms
  handoff complete; no KEEPALIVE, device should reset

NuttShell (NSH) NuttX-3.6.1
nsh> mcu_wdt_test -c 005 -s
  PASS: previous reset cause = WATCHDOG (SYS_RWDT)
  status-only: automonitor handoff not performed
All selected WDT cases passed
nsh>
```

`flags=0x3` 表示 ACTIVE|RESET。完成判据：automonitor 独立保持系统至少 5 秒；
用户接管后停止喂狗会真实复位；重启原因闭环为 `SYS_RWDT`。

## all 模式与回归

`mcu_wdt_test -c all` 依次运行 WDT-002、WDT-004、WDT-003，最后运行
WDT-001。WDT-004 在未启用 capture 时明确 SKIP；WDT-001 正常结果是复位，
所以 `all` 不会在同一次启动中打印最终 PASS。重启后仍需执行
`mcu_wdt_test -c 001 -s`。

本轮在 automonitor 固件完成以下现役回归：

```text
date; sleep 2; date
Mon, Jan 01 00:00:00 2018
Mon, Jan 01 00:00:02 2018

[GPIO-edge] PASS rejected invalid operations and recovered for 3 cycles

[TIMER-001] RESULT max_err=216.0us (0.216%) tol=500.0us (0.50%)
[TIMER-001] PASS accuracy within tolerance

[TIMER-002] ratio period_b/period_a=2.000
[TIMER-002] PASS prescaler takes effect

[TIMER-005] PASS rejected requests preserved state; live update fired;
              lifecycle recovered

Starting oneshot timer with delay 100000 microseconds
Waiting...
Finished
nsh> echo final_alive
final_alive
nsh>
```

这些结果证明 automonitor 没有破坏 RTC 单调前进、GPIO 状态恢复、TIMER0 周期/
prescaler/lifecycle、TIMER1 oneshot 和 NSH 存活。它们不替代 GPIO 外部电气、总线
波形或 WDT IRQ 时序的仪器证据。
