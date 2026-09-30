# BL616CL Mutex 优先级继承

本文说明 BL616CL 如何启用 OpenVela 通用的 mutex 优先级继承（priority
inheritance，PI），哪些同步对象参与继承，`SEM_PREALLOCHOLDERS` 的取值原因，
以及测试程序和实板结果。PI 与芯片无关，BL616CL 不需要私有代码。

## 背景

低优先级线程持有 mutex 时，高优先级线程等待它；这时只要有一个中优先级线程
一直占用 CPU，持有者就得不到运行机会，高优先级线程的等待时间由中优先级线程
决定，这就是无界的优先级反转。打开 PI 后，持有者在有更高优先级的等待者时
临时提升到等待者的优先级，释放 mutex 后恢复原优先级。

## 配置

`nsh-peripherals`、`ostest` 默认打开；`nsh` 保持最小配置，不打开。

```text
CONFIG_PRIORITY_INHERITANCE=y
CONFIG_SEM_PREALLOCHOLDERS=0
```

打开后参与继承的对象：

| 对象 | 协议 | 说明 |
|---|---|---|
| `nxmutex`、`nxrmutex` | `SEM_PRIO_INHERIT` | 驱动、文件系统、网络栈的锁，`nxmutex_init()` 自动设置 |
| pthread mutex | `PTHREAD_PRIO_INHERIT` | `PTHREAD_MUTEX_DEFAULT_PRIO_INHERIT` 随 PI 默认选中，`pthread_mutex_init(m, NULL)` 即带继承 |
| 普通 `sem_t` | `SEM_PRIO_NONE` | 默认不参与，用于中断通知、计数的信号量不需要改动 |

wl80211 的 `rtos_semaphore_*`、DMA 完成通知、Wi-Fi 扫描和连接等待都是普通
信号量，保持 `SEM_PRIO_NONE`；`rtos_mutex_*` 基于 `nxmutex`，自动参与继承。

### SEM_PREALLOCHOLDERS=0

每个参与继承的对象要记录持有者（`nuttx/sched/semaphore/sem_holder.c`）：

- 大于 0 时，持有者记录来自一个全局池，所有对象共用。同一时刻被持有的 PI
  mutex 总数超过池大小时，`nxsem_allocholder()` 直接 `PANIC()`。默认值 8
  就是全系统同时持有 PI mutex 的上限；Wi-Fi 网络栈下的实际峰值没有测量。
- 等于 0 时，每个 `sem_t` 内置一个持有者槽。mutex 只有一个持有者，释放时先
  清旧持有者再登记新持有者（`sem_post.c`），内置槽永远够用。

代价是每个 `sem_t` 大约增加 12 字节。约束：不要对会被多个线程同时持有的计数
信号量设置 `SEM_PRIO_INHERIT`，否则第二个持有者登记时 `PANIC()`。当前构建中
只有 ostest 的 `priority_inheritance` 这样用，并且它在 `SEM_PREALLOCHOLDERS`
不大于 3 时只用一个低优先级持有者。

### 边界

- 只提升直接持有者，不沿锁链传递：被提升的持有者如果又在等另一把锁，那把锁
  的持有者不会被提升。
- ostest 的 `cond_test` 与 PI 不兼容，打开后打印 `Skipping` 跳过。
- `nsh` 配置不打开 PI，需要时按上面两项加入 defconfig。

## 测试程序

`apps/os_feature_tests/prio_inherit`，选项
`CONFIG_BL_OS_FEATURE_TESTS_PRIO_INHERIT`，依赖 `PRIORITY_INHERITANCE`，
`ostest` 配置打开。命令 `prio_inherit_test [all|default|nxmutex|none|nested|timeout]`，
不带参数时运行全部用例。

线程优先级：持有者 101、中优先级忙等 105、等待者 110 和 112、控制线程 115，
都低于 `wifi_fw`（130）和 LPWORK（150）。控制线程用 `pthread_getschedparam()`
读取持有者的有效优先级。

| 用例 | 场景 | 通过条件 |
|---|---|---|
| `default` | `pthread_mutex_init(NULL)` 的 mutex；持有者、忙等 200 ms 的中优先级线程、等待者 | 持有者提升到 110；等待者在中优先级线程结束前拿到锁；释放后持有者回到 101 |
| `nxmutex` | 同上，换成驱动使用的 `nxmutex` | 同上 |
| `none` | 同上，`PTHREAD_PRIO_NONE`，对照组 | 持有者不提升；等待者在中优先级线程结束后才拿到锁，证明用例能测出反转 |
| `nested` | 持有者持两把锁，等待者分别为 110、112；两种释放顺序各一次 | 持有者提升到 112；先放 110 的锁后仍为 112，先放 112 的锁后降为 110；全部释放后回到 101 |
| `timeout` | 等待者用 `pthread_mutex_timedlock()` 等 50 ms | 等待期间提升到 110；等待者 `ETIMEDOUT` 后持有者回到 101 |

输出每个用例一行 `PRIO_INHERIT_TEST <case> PASS|FAIL ...`，最后一行
`PRIO_INHERIT_TEST PASS failures=0`。

## 实板结果

`ostest` 配置，Ai-M64L-32S-Kit：

```text
PRIO_INHERIT_TEST default PASS boosted=110/110 restored=101/101 handoff_us=88 mid_done_first=no/no
PRIO_INHERIT_TEST nxmutex PASS boosted=110/110 restored=101/101 handoff_us=59 mid_done_first=no/no
PRIO_INHERIT_TEST none PASS boosted=101/101 restored=101/101 handoff_us=179460 mid_done_first=yes/yes
PRIO_INHERIT_TEST nested-low-first PASS boosted=112/112 after_unlock=112/112,101/101
PRIO_INHERIT_TEST nested-high-first PASS boosted=112/112 after_unlock=110/110,101/101
PRIO_INHERIT_TEST timeout PASS boosted=110/110 wait=ETIMEDOUT(110) after_timeout=101/101 restored=101/101
PRIO_INHERIT_TEST PASS failures=0
```

有 PI 时持有者释放后 59～88 µs 等待者就拿到锁；没有 PI 时等待者要等中优先级
线程跑完，约 179 ms。

完整 `ostest` 退出码为 0，其中上游 `priority_inheritance` 输出
`highpri_thread-1: SUCCESS midpri_thread is still running!` 和
`PASSED Priority were correctly restored.`。

## 开销

| 配置 | `.text` | `.data` | `.bss` | 开机后堆 |
|---|---|---|---|---|
| `nsh-peripherals` | +4072 B | +336 B | +352 B | 已用 +416 B |

mutex 没有竞争时仍走原子快速路径：`nxmutex_wait()`、`nxmutex_post()` 只在
`PRIORITY_PROTECT` 下关闭快速路径。PI 的额外工作只发生在有竞争时：登记持有
者、提升持有者、释放时恢复优先级。

`nsh-peripherals` 打开前后 `mcu_dma_test` 均为 8/8 通过（DMA-007 覆盖通道
等待、耗尽和并发释放）；`mcu_timer_test` 均为 8/10，TIMER-003/004 的
`PWMIOC_START` 返回 `EINVAL` 与 PI 无关。
