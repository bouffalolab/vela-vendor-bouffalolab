# MCU 看门狗 (WDT) 外设测试

BL616/BL618 看门狗功能测试，作为 `mcu_peripheral_tests` 外设测试集的一个子模块，
编译为 NSH builtin 命令 `mcu_wdt_test`。

## 背景

测试矩阵要求覆盖以下两条看门狗用例：

| 用例 ID | 功能点 | 步骤 | 预期结果 | 判定标准 |
|---------|--------|------|----------|----------|
| **WDT-001** | 超时复位 | 启用 WDT、不喂狗、等待超时 | MCU 发生复位 | 复位源标志位为 WDT 复位 |
| **WDT-002** | 正常喂狗 | 启用 WDT、超时前周期性喂狗 | MCU 不发生复位 | 喂狗操作成功 |

## 设计原理

### 设备与驱动

- 设备节点：`/dev/watchdog0`
- 上层框架：`nuttx/drivers/timers/watchdog.c`（标准 NuttX watchdog upper-half）
- BL616 lower-half：`vendor/bouffalolab/chip/bl616/bl616_wdt_lowerhalf.c`
  （`WDG_MODE_RESET`，超时范围 1–65535 ms，32 kHz 时钟源）
- 用户态通过 ioctl 操作：`WDIOC_SETTIMEOUT` / `WDIOC_START` / `WDIOC_KEEPALIVE`
  / `WDIOC_STOP` / `WDIOC_GETSTATUS`

### 与内核 auto-monitor 的关系（关键）

`miot_test` 默认开启 `CONFIG_WATCHDOG_AUTOMONITOR`（默认 `BY_WDOG`，超时 60 s，
每 30 s 由内核软件定时器自动喂狗）。这意味着系统启动后看门狗已被内核接管并持续喂狗，
单纯"不喂狗"无法触发复位。

关键机制：`watchdog.c` 的 `WDIOC_START` 分支会先调用 `watchdog_automonitor_stop()`
——**用户态一旦 `WDIOC_START`，内核 auto-monitor 立即停手并取消其喂狗定时器，
测试程序完整接管看门狗**。因此 WDT-001 无需任何关中断 hack，标准
"SETTIMEOUT → START → 不喂狗"即可触发复位。

> 副作用：任一用例 `WDIOC_START` 后，本次启动的 auto-monitor 不再恢复（直到下次重启）。
> 故 WDT-002 结束时必须 `WDIOC_STOP`，否则退回 NSH 后约 1 个超时周期便会被复位。

### 复位源自检

通过标准 `boardctl(BOARDIOC_RESET_CAUSE, &cause)` 读取上次复位原因。板级
`board_reset_cause()`（`vendor/bouffalolab/boards/bl616evb/src/bl616_reset.c`）
把芯片的 `BL616_RST_HARDWARE_WATCHDOG` 映射为 `BOARDIOC_RESETCAUSE_SYS_RWDT`。

`bl616_systemreset.c` 的 `bl616_reset_reason_init()` 在启动早期把 HBN 复位原因寄存器
**预设为 HARDWARE_WATCHDOG**，仅当软件主动 `reset_reason_set()` 时才覆盖；硬件 WDT
复位没有机会改写，因此重启后必读到 `SYS_RWDT`。判定可靠。

### WDT-001：超时复位（报告上轮 + 触发本轮）

硬件看门狗复位是破坏性的——设备一定会重启，无法在单条命令内完成
"触发复位 → 确认复位源"闭环。因此 WDT-001 每次执行：

1. 先用 `boardctl` 读复位源，**报告上一轮结果（显示一次）**：
   - 上次是 WDT 复位 → `PASS: previous reset cause = WATCHDOG (SYS_RWDT)`
   - 否则 → `Previous reset cause = N (not WATCHDOG)`
2. 然后 `SETTIMEOUT → START → 不喂狗`，**触发本轮复位**，设备重启。

这样反复执行 `mcu_wdt_test -c 001` 会持续"确认上一轮 + 触发下一轮"，
无需中间手动 reboot。

`-s`（status-only）选项：只报告上次复位源、**不**触发复位，用于非破坏性确认
（host 自动化脚本的第二阶段即用此模式干净判定，避免设备被再次复位）。

### WDT-002：正常喂狗不复位

`SETTIMEOUT → START（接管 auto-monitor）→ 周期 KEEPALIVE → 跑完喂狗窗口未复位
→ WDIOC_STOP 收尾`。跑完整个喂狗时长设备不复位即证明喂狗有效；末尾 `WDIOC_STOP`
停掉看门狗，防止退回 NSH 后被意外复位。

## 文件说明

| 文件 | 说明 |
|------|------|
| `wdt_case_main.c` | 测试主程序，实现 WDT-001/002，编译为 `mcu_wdt_test` |
| `Kconfig` / `Make.defs` / `Makefile` | 子模块构建文件 |
| `wdt_serial_test.py` | host 端串口验证脚本：驱动 NSH 自动跑测试、判定结果、保存原始日志 |

## 构建配置

- 顶层 `mcu_peripheral_tests/Kconfig` 显式 `source` 本目录 `Kconfig`
- 在目标 defconfig 启用：`CONFIG_BL_MCU_PERIPHERAL_TESTS_WDT=y`
  （已加入 `boards/bl616evb/configs/miot_test/defconfig`）
- 依赖（`miot_test` 已具备）：`CONFIG_WATCHDOG=y`、`CONFIG_BOARDCTL_RESET_CAUSE=y`

```bash
./build.sh vendor/bouffalolab/boards/bl616evb/configs/miot_test -j14
```

## 测试步骤

### NSH 手动测试

```bash
nsh> mcu_wdt_test -c 002       # 喂狗不复位（安全）
nsh> mcu_wdt_test -c 001       # 报告上轮结果 + 触发本轮复位（设备会重启）
nsh> mcu_wdt_test -c 001 -s    # 只确认上次复位源是否为 WDT，不复位（非破坏）
nsh> mcu_wdt_test -c all       # 先 002 后 001（最终会复位）
nsh> mcu_wdt_test -h           # 帮助
```

可选参数：`-t <ms>` 超时（默认 3000）、`-p <ms>` 喂狗时长（默认 9000）、
`-i <ms>` 喂狗间隔（默认 1000）、`-d <dev>` 设备路径、`-v` verbose。

WDT-001 完整验证流程：

1. `mcu_wdt_test -c 001` → 约 3 s 后设备复位重启（串口可见重启日志与 `Reset cause: HW WDG`）
2. 重启进入 NSH 后 `mcu_wdt_test -c 001 -s` → `PASS: previous reset cause = WATCHDOG`

### host 端自动化测试

```bash
python3 wdt_serial_test.py --port /dev/ttyUSB2 --baud 2000000 \
    --case all --log /tmp/wdt.log
```

脚本使用单一串口 fd 自动驱动 NSH：打开后立即恢复运行态
（DTR=1、RTS=0），跑 WDT-002 → 跑 WDT-001 阶段一（`-c 001` 触发复位）
→ 等重启 → 阶段二（`-c 001 -s` 干净确认）→ 输出判定；退出前再次恢复
运行态。`--log` 保存全部原始串口数据（含启动日志）与脚本步骤标记，便于追溯。

> 提示：`--log` 默认在当前目录按时间戳生成 `.log`，建议指定到 `/tmp` 等位置，
> 避免在 git 仓库留下日志文件。

## 测试结果

配置 `miot_test`，实板 BL616（`/dev/ttyACM1`，2000000 baud）实测：

- **编译/烧录**：成功，`mcu_wdt_test` 注册进固件，无相关编译/链接错误。
- **WDT-002**：`PASS: fed 9 times over 9011ms, no reset; watchdog stopped`
  —— 喂狗 9 次、9 s 内设备不复位。✅
- **WDT-001 阶段一**（复位源 = POWER OFF）：
  `Previous reset cause = 1 (not WATCHDOG)` → `Arming watchdog (3000ms)...`
  → 设备约 3 s 后真实复位重启。✅
- **WDT-001 阶段二**（`-c 001 -s`）：
  `PASS: previous reset cause = WATCHDOG (SYS_RWDT)` + `status-only: watchdog not armed`
  —— 复位源确认为看门狗，且不触发新复位。✅
- **"每次触发"验证**（设备处于 WDT 复位态时跑 `-c 001`）：
  先 `PASS: previous reset cause = WATCHDOG`（报告上轮，显示一次），再
  `Arming watchdog ...` 触发本轮复位 —— 不再卡在只显示上次结果。✅

两条用例的全部代码路径均已在实板印证。
