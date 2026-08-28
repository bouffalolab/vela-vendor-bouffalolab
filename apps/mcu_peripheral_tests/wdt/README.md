# MCU WDT 外设测试

`wdt_case_main.c` 编译为独立的 `libapps_mcu_wdt_test.a`，注册 NSH 命令
`mcu_wdt_test`，通过 `/dev/watchdog0` 使用标准 NuttX watchdog ioctl。

## 背景与约束

系统启动后 watchdog 可能由内核 auto-monitor 管理。用户调用 WDIOC_START 时 upper-half 停止 auto-monitor，
测试程序接管硬件。接管后的安全收尾必须明确：非复位用例返回前调用 WDIOC_STOP；WDT-001 则故意不停止。

运行前确认固件已进入 `NuttShell (NSH)`，`ls /dev` 可见 `watchdog0`。以下流程从固件运行态开始，不包含烧录。

## WDT-001 超时复位

背景：证明停止喂狗会发生真实硬件复位，并通过下一次启动的 reset cause 完成闭环。

命令：

```text
mcu_wdt_test -c 001 -t 3000
# 重启进入 nsh> 后：
mcu_wdt_test -c 001 -s
```

流程：

1. 第一条命令先调用 BOARDIOC_RESET_CAUSE，打印上一次复位原因；首次运行不是 WDT 原因不算本轮失败。
2. 打开 watchdog0，SETTIMEOUT=3000ms，然后 START；该动作将喂狗责任交给测试程序。
3. 程序明确不调用 KEEPALIVE，每 100ms 等待；正常情况下约 3 秒后芯片复位，命令不会返回 NSH。
4. 等待重新出现 `NuttShell (NSH)` 和 `nsh>`，证明系统完成重启而非仅串口中断。
5. 执行 `-c 001 -s`；只读取 reset cause，不打开和启动 watchdog。
6. reset cause 必须为 `BOARDIOC_RESETCAUSE_SYS_RWDT`；命令打印 status-only 并返回 `nsh>`。
7. 若 9 秒 guard 到期仍未复位，程序打印 `watchdog did not reset`，本 case 失败。

完成判据：阶段一发生重启；阶段二打印 `PASS: previous reset cause = WATCHDOG (SYS_RWDT)`；
`-s` 明确打印 `watchdog not armed` 并返回 NSH。

固件运行关键证据：

```text
[WDT-001] Timeout reset (report previous round, then trigger)
  Arming watchdog (3000ms), NOT feeding. Device will reset now.
... device reboot ...
NuttShell (NSH)
nsh> mcu_wdt_test -c 001 -s
  PASS: previous reset cause = WATCHDOG (SYS_RWDT)
  status-only: watchdog not armed
nsh>
```

## WDT-002 周期喂狗

背景：证明在 timeout 前 KEEPALIVE 能持续避免复位，并验证测试退出后 watchdog 已停止。

命令：`mcu_wdt_test -c 002 -t 3000 -p 9000 -i 1000 -v`

流程：

1. 参数门禁：feed interval 必须小于 timeout；本次为 1000ms < 3000ms。
2. 打开 watchdog0，SETTIMEOUT=3000ms，START 并记录单调时钟起点。
3. 每 1000ms 调用 KEEPALIVE，累计 feeds；`-v` 同时 GETSTATUS 并打印 timeleft。
4. 循环直到 elapsed>=9000ms；这段时间不能出现启动日志或 reset cause。
5. 调用 STOP；即使 STOP 失败也必须显式报告 warning，不能静默返回。
6. 关闭 fd，打印 feed 次数和实际 elapsed，等待回到 `nsh>`。
7. 返回 NSH 后额外观察一个 timeout 窗口（至少 3 秒）；不得发生延迟复位。

完成判据：9 秒内无复位，KEEPALIVE 全部成功，STOP 后仍保持 NSH。

固件运行关键证据：

```text
[WDT-002] Periodic keepalive, no reset
  Device: /dev/watchdog0 Timeout: 3000ms Interval: 1000ms Duration: 9000ms
  fed #1 elapsed=1001ms
  ...
  fed #9 elapsed=9009ms
  PASS: fed 9 times over 9009ms, no reset; watchdog stopped
nsh>
```

## WDT-003 生命周期与异常请求

背景：验证非法 timeout、重复 START、运行中改 timeout 和停止态操作不会改变有效状态或留下隐性复位。

命令：`mcu_wdt_test -c 003 -t 3000`

流程：

1. 打开 watchdog0；SETTIMEOUT=0 必须返回 ERANGE。
2. SETTIMEOUT=65536 必须返回 ERANGE，覆盖 16-bit compare 上界外的请求。
3. SETTIMEOUT=3000 并 START；再次 START 必须返回 EBUSY。
4. 选择仍在合法范围内且不同于 3000 的相邻 timeout，运行中 SETTIMEOUT 必须返回 EBUSY。
5. GETSTATUS：flags 必须包含 ACTIVE，timeout 必须仍为 3000，证明拒绝请求未污染状态。
6. STOP 必须成功。停止态再次 STOP 和 KEEPALIVE 都必须成功且不能重新启动硬件。
7. 再次 GETSTATUS：ACTIVE 必须清零，timeout 仍为 3000。
8. 关闭 fd，返回 `nsh>` 后观察至少一个 timeout 窗口，确认没有隐藏复位。

完成判据：所有错误码符合契约，active/inactive 状态正确，命令返回后无复位。

固件运行关键证据：

```text
MCU Peripheral WDT Tests
Case: 003 Device: /dev/watchdog0 Timeout: 3000ms
[WDT-003] Lifecycle and rejected requests, no reset
  PASS: invalid/live changes rejected; duplicate lifecycle preserved state
All selected WDT cases passed
nsh>
```

## all 模式

`mcu_wdt_test -c all` 先执行 WDT-002，再执行 WDT-003，最后执行 WDT-001。前两个用例必须完成 STOP；
最后一个用例按设计触发复位，因此 `all` 正常成功时不会在同一次启动中返回 `All selected WDT cases passed`。
重启后仍需手动执行 `mcu_wdt_test -c 001 -s`，才完成 WDT-001 的复位原因验收。
