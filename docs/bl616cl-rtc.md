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

## ST017 RTC upper-half ioctl 参数校验

### 背景

P03 实板回归之外，OpenVela 通用 RTC upper-half 的九个标准 ioctl 在
`CONFIG_DEBUG_ASSERTIONS=n` 时仍可能把 NULL 指针传给 lower-half，或用非法
Alarm/periodic ID 访问 upper-half 状态。`DEBUGASSERT` 在 release 配置中不求值，
所以这不是 BL616CL 硬件异常，而是所有 RTC lower-half 都可能遇到的公共接口缺口。

### 方案

在每个标准 ioctl 首次解引用参数、访问 `alarminfo[]`、访问 periodic 状态或调用
lower-half 之前加入显式运行时判断：

| ioctl | 合法输入 | 非法输入结果 |
|---|---|---|
| `RTC_RD_TIME` | 非 NULL `rtc_time *` | `EINVAL`，不调用 lower |
| `RTC_SET_TIME` | 非 NULL `rtc_time *` | `EINVAL`，不调用 lower，不同步系统时间 |
| `RTC_HAVE_SET_TIME` | 非 NULL `bool *` | `EINVAL`，不调用 lower |
| `RTC_SET_ALARM` | 非 NULL，`id < CONFIG_RTC_NALARMS` | `EINVAL`，upper/lower 状态不变 |
| `RTC_SET_RELATIVE` | 非 NULL，`id < CONFIG_RTC_NALARMS` | `EINVAL`，upper/lower 状态不变 |
| `RTC_CANCEL_ALARM` | `0 <= id < CONFIG_RTC_NALARMS` | `EINVAL`，不索引状态、不调用 lower |
| `RTC_RD_ALARM` | 非 NULL，`id < CONFIG_RTC_NALARMS` | `EINVAL`，输出和状态不变 |
| `RTC_SET_PERIODIC` | 非 NULL 且 `id == 0` | `EINVAL`，单实例状态不变 |
| `RTC_CANCEL_PERIODIC` | `id == 0` | `EINVAL`，不调用 lower |

scalar cancel 命令先校验原始 `unsigned long arg`，再转换为 `int`，避免在更宽
ABI 上将 `0x100000000` 截断成合法 ID 0。lower 方法缺失仍返回 `ENOSYS`，未识别
的私有 ioctl 仍按原样转发；本项不改变日期字段、相对时间、周期、PID、
`sigevent`、锁序或销毁生命周期。

### 测试构成

测试 app 位于 `apps/os_feature_tests/rtc_ioctl/`，由
`CONFIG_BL_OS_FEATURE_TESTS_RTC_IOCTL` 控制，默认关闭并且只允许 flat built-in
配置。kernel fake lower-half 注册 `/dev/rtc99` 和一个所有方法均缺失的
`/dev/rtc98`，记录 lower 方法及顺序、调用次数、ID、Alarm 时间、relative 秒数、
periodic timespec、callback/priv 有效性、私有命令与参数、guard canary 和 destroy
次数。测试完成后关闭 fd 并 unlink 两个节点，确认 fake lower 被销毁一次。

`rtc_ioctl_test -c preflight` 只执行两个不会使旧实现崩溃的 NULL 用例，用来证明
未修复 upper-half 的红测；`rtc_ioctl_test -c all` 按下列顺序执行：

1. NULL 的 `RD_TIME`、`SET_TIME`、`HAVE_SET_TIME`，再执行三个基础 ioctl 的
   正常读写和返回值检查。
2. 在缺失方法设备上，用合法参数逐项调用九个标准 ioctl，确认每项为
   `ENOSYS`，而不是把缺失方法误报为 `EINVAL` 或成功。
3. Alarm 先合法布防 ID 0，再覆盖 NULL、结构 ID 上界、scalar cancel 负值/上界和
   查询输出 guard。非法矩阵后用合法 `RD_ALARM` 确认 `active` 与原 Alarm 时间均未
   改变；再执行 `SET_RELATIVE`，要求 lower 顺序为 `cancelalarm -> setrelative`，
   证明 upper 仍观察到原 active 状态，最后取消。
4. periodic 先合法设置 ID 0，再覆盖 NULL、非零结构 ID、scalar 负值/正值。后续
   合法替换必须调用 `cancelperiodic -> setperiodic`，证明非法请求没有清除 active
   状态，最后取消。在 `ULONG_MAX > UINT_MAX` 的 ABI 上额外覆盖低 32 位为零的
   wrapped ID；BL616CL RV32 上该条件为假，但原始 `arg` 检查覆盖目标机可表达值。
5. `CONFIG_RTC_IOCTL` 开启时调用一个私有命令，确认命令号、`arg` 和返回值均按原样
   在 upper/lower 之间转发。
6. 关闭 fd、unlink 节点，确认 destroy 和 canary；程序最后用 `echo $?` 读取退出码。

每项非法输入的判据都是：返回 `ERROR`、`errno=EINVAL`、fake snapshot 完全不变；
缺失方法的判据是 `ERROR`、`errno=ENOSYS`；正常路径除了返回 0 或 fake private
ioctl 的固定结果 77，还必须逐项匹配预期 lower 方法、调用顺序和完整参数快照。

### 命令与流程

```bash
# debug：构建后按板卡流程烧录，再复位确认 NSH
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh-rtc-ioctl-debug -j14
python3 vendor/bouffalolab/.agents/skills/bl-module-reset/scripts/bl_module_reset.py \
  --port /dev/ttyUSB2 --baudrate 2000000 \
  --expect "NuttShell (NSH)" --expect "nsh>"

# 保持同一串口 fd，等待每条命令返回 nsh>
rtc_ioctl_test -c preflight
rtc_ioctl_test -c all
echo $?
```

运行时不需要重新打开串口，也不发送 `reboot`；`/dev/ttyUSB2` 只是本次实测板卡的
USB-UART 控制台，不是 RTC 设计或运行依赖。release 流程只把
`DEBUG_ASSERTIONS` 关闭后重新 clean build，其余运行命令完全相同。

宽 ABI 使用标准 NuttX `sim/nsh` x86-64 配置，打开 RTC driver、Alarm、periodic、
private ioctl 和同一测试 app；启动模拟器后仍执行 `rtc_ioctl_test -c all` 和
`echo $?`。

### 实测数据

| 配置 | 构建 | 串口结果 | 断言数 |
|---|---:|---|---:|
| 未修复 debug 红测 | 1228/1228 | 两个 NULL 均为 `EFAULT`，`failures=2`，无 panic | 2 |
| 修复 debug，全 Alarm/periodic/private | 1228/1228 | `failures=0`，NSH `$?=0` | 39 |
| 修复 release，`DEBUG_ASSERTIONS=off` | 1227/1227 | `failures=0`，NSH `$?=0` | 39 |
| Alarm-only，periodic/private 关闭 | 1228/1228 | `failures=0`，禁用 case 未出现 | 29 |
| 基础 RTC，Alarm/periodic/private 关闭 | 1228/1228 | `failures=0`，禁用 case 未出现 | 11 |
| NuttX sim x86-64，全功能 | 1216/1216 | wrapped-ID case 均 PASS，`$?=0` | 41 |

debug 实测输出为：

```text
RTC ioctl validation: assertions=on alarm=on periodic=on
RTC ioctl validation: cases=39 failures=0 result=PASS
0
```

release 实测输出为：

```text
RTC ioctl validation: assertions=off alarm=on periodic=on
RTC ioctl validation: cases=39 failures=0 result=PASS
0
```

Alarm-only 输出 `assertions=on alarm=on periodic=off`、
`cases=29 failures=0 result=PASS`；基础输出
`assertions=on alarm=off periodic=off`、`cases=11 failures=0 result=PASS`。
四种配置均在 `/dev/ttyUSB2`、2 Mbps 上完成烧录后复位，启动匹配
`NuttShell (NSH)` 和 `nsh>`。

x86-64 输出额外包含：

```text
PASS: RTC-VAL-108A CANCEL_ALARM wrapped ID
PASS: RTC-VAL-205A CANCEL_PERIODIC wrapped ID
RTC ioctl validation: cases=41 failures=0 result=PASS
0
```

修复后的 debug/release 固件还分别完成真实 `/dev/rtc0` RTC 运行路径验证；
正式产品配置重新 clean build 为 1224/1224，`nuttx.bin` 为 479600 B，SHA256 为
`43c2b253669b020741c233c250ca452d7fb3f78f74b67ac8b0383fa6c3404d24`。关闭测试
Kconfig 后，最终 ELF 中不存在 fake lower、测试 main 或测试 archive 符号；实板
`help` 同时不含 `rtc_ioctl_test` 和 `mcu_rtc_test`。正式固件的 `/dev/rtc0` 存在，
两次 `date` 在 `sleep 2` 前后从 `2018-01-01 00:00:00` 前进到 `00:00:02`；
TRNG 读取 128 B、GPIO edge、timer 001/002/005、oneshot、WDT 002/003 和 NSH
全部返回 0。测试配置另完成真实 RTC 001-004 深测，报告
`RTC test PASS (0 failures)`。两组回归均未观察到
`FAIL/ASSERT/PANIC/KASAN/UBSAN/RV Exception`。

### 限制与后续

- 当前 `SYSTEM_TIME64=n`，日历和 Alarm 只验证到 2038 年 32 位边界。
- 当前只实测 `SIGEV_SIGNAL`；`CONFIG_SIG_EVTHREAD=n`，未验证
  `SIGEV_THREAD` 或 `SIGEV_NONE`。
- `RTC_PERIODIC`、64 位时间、完整通知模式、PM/HBN wakeup 和 HBN_OUT0
  demux 分别通过后续子任务补全。
- NuttX RTC upper-half 的运行时参数校验已由 ST017 补齐；唯一销毁权、
  producer/work quiesce 和 `SIGEV_THREAD` 代际管理仍是独立上游问题。
- `RTC_SET_TIME` 先调用 lower `settime()`，返回后才由 upper-half 执行
  `clock_synchronize()`；现有 lower ABI 没有同步完成 hook。本次只证明
  RTC/system 原先同步的标准 crossing 路径中，接收线程返回后观察到的墙钟不早于
  新设时间；不承诺 callback 执行时刻晚于该次同步完成，更强顺序需上游 ABI 扩展。
- RTC-005 只证明当前单线程双 fd 顺序和 `SIGEV_SIGNAL` 场景，不能作为
  upper-half 并发生命周期安全的证明。
- 没有外部 XTAL32K、电池或功耗测量，不能声明外部晶振、掉电保持或低功耗收益。
