# BL616CL RTC 测试说明

## 背景与方案

该目录提供一个 RTC 测试入口 `mcu_rtc_test`，通过 `-c` 选择场景，不拆分
多个 main。测试经 `/dev/rtc0` 的 OpenVela 标准 ioctl 验证 UTC 日历、亚秒、
单路 Alarm 和设备生命周期；RTC-004 直接读取 chip 公开的 48 位 HBN counter。

测试 app 由 `CONFIG_BL_MCU_PERIPHERAL_TESTS_RTC` 独立控制，默认关闭，并依赖
`CONFIG_BL616CL_RTC`。当 `CONFIG_BL616CL_RTC_ALARM=n` 时，RTC-002、
RTC-003 和 RTC-005 报告 SKIP；`-c all` 此时以 `PARTIAL` 汇总并返回非零，
不能作为完整 Alarm 验收通过。单独 SKIP 和 FAIL 也返回非零，完整 PASS 返回 0；
NSH 会把 builtin 的非零结果统一显示为 `$?=1`，自动化必须同时解析明确的
`SKIP`、`PARTIAL` 或 `FAIL` 汇总文本。`CONFIG_RTC_ALARM` 是打开 BL616CL Alarm
后自动选择的上层开关。正式产品不包含测试 app。

## 配置与构建

使用 `menuconfig` 切换下表配置，保存后执行 `savedefconfig`；正式
`defconfig` 只保留最后一行的产品配置。不要直接编辑自动生成的 defconfig。

| 配置 | RTC | Alarm | 测试 app | 时钟源 | clean build |
|---|---:|---:|---:|---|---:|
| 关闭态 | n | n | n | - | 1220/1220 |
| 基础 RTC | y | n | n | DIG32K | 1224/1224 |
| Alarm 关闭验收 | y | n | y | DIG32K | 1226/1226 |
| 验收态 | y | y | y | RC32K、DIG32K 分别构建 | 1226/1226 |
| 正式产品 | y | y | n | DIG32K | 1224/1224 |

五种配置对应的关键选项如下；未列出的 `RTC_*` 依赖由 Kconfig 自动选择：

```text
# 关闭态
# CONFIG_BL616CL_RTC is not set
# CONFIG_BL_MCU_PERIPHERAL_TESTS_RTC is not set

# 基础 RTC
CONFIG_BL616CL_RTC=y
CONFIG_BL616CL_RTC_CLOCK_DIG32K=y
# CONFIG_BL616CL_RTC_ALARM is not set
# CONFIG_RTC_ALARM is not set
# CONFIG_BL_MCU_PERIPHERAL_TESTS_RTC is not set

# Alarm 关闭验收
CONFIG_BL616CL_RTC=y
CONFIG_BL616CL_RTC_CLOCK_DIG32K=y
# CONFIG_BL616CL_RTC_ALARM is not set
# CONFIG_RTC_ALARM is not set
CONFIG_BL_MCU_PERIPHERAL_TESTS_RTC=y

# 验收态；时钟源二选一分别构建和实测
CONFIG_BL616CL_RTC=y
CONFIG_BL616CL_RTC_ALARM=y
CONFIG_BL_MCU_PERIPHERAL_TESTS_RTC=y
CONFIG_BL616CL_RTC_CLOCK_RC32K=y
# 或 CONFIG_BL616CL_RTC_CLOCK_DIG32K=y

# 正式产品
CONFIG_BL616CL_RTC=y
CONFIG_BL616CL_RTC_ALARM=y
CONFIG_BL616CL_RTC_CLOCK_DIG32K=y
# CONFIG_BL_MCU_PERIPHERAL_TESTS_RTC is not set
```

在 SDK 根目录执行：

```bash
vendor/bouffalolab/vela menuconfig \
  bl616cl/ai-m64l-32s-kit/configs/nsh
cmake --build cmake_out/ai-m64l-32s-kit_nsh -t savedefconfig
vendor/bouffalolab/vela clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
vendor/bouffalolab/vela build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

从 RTC-on 切到完整关闭态时，除关闭 `CONFIG_BL616CL_RTC` 外，还要在
`menuconfig` 中关闭之前留下的 `RTC`、`RTC_ALARM`、`RTC_ARCH`、
`RTC_DATETIME` 和 `RTC_DRIVER`，然后执行 `savedefconfig`。否则通用
`RTC_ARCH` 仍会要求 `up_rtc_initialize()`，而 chip RTC object 已被裁剪，链接会
失败。Alarm-only 关闭态同理要确认通用 `RTC_ALARM=n`。

每次 clean build 后检查：

1. 关闭态没有 `bl616cl_rtc*.c.o`、RTC upper-half 和测试 app。
2. 基础态的两个 RTC object 进入 `arch/libarch.a`，但没有 Alarm
   ISR/callback/hardware Alarm 符号，也没有测试 app。
3. 验收态包含 RTC、Alarm 和 `libapps_mcu_rtc_test.a`。
4. 正式产品包含 RTC/Alarm，不包含 `mcu_rtc_test`。
5. `libbl_std.a` 只提供 SDK std 实现，不包含 RTC adapter object。

在 SDK 根目录执行以下命令检查当前构建产物：

```bash
OUT=cmake_out/ai-m64l-32s-kit_nsh
TOOL=prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf
"${TOOL}-ar" t "$OUT/arch/libarch.a" | grep bl616cl_rtc
"${TOOL}-ar" t "$OUT/apps/vendor/bouffalolab/libbl_std.a" | grep bl616cl_rtc
find "$OUT" -name libapps_mcu_rtc_test.a -print
"${TOOL}-nm" -g --defined-only "$OUT/final_nuttx" | grep -E \
  'mcu_rtc_test|bl616cl_rtc|up_rtc_'
```

正式产品实测结果为：`libarch.a` 含 `bl616cl_rtc.c.o` 和
`bl616cl_rtc_hw.c.o`；`libbl_std.a` 不含 RTC adapter；测试 app archive
不存在，ELF 也没有 `mcu_rtc_test` 符号。

## 运行约束

串口设备节点是 `/dev/ttyUSB2`，波特率为 2,000,000。它只是板载
USB-UART 的当前 Linux 设备名，不是 RTC 的依赖。复位后先确认
`NuttShell (NSH)` 和 `nsh>`，保持同一串口 fd，并在收到 prompt 后再发送
下一条命令。

`mcu_rtc_test -c all` 只执行 RTC-001~004。RTC-005 会 unlink
`/dev/rtc0`，必须最后单独执行，完成后复位才能再次访问该节点。

Alarm 关闭验收配置中，`all` 仍执行 RTC-001 和 RTC-004，RTC-002/003
逐项报告 SKIP，并以 `PARTIAL` 汇总；RTC-005 单独报告统一 SKIP。两条命令
在 NSH 中均返回 1，自动化必须同时检查退出码和汇总文本。实测输出为：

```text
[RTC-002] SKIP: CONFIG_BL616CL_RTC_ALARM is not enabled
[RTC-003] SKIP: CONFIG_BL616CL_RTC_ALARM is not enabled
RTC test PARTIAL (0 failures, 2 skipped)
rtc_test_status=1
[RTC-005] SKIP: alarm or pseudo-filesystem unlink is unavailable
RTC test SKIP (requested case is unavailable)
rtc_test_status=1
```

验收固件执行 `help` 后确认命令已注册：

```text
Builtin Apps:
    cpuload               gpio                  mcu_timer_test
    critmon               hello                 mcu_wdt_test
    dumpstack             oneshot               critmon_start
    nsh                   timer                 critmon_stop
    sh                    wdog                  stackmonitor_stop
    stackmonitor_start    mcu_gpio_test
    trace                 mcu_rtc_test
nsh>
```

## RTC-001：时间 ABI、UTC 与边界

命令：

```text
mcu_rtc_test -c 001
```

流程：

1. 读取 fresh-boot 时间和 `RTC_HAVE_SET_TIME`；未设时时必须位于
   2018-01-01 起 30 秒窗口内，Alarm 必须 inactive。
2. 核对 `rtc_time` 与 `tm` 字段布局。
3. 设置 `2024-02-29 12:34:56.123456789 UTC`，要求完整 `(sec,nsec)` 读回
   不早于设值且延迟不超过 200 ms，验证闰日、亚秒和
   `RTC_HAVE_SET_TIME=true`。
4. 连续读取 RTC，要求 `(sec,nsec)` 严格非递减，并在 2 秒内跨过秒边界。
5. 通过 `clock_settime(CLOCK_REALTIME)` 设置
   `2024-03-01 01:02:03.456 UTC`，比较 RTC 与系统墙钟。
6. 逐项拒绝非闰日、非法纳秒、月、日、时、分、秒和 epoch 前日期；比较完整
   `(sec,nsec)`，拒绝前后只允许 RTC 正常前进 0~200 ms。
7. 未知 ioctl 必须返回 `ENOSYS`；验证 2038 边界和 32 位 post-2038
   拒绝，最后按已消耗的单调时间恢复原始时间。

关键输出：

```text
[RTC-001] time ABI, leap day, invalid date, 2038, subsecond
  ABI: rtc_time=48 tm=44 offsets sec=0 min=4 year=20
  fresh-boot baseline: 2018-01-01 00:00:00.084289550 UTC
  fresh-boot HAVE_SET_TIME=0 offset=84289550 ns
  fresh-boot alarm active=0
  RTC_SET_TIME RTC/system delta=177719 ns
  settime readback delta=488281 ns
  subsecond monotonic samples=434 crossed-second=1
  clock_settime RTC/system delta=237307 ns
  invalid non-leap date rejected; RTC advanced 457764 ns
  invalid nanosecond rejected; RTC advanced 335694 ns
  invalid month -1 rejected; RTC advanced 335693 ns
  invalid month 12 rejected; RTC advanced 366211 ns
  invalid day 0 rejected; RTC advanced 366211 ns
  invalid day 32 rejected; RTC advanced 213623 ns
  invalid hour 24 rejected; RTC advanced 366211 ns
  invalid minute 60 rejected; RTC advanced 335693 ns
  invalid second 60 rejected; RTC advanced 366211 ns
  invalid pre-epoch date rejected; RTC advanced 366211 ns
  unknown ioctl rejected with ENOSYS
RTC test PASS (0 failures)
```

DIG32K 同一流程的 settime 读回延迟为 518925 ns，
`subsecond monotonic samples=437 crossed-second=1`，RTC/system 差值为
163550 ns 和 400900 ns，均小于 200 ms 判定门限。标准 ABI 没有把
`RTC_HAVE_SET_TIME` 恢复为 false 的接口，因此测试会明确输出该限制。

## RTC-002：Alarm 触发、查询和 re-arm

命令：

```text
mcu_rtc_test -c 002
```

流程：

1. 屏蔽 `SIGUSR1` 并清空旧信号。
2. 设置 relative 2 秒 Alarm，查询 deadline/active，等待 value 257。
3. 确认触发后 inactive，并等待 200 ms 排除重复通知。
4. 再次设置 relative 2 秒，等待 value 258，证明 re-arm。
5. 根据当前 RTC 设置 absolute 2 秒 Alarm，查询精确 deadline，等待 value 259。

RC32K 关键输出：

```text
[RTC-002] relative/absolute alarm, rearm, active state, signal value
  relative deadline delta=1999298096 ns
  alarm value=257 elapsed=1986 ms
  alarm active=0
  relative deadline delta=1999664306 ns
  alarm value=258 elapsed=1987 ms
  alarm active=0
  alarm active=1 deadline=2018-01-01T00:00:06
  alarm value=259 elapsed=1986 ms
  alarm active=0
RTC test PASS (0 failures)
```

DIG32K 主流程三次耗时均为 1999 ms；计入三轮 warm reset 后的重新布防，
完整实测范围为 1998~2000 ms。两种时钟均只触发一次。

## RTC-003：取消、替换、拒绝与 settime 顺序

命令：

```text
mcu_rtc_test -c 003
```

流程：

1. 设置 relative 3 秒 Alarm，连续 cancel 两次都必须成功；等待 4 秒无信号。
2. 设置 3 秒 Alarm，再以 2 秒 Alarm 替换；只允许 value 513 到达，
   随后查询 active 并确认 value 513 已触发。
3. 过去的 absolute 返回 `-ETIME`；zero/negative relative 返回
   `-EINVAL`。
4. 设置并取消 2038 absolute deadline；32 位配置还要拒绝越界 relative。
5. 设置 2 秒 Alarm 后把 RTC 写回同一时间，Alarm 保持 active，并收到
   value 514；value 513 不得重复出现。
6. 将 RTC 向前设置越过 active deadline，由独立接收线程收到 value 515；
   接收线程从 `sigtimedwait()` 返回后读取的完整墙钟不得早于新设时间，随后 RTC
   与 `CLOCK_REALTIME` 仍需匹配。该观测不等同于 callback 执行时刻。
7. 最多尝试 10 次设置距当前时间 0.5 ms 的短 absolute Alarm；允许编程前已
   过期时返回 `-ETIME` 并重试，成功布防后必须收到 value 516，且只通知一次。
   lower-half 先要求 32 tick 余量，失败时依次扩大为 64、128 tick，并以
   256-tick 窗口完成最后一次编程。

关键输出：

```text
[RTC-003] cancel, replace, reject, settime reprogram
  alarm active=0
  alarm active=0
  relative deadline delta=2999664307 ns
  relative deadline delta=1999755859 ns
  alarm value=513 elapsed=1987 ms
  alarm active=1 deadline=2038-01-19T03:14:07
  alarm active=0
  alarm active=1
  alarm value=514 elapsed=1987 ms
  receiver observed post-settime wall time=1514764820.849763016
  alarm active=0
  alarm value=516 elapsed=2 ms
  alarm active=0
  settime-alarm-order RTC/system delta=1035957 ns
RTC test PASS (0 failures)
```

DIG32K 的 replace/re-arm 耗时为 1999/2000 ms，短 Alarm 为 2 ms，顺序检查
差值为 282500 ns。短 Alarm 收到 value 516 后还通过 200 ms 重复通知静默检查。

## RTC-004：48 位 counter 与频率模型

命令：

```text
mcu_rtc_test -c 004
```

流程：

1. 读取 raw counter，以 `CLOCK_MONOTONIC` 测量约 5 秒后再次读取。
2. 用 48 位 mask 计算 delta，输出模型频率、相对观测频率和误差。
3. 验证 `0xfffffffffff5 -> 0x000000000005` 的软件回绕结果为 16 ticks，
   并验证正反向差值。
4. 连续运行五轮，每轮必须看到 counter 前进、回绕通过和统一 PASS。

五轮关键数据：

```text
RC32K model: 32768 / 1 Hz
observed: 32977948, 32977330, 32978032, 32977972, 32977002 mHz
error:    6407, 6388, 6409, 6407, 6378 ppm

DIG32K model: 40000000 / 1221 Hz (32760032 mHz)
observed: 32759956, 32759864, 32759851, 32759772, 32759761 mHz
error:    2, 5, 5, 7, 8 ppm

wrap: 0xfffffffffff5 -> 0x000000000005, elapsed=16 ticks
RTC test PASS (0 failures)
```

`CLOCK_MONOTONIC` 由当前 MTimer 路径提供，所以这些数字只证明两种 RTC
时钟相对 MTimer 的频率差。它们不是校准仪表给出的绝对 ppm。基于同板同流程的
相对结果，正式配置选择 DIG32K。

## RTC-005：双 fd unlink 生命周期

命令：

```text
mcu_rtc_test -c 005
```

流程：

1. 同时打开 fd1、fd2，使用 fd1 设置 2 秒 Alarm，并通过 fd2确认 active。
2. unlink `/dev/rtc0`，随后新 open 必须返回 `ENOENT`；已有 fd2 仍能读时。
3. 关闭 fd1，通过 fd2 再次确认 Alarm active，证明尚未提前 destroy。
4. 关闭最后一个 fd2，等待 3 秒；不得收到旧 Alarm 信号，也不得出现
   `ASSERT/PANIC/KASAN/UBSAN`。

RC32K 与 DIG32K 的关键输出相同：

```text
[RTC-005] unlink active alarm and pending callback safety
  alarm active=1
  alarm active=1
  unlinked device survived first close; final close canceled alarm
nsh>
```

RTC-005 成功时没有统一 `RTC test PASS` 行，以上输出、3 秒静默窗口、NSH
prompt 和异常扫描共同构成判据。该 case 只覆盖 `SIGEV_SIGNAL` 下的当前
双 fd 顺序，不证明上层任意并发 close/unlink 安全。

## 完整运行顺序

```text
help
mcu_rtc_test -c all
mcu_rtc_test -c 004
mcu_rtc_test -c 004
mcu_rtc_test -c 004
mcu_rtc_test -c 004
```

`all` 已含一轮 RTC-004，上述顺序合计五轮。随后让 RTC-002 运行到 absolute
Alarm active 时执行 warm reset；复位后保持 3 秒静默，再运行 RTC-001 和
RTC-002。保持同一个串口 fd，RC32K 与 DIG32K 各连续执行三轮，结果如下：

```text
RC32K round 1: 2018-01-01 00:00:03.084625244 UTC
RC32K round 2: 2018-01-01 00:00:03.084747314 UTC
RC32K round 3: 2018-01-01 00:00:03.084106445 UTC
DIG32K round 1: 2018-01-01 00:00:03.065747850 UTC
DIG32K round 2: 2018-01-01 00:00:03.065320500 UTC
DIG32K round 3: 2018-01-01 00:00:03.068128800 UTC
fresh-boot HAVE_SET_TIME=0
fresh-boot alarm active=0
post-reset RTC-002: RTC test PASS (0 failures)
rtc_alive
```

该复位路径会重建 RAM epoch，不保持已设墙钟或 active Alarm；它只证明旧 Alarm
在复位后 3 秒内没有泄漏通知，且重新初始化后可再次布防。RTC-005 必须另行复位、
单独执行，并在结束后再次复位。

## 正式产品回归

关闭 `CONFIG_BL_MCU_PERIPHERAL_TESTS_RTC`、恢复 DIG32K 后 clean build 和
运行。关键结果：

```text
Builtin Apps:
    cpuload               trace                mcu_gpio_test
    critmon               gpio                 mcu_timer_test
    dumpstack             hello                mcu_wdt_test
    nsh                   oneshot              critmon_start
    sh                    timer                critmon_stop
    stackmonitor_start    wdog                 stackmonitor_stop
ls /dev/rtc0
 /dev/rtc0
date
Mon, Jan 01 00:00:00 2018
ls /dev/random
 /dev/random
rtc_random_status=0
[GPIO-edge] PASS rejected invalid operations and recovered for 3 cycles
[TIMER-001] PASS accuracy within tolerance
[TIMER-002] PASS prescaler takes effect
[TIMER-005] PASS rejected requests preserved state; live update fired; lifecycle recovered
Finished
PASS: fed 6 times over 3021ms, no reset; watchdog stopped
PASS: invalid/live changes rejected; duplicate lifecycle preserved state
rtc_product_alive
```

全流程脚本返回 `failures=[]`，未观察到
`FAIL/ASSERT/PANIC/KASAN/UBSAN`。最终 fresh clean build 为
`1224/1224`，`final_nuttx` 为 859432 B，text/data/bss 为
458572/15664/20108 B，`nuttx.bin` 为 479888 B，SHA256 为
`c999b23d1e84ba3683ac1c1806627fde3d881d9c7c1520790c2b8dec7ed7aa85`。
烧录器对 app 分区计算的 host/device SHA256 相同，且四个分区均报告
`Verification succeeded`。

## 已知边界

- 当前产品 `CONFIG_SYSTEM_TIME64=n`，只验证到 2038 年 32 位边界。
- 仅验证 `SIGEV_SIGNAL`；`CONFIG_SIG_EVTHREAD=n`，没有验证
  `SIGEV_THREAD` 或 `SIGEV_NONE`。
- 未实现 `RTC_PERIODIC`、多 Alarm、rate adjustment、PDS/HBN wakeup、
  外部 XTAL32K 和断电保持。
- NuttX RTC upper-half 的参数校验、通知 work 和并发销毁需由独立上游任务修复；
  RTC-005 不能扩大解释为这些问题已经解决。
- `RTC_SET_TIME` 的 lower-half 返回后，upper-half 才执行 `clock_synchronize()`；
  当前 crossing 测试覆盖 RTC/system 原先同步的标准路径，不承诺两者预先任意偏离时
  callback 一定晚于该次同步完成。测试只在接收线程返回后读取墙钟，更强顺序需要
  上游显式 completion ABI。
