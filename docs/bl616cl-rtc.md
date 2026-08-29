# BL616CL RTC/Alarm 适配说明

## 背景

BL616CL HBN 域提供 48 位自由运行 counter 和一路 compare。OpenVela 通过
`RTC_DATETIME`、`RTC_DRIVER` 和 arch RTC 接口在启动早期取得墙钟；系统
elapsed time 仍由 MTimer 提供。本适配把硬件接入标准 `/dev/rtc0`，不增加
BL 私有用户 ABI，也不把 HBN 慢时钟替换为 scheduler tick。

当前 warm reset 路径会重建 RAM 中的 epoch/counter snapshot。实测复位后
`RTC_HAVE_SET_TIME=0`、Alarm inactive，墙钟从 2018 基线重新开始。因此本适配
不声明 battery-backed、跨 reset 墙钟保持或断电保持。

## 最大能力交集

| OpenVela 能力 | BL616CL 依据 | 结论 |
|---|---|---|
| `RTC_DATETIME` | 48 位 counter 可换算 UTC 日期时间 | 纳入；启动时 seed 系统墙钟 |
| `ARCH_HAVE_RTC_SUBSECONDS` | 约 32.7 kHz，可换算亚秒 | 纳入；保留 `tm_nsec` |
| `RTC_DRIVER`、`RTC_ARCH` | lower-half 可同时服务 arch 和字符设备 | 纳入；注册 `/dev/rtc0` |
| `RTC_ALARM`、`RTC_NALARMS=1` | 一路 48 位 compare 和 HBN_OUT0 IRQ | 纳入；absolute/relative、query/cancel/replace/re-arm |
| `RTC_HIRES` | HBN 可提供高分辨率 counter | 排除；当前 MTimer 已拥有 elapsed time，RAM epoch 不适合完全替代 |
| `RTC_PERIODIC` | compare 可在 ISR 中软件重装 | 独立补全；需先设计与单路 Alarm 的资源互斥 |
| `RTC_ADJTIME` | 没有已接入的硬件 rate-adjust API | 排除；不伪造校频接口 |
| `SYSTEM_TIME64` | counter 范围足够，但当前系统配置关闭 | 独立补全；需验证 2100/2400 和远期 Alarm |
| `SIGEV_SIGNAL` | RTC upper-half 标准通知路径 | 纳入并实测 |
| `SIGEV_NONE`、`SIGEV_THREAD` | 上层 ABI 存在，当前 `SIG_EVTHREAD=n` | 未验证；上层生命周期问题独立修复 |
| PDS/HBN wakeup | HBN RTC 可作为唤醒源 | 延后；依赖 PM policy 和 HBN_OUT0 公共 demux |
| XTAL32K | HBN mux 支持 | 排除；当前板没有外部晶振硬件依据 |
| HBN XTAL divider | setter 的 divider 参数只有 8 bit | 排除；无法表达约 1221 的目标分频 |
| 多 Alarm | 只有一路 compare | 不支持；编译期要求 `RTC_NALARMS=1` |

`HBN_OUT0` 是聚合 IRQ。本次 RTC Alarm 独占该 IRQ；未来 HBN GPIO、PDS 或
其他唤醒源接入前必须建立单 owner 的公共 demux。

## 实现方案

1. `chips/bl616cl/bl616cl_rtc_hw.c` 只声明所需 SDK ABI，调用
   `HBN_Set_RTC_CLK_Sel`、`HBN_32K_Sel`、`HBN_Get_RTC_Timer_Val`、
   `HBN_Set_RTC_Timer` 和两阶段清中断函数，避免 SDK 头文件与 NuttX
   `ERROR`/IRQ 定义冲突。
2. `chips/bl616cl/bl616cl_rtc.c` 在 RAM 保存 `epoch_base`、
   `counter_base` 和纳秒基准。读取时锁存 HBN counter，以 48 位 mask
   计算 delta，再用 UTC `timegm()`/`gmtime_r()` 做严格日历转换。
3. DIG32K 使用 `40,000,000 / 1221 Hz` 的有理数模型；RC32K 使用 SDK
   名义值 32768 Hz。ticks/时间换算避免 64 位中间值溢出并向未来 deadline
   上取整。
4. `up_rtc_initialize()` 在 `clock_initialize()` 早期初始化硬件、
   snapshot 和静态 lower-half，并调用 `up_rtc_set_lowerhalf()`；
   board late bringup 只负责 `rtc_initialize(0, lower)`。
5. 编程 Alarm 前先清旧 compare mode 和 RTC status；写 compare 后复读 counter，
   确认目标仍至少保留 32 ticks，过近或已越过时依次扩大到 64、128、256 ticks
   重试。ISR 在 spinlock 内确认 pending、清硬件并取出 callback，在锁外回调，
   避免 callback 重入 lower-half 时死锁。
6. `RTC_SET_TIME` 会按 absolute deadline 重编程 active Alarm。新时间越过
   deadline 时请求近期 compare，并由 32-tick 编程保护确保 compare 不会在生效前
   被 counter 越过；在 RTC/system 原先同步的标准路径中，ISR 通过系统墙钟判断
   upper-half 是否已完成 `clock_synchronize()`，未同步时继续延后通知。
7. lower-half destroy 清 callback、compare 和 IRQ；当前单线程双 fd unlink
   流程已验证。NuttX upper-half 的并发销毁和通知 work 问题不由 vendor
   adapter 掩盖，另建上游任务处理。

RTC adapter 随 custom chip 编入 `arch/libarch.a`，不进入 `libbl_std.a`。
测试 app 单独生成 `libapps_mcu_rtc_test.a`，正式配置关闭。

## Kconfig 与裁剪

正式配置：

```text
CONFIG_BL616CL_RTC=y
CONFIG_BL616CL_RTC_ALARM=y
CONFIG_BL616CL_RTC_CLOCK_DIG32K=y
# CONFIG_BL_MCU_PERIPHERAL_TESTS_RTC is not set
```

`CONFIG_BL616CL_RTC` 自动选择 `RTC`、`RTC_DATETIME`、`RTC_DRIVER`、
`RTC_ARCH` 和 `ARCH_HAVE_RTC_SUBSECONDS`。`CONFIG_BL616CL_RTC_ALARM`
独立选择 `RTC_ALARM`；关闭后 Alarm ISR、callback、HBN compare API 和 IRQ
attach 均不编译。若从 Alarm-on 配置切换到完整 Alarm-off 裁剪态，还要在
`menuconfig` 中确认通用 `CONFIG_RTC_ALARM=n`，再执行 `savedefconfig`。

六种 fresh clean build：

| 配置 | 结果 | archive/符号门禁 |
|---|---:|---|
| RTC off | 1220/1220 | RTC object、upper-half、测试 app 均不存在 |
| RTC on、Alarm off | 1224/1224 | RTC object 在 `libarch.a`；Alarm/test 不存在 |
| RTC on、Alarm off、test | 1226/1226 | RTC 基础用例执行；Alarm 用例明确 SKIP/PARTIAL |
| RTC+Alarm+test，RC32K | 1226/1226 | RTC/Alarm/test 均存在 |
| RTC+Alarm+test，DIG32K | 1226/1226 | RTC/Alarm/test 均存在 |
| RTC+Alarm、no-test，DIG32K | 1224/1224 | RTC/Alarm 存在；测试 app 不存在 |

配置和构建命令：

```bash
python3 vendor/bouffalolab/bl_build.py menuconfig \
  bl616cl/ai-m64l-32s-kit/configs/nsh
cmake --build cmake_out/ai-m64l-32s-kit_nsh -t savedefconfig
python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

defconfig 是自动生成文件；切换配置后通过 `savedefconfig` 保存，不直接编辑。从
RTC-on 切到 RTC-off 时必须一并关闭遗留的通用 `RTC`、`RTC_ALARM`、
`RTC_ARCH`、`RTC_DATETIME`、`RTC_DRIVER`；只关闭 BL 私有开关会留下通用
arch RTC 依赖，链接时仍要求已经被裁剪的 `up_rtc_initialize()`。

## 测试流程

测试 app 的逐 case 命令、完整动作和判据见
`apps/mcu_peripheral_tests/rtc/README.md`。这里记录系统级执行顺序：

1. 完成 RTC off、基础 RTC 和 RTC+Alarm 的 clean build 与 archive/nm 裁剪门禁。
2. 在 RTC on、Alarm off 配置启用测试 app，执行 `mcu_rtc_test -c all` 和
   `mcu_rtc_test -c 005`，确认基础用例运行、Alarm 用例明确 SKIP，`all` 以
   `PARTIAL (0 failures, 2 skipped)` 汇总；两条命令均返回非零。NSH 会统一
   显示为 `$?=1`，runner 必须结合汇总文本区分 SKIP/PARTIAL 与 FAIL；完整
   PASS 返回 0。
3. 启用测试 app，以 RC32K clean build；启动后用同一串口 fd 执行
   `help`、`mcu_rtc_test -c all`，再补四轮 RTC-004。
4. 让 RTC-002 运行到 absolute Alarm active 后 warm reset；保持 3 秒静默，
   再跑 RTC-001、RTC-002 和存活命令；同一 fd 连续执行三轮。
5. 复位后独立执行 RTC-005；该 case unlink `/dev/rtc0`，结束后再次复位。
6. 切换 DIG32K，重复步骤 3~5。
7. 恢复正式 DIG32K、关闭测试 app，clean build 并回归 `/dev/rtc0`、TRNG、
   GPIO、timer、oneshot、WDT 和 NSH。

串口节点 `/dev/ttyUSB2` 只用于烧录、复位、日志和 NSH 交互，不是 RTC 功能
依赖。运行阶段保持单 fd，所有命令等待 `nsh>` 后再发送。

## 实测数据

### Alarm 关闭分支

```text
[RTC-002] SKIP: CONFIG_BL616CL_RTC_ALARM is not enabled
[RTC-003] SKIP: CONFIG_BL616CL_RTC_ALARM is not enabled
RTC test PARTIAL (0 failures, 2 skipped)
rtc_test_status=1
[RTC-005] SKIP: alarm or pseudo-filesystem unlink is unavailable
RTC test SKIP (requested case is unavailable)
rtc_test_status=1
```

该配置的 RTC-001、RTC-004 正常执行，两条命令在 NSH 中均返回 1，runner 在核对退出码
和汇总文本后为 `failures=[]`。这只证明 Alarm 关闭时测试入口可用、基础 RTC
可运行且 Alarm case 不产生假 PASS。

### UTC、亚秒和系统同步

```text
[RTC-001] time ABI, leap day, invalid date, 2038, subsecond
  ABI: rtc_time=48 tm=44 offsets sec=0 min=4 year=20
  fresh-boot HAVE_SET_TIME=0 offset=84045410 ns
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

上块为 RC32K；DIG32K settime 读回延迟为 518925 ns，输出
`subsecond monotonic samples=437 crossed-second=1`，两次 RTC/system 差值为
163550 ns、400900 ns。两种时钟均通过 200 ms 门限。

### Alarm

```text
RC32K relative/re-arm/absolute: 1986, 1987, 1986 ms
DIG32K relative/re-arm/absolute: 1999, 1999, 1999 ms
replace value=513: RC32K 1987 ms, DIG32K 2000 ms
same-time re-arm value=514: RC32K 1987 ms, DIG32K 2000 ms
settime-crossing value=515: receiver observed post-settime wall time
short absolute value=516: DIG32K 2 ms
settime-alarm-order RTC/system delta: RC32K 1035957 ns, DIG32K 282500 ns
RTC test PASS (0 failures)
```

cancel、重复 cancel、replace、active query、过去 absolute、zero/negative
relative、2038 absolute、settime reprogram、200 ms 重复通知静默检查和再次
布防均通过。

### 频率与 48 位回绕

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

测量参考是由 MTimer 提供的 `CLOCK_MONOTONIC`，所以 ppm 只表示相对 MTimer
的频率差，不是绝对校准结果。DIG32K 在相同条件下明显优于 RC32K，因此选择为
正式默认；RC32K 保留为 Kconfig 可选项。

### warm reset 与 unlink

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

两种时钟各执行三轮 active Alarm warm reset。每轮复位后 3 秒内没有旧信号，
fresh boot 和重新布防通过。这证明当前 reset 路径不保持 wall clock 或 Alarm，
不外推到断电场景。

RTC-005 使用两个 fd：

```text
[RTC-005] unlink active alarm and pending callback safety
  alarm active=1
  alarm active=1
  unlinked device survived first close; final close canceled alarm
nsh>
```

unlink 后新 open 返回 `ENOENT`，已有 fd2 仍可读；关闭 fd1 不提前 destroy，
关闭最后一个 fd2 后等待 3 秒无旧信号。

### 正式产品回归

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

正式固件保留 `/dev/rtc0`，不含 `mcu_rtc_test`。TRNG 读取和现役外设回归
通过，运行脚本返回 `failures=[]`，全流程未观察到
`FAIL/ASSERT/PANIC/KASAN/UBSAN`。最终 fresh clean build 为
`1224/1224`，`final_nuttx` 为 859432 B，text/data/bss 为
458572/15664/20108 B，`nuttx.bin` 为 479888 B，SHA256 为
`c999b23d1e84ba3683ac1c1806627fde3d881d9c7c1520790c2b8dec7ed7aa85`。
烧录器读取 app 分区后的 device SHA256 与 host SHA256 相同。

## 限制与后续

- 当前 `SYSTEM_TIME64=n`，日历和 Alarm 只验证到 2038 年 32 位边界。
- 当前只实测 `SIGEV_SIGNAL`；`CONFIG_SIG_EVTHREAD=n`，未验证
  `SIGEV_THREAD` 或 `SIGEV_NONE`。
- `RTC_PERIODIC`、64 位时间、完整通知模式、PM/HBN wakeup 和 HBN_OUT0
  demux 分别通过后续子任务补全。
- NuttX RTC upper-half 仍需补运行时参数校验、唯一销毁权、producer/work
  quiesce 和 `SIGEV_THREAD` 代际管理；这些是独立上游问题。
- `RTC_SET_TIME` 先调用 lower `settime()`，返回后才由 upper-half 执行
  `clock_synchronize()`；现有 lower ABI 没有同步完成 hook。本次只证明
  RTC/system 原先同步的标准 crossing 路径中，接收线程返回后观察到的墙钟不早于
  新设时间；不承诺 callback 执行时刻晚于该次同步完成，更强顺序需上游 ABI 扩展。
- RTC-005 只证明当前单线程双 fd 顺序和 `SIGEV_SIGNAL` 场景，不能作为
  upper-half 并发生命周期安全的证明。
- 没有外部 XTAL32K、电池或功耗测量，不能声明外部晶振、掉电保持或低功耗收益。
