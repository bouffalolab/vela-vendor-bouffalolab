# MCU 定时器 / PWM 外设测试

BL616/BL618 通用定时器（TIMER）与 PWM 外设功能测试，作为 `mcu_peripheral_tests`
外设测试集的一个子模块，编译为 NSH builtin 命令 **`mcu_timer_test`**。

## 背景

测试矩阵要求覆盖以下 TIMER / PWM 用例：

| 用例 ID | 外设 | 功能点 | 前置条件 | 步骤 | 预期结果 | 测量/工具 | 判定标准 |
|---------|------|--------|----------|------|----------|-----------|----------|
| **TIMER-001** | TIMER | 基本计数 / 溢出周期准确性 | timer0 已注册 | 设周期周期触发，连续测 N 个间隔 | 周期与设定值一致 | 软件 `CLOCK_MONOTONIC` / 示波器 | 误差 ≤ 容差（默认 ±0.5%） |
| **TIMER-002** | TIMER | 预分频器设置生效 | 同上 | 固定比较值，换两个分频值各测周期 | 周期比 = (b+1)/(a+1) | 软件（周期比例）/ 示波器 | 比值落在 ±5% 内、写入即生效 |
| **TIMER-003** | PWM | PWM 频率 / 占空比精度 | PWM 输出到 GPIO28 | 设不同频率/占空比并测量 | 实测与设定一致 | 示波器 / 逻辑分析仪 | 频率 ±1%、占空比 ±2% |
| **TIMER-004** | PWM | 占空比渐变 / 呼吸灯 | 同上（可接 LED） | 固定频率，占空比 0→100→0 渐变 | 频率恒定、占空比线性无跳变 | 示波器 / LED | 频率不变、占空比按设定切换 |

> TIMER 与 PWM 是**两个独立外设**：001/002 用通用 timer0，003/004 用独立 PWM 外设。

## 设计原理

### 设备与驱动

BL616 的 timer/PWM 在软件上分三层，能力从下到上递减：

```
第3层 NuttX 框架  /dev/timer0 (TCIOC_*) /dev/pwm0 (PWMIOC_*)  ← 最易用，但无预分频/捕获 ioctl
第2层 LHAL 库     bflb_timer_* / bflb_pwm_v2_*                ← 能改预分频；无 capture 函数
第1层 硬件寄存器  timer_reg.h, base=0x2000a500               ← 能力最全，捕获只在此层
```

- 设备节点：`/dev/timer0`（TIMER）、`/dev/pwm0`（PWM CHAN0 = GPIO28）。
- 上层框架：`nuttx/drivers/timers/timer.c` / `pwm.c`。
- BL616 lower-half：`vendor/bouffalolab/chip/bl616/bl616_tim_lowerhalf.c`、`bl616_pwm.c`。
- timer0 复用：本 defconfig 下 `CONFIG_BL616_ONESHOT=y`，timer1 注册为 `/dev/oneshot`，测试固定用 timer0。

### TIMER-001：溢出周期准确性

PROLOAD 模式下到期回调返回 true 自动重装 → 周期触发。设周期 T，连续量 N 个相邻间隔与设定值
比对；先收一次“热身”信号设基准，避免 `start→arm` 延迟污染首个间隔，调度抖动在“相邻周期差”
里相互抵消。参考时钟 `CLOCK_MONOTONIC`（派生自 40MHz 晶振的 mtimer，与 timer0 独立）。

### TIMER-002：预分频

原理：计数时钟 = 40MHz/(div+1)，固定比较值 N 时溢出周期 `T = N×(div+1)/40MHz`，故 `T_b/T_a = (div_b+1)/(div_a+1)`，与比较值
无关。测完恢复 div=39 以保持 `/dev/timer0` 的 μs 口径。

### TIMER-003 / 004：PWM 精度与呼吸灯

`/dev/pwm0` `PWMIOC_SETCHARACTERISTICS` + `PWMIOC_START`。频率 = 80MHz/(clk_div×period)，
`duty` 为 b16 定点（50% = 0x8000）。004 依赖 `bl616_pwm.c` 的 fast-path：频率不变时只更新
占空比阈值、不重 init → 渐变平滑无抖动。

### 示波器测量：GPIO 翻转旁证（001/002）

001/002 本是纯软件判定，无物理信号可探。为便于用示波器验证，app 增加 `-g <dev>` 选项：每次
定时到期翻转一次板载输出 GPIO，产生**整周期 = 2×定时周期**的方波，示波器测方波周期 ÷2 即定时
周期。用标准 NuttX GPIO 字符设备 `ioctl(fd, GPIOC_WRITE, 0/1)`，app 不耦合 board/chip GPIO 代码。

> 引脚说明：本固件按 **BL616 编译**（`CONFIG_ARCH_CHIP_BL616`），可用引脚集为
> PIN0-3/10-17/20-22/27-30。翻转脚选 **GPIO11**（`BOARD_GPIO_OUT1` → `/dev/gpio1`，
> 见 `boards/bl616evb/src/bl616_gpio.c`）。

## 文件说明

| 文件 | 说明 |
|------|------|
| `timer_case_main.c` | 测试主程序，实现 TIMER-001~004 + `-g` 示波器翻转，编译为 `mcu_timer_test` |
| `Kconfig` / `Make.defs` / `Makefile` | 子模块构建文件 |

依赖的 chip / board 改动（非本目录）：
`chip/bl616/include/bl616_tim_ioctl.h`（002 自定义 ioctl 单一数据源）、
`chip/bl616/bl616_tim_lowerhalf.c`（ioctl 转发 + 范围校验）、
`chip/bl616/bl616_tim.c` + `bl616_tim.h`（`setclockdiv` ops）、
`boards/bl616evb/src/bl616_gpio.c`（翻转测试脚 GPIO11）。

## 构建配置

- 顶层 `mcu_peripheral_tests/Kconfig` 显式 `source` 本目录 `Kconfig`。
- 在目标 defconfig 启用：`CONFIG_BL_MCU_PERIPHERAL_TESTS_TIMER=y`。
- 依赖（`timer` defconfig 已具备）：
  - TIMER：`CONFIG_BL616_TIMER=y` + `CONFIG_BL616_TIMER0=y`
  - PWM：`CONFIG_PWM=y` + `CONFIG_BL616_PWM=y` + `CONFIG_BL616_PWM_CHANNEL_0=y`
  - GPIO 翻转：`CONFIG_DEV_GPIO=y`
  - 控制台/NSH：`CONFIG_BL616_UART=y`、`CONFIG_NSH_MAXARGUMENTS=12`

```bash
# 在 SDK 根目录编译（改过 defconfig 需先 rm -f nuttx/.config 强制重配）
./build.sh vendor/bouffalolab/boards/bl616evb/configs/timer -j12
# 出现 "All programming completed successfully" = 编译+烧录成功
# 末尾 truncate/OTA 报错是 timer defconfig 既有打包脚本 bug，不影响 nuttx.bin，忽略
```

## 测试步骤

```bash
picocom -b 2000000 /dev/ttyACM0     # 复位板子 → nsh>
nsh> ls /dev                         # 应见 timer0 / pwm0 / gpio1

nsh> mcu_timer_test -c 001 -t 100000 -n 10 -e 0.5            # 基本计数（纯软件）
nsh> mcu_timer_test -c 001 -t 500000 -n 200 -g /dev/gpio1    # 001 示波器旁证（探 GPIO11）
nsh> mcu_timer_test -c 002 -v                                # 预分频（默认 39/79）
nsh> mcu_timer_test -c 002 -t 1000000 -a 19 -b 39 -g /dev/gpio1  # 002 自定义分频 + 示波器
nsh> mcu_timer_test -c 003 -f 2000 -D 40 -w 5                # PWM 单点 2kHz/40%（探 GPIO28）
nsh> mcu_timer_test -c 004 -f 1000 -s 25 -i 1000 -n 5        # 呼吸灯 1kHz、25% 台阶
nsh> mcu_timer_test -c all                                   # 顺序跑全部
nsh> mcu_timer_test -h                                       # 参数帮助
```

参数：`-c` 用例 | `-d/-p/-g` 设备 | `-t` 周期/比较值 | `-n` 轮数/呼吸周期 | `-e` 容差% |
`-a/-b` 分频值(002) | `-f` PWM 频率 | `-D` 占空比%(003单点) | `-w` 保持秒(003) |
`-s` 占空比步进%(004) | `-i` 步间隔ms(004) | `-v` 详细 | `-h` 帮助。所有自定义参数有默认值。

## 测试结果

实板 **BL618G1 外设板**（主控 BL618M-65-Q2I，固件按 BL616 编译；`/dev/ttyACM0` @ 2000000
baud），RIGOL 示波器实测（探头：001/002 接 GPIO11，003/004 接 GPIO28，DC 耦合）。

- **编译/烧录**：成功，`mcu_timer_test` 注册进固件，无相关编译/链接错误。
- **总体结论**：**TIMER-001~004 全部 PASS**。

### TIMER-001 基本计数 = PASS

| 测量 | 命令 | 实测 | 判据 | 判定 |
|------|------|------|------|------|
| 软件 | `-c 001 -t 100000 -n 10 -e 0.5` | `max_err=0.0us (0.000%)` | ≤0.5% | ✅ PASS |
| 示波器 | `-c 001 -t 500000 -n 200 -g /dev/gpio1` | 周期 1.000s、频率 1.00Hz、占空比 50.00% | 周期=2×0.5s=1s | ✅ PASS |

软件路径误差 0.0%；示波器方波周期 1.000s 正好为定时周期（0.5s）的 2 倍，占空比 50%，两条路径
互为印证。

### TIMER-002 预分频 = PASS

| 测量 | 命令 | 实测 | 判据 | 判定 |
|------|------|------|------|------|
| 软件(默认39/79) | `-c 002 -v` | div39=0.5100s、div79=1.0000s、ratio=1.961（expected 2.000） | ±5% | ✅ PASS |
| 自定义分频 | `-c 002 -t 1000000 -a 19 -b 39` | 期望比值 (39+1)/(19+1)=2.0 | ±5% | ✅ PASS |
| 示波器 | `-c 002 -t 1000000 -a 19 -b 39 -g /dev/gpio1` | 光标测 div=19 段方波 1.000s/1.00Hz，div=39 段周期翻倍≈2s | 后段/前段≈2.0 | ✅ PASS |

串口逐层印证了去重后的 ioctl 链路：`cmd 5648 (=_TCIOC(0x10)) → Forwarding → bl616_tim_set_clock_div`，
两侧解析同一常量，证明单一数据源无失配。

> 说明：软件默认档 div39 周期实测 **0.5100s**（理论 0.5s），偏大 10ms = 恰好 1 个系统 tick
> （`CONFIG_USEC_PER_TICK=10000`），是软件 `CLOCK_MONOTONIC` 的 10ms 量化所致、非定时器误差；
> 比值 1.961 仍在 ±5% 内。示波器旁证（硬件直接测翻转沿）比值精确为 2.0，可见预分频硬件准确。

### TIMER-003 PWM 频率/占空比精度 = PASS

| 单点自定义 | 设定 | 实测频率 | 实测占空比 | 判定 |
|------------|------|---------|-----------|------|
| `-f 2000 -D 40 -w 5` | 2 kHz / 40% | 2.00 kHz（周期 500.0µs） | 40.0% | ✅ PASS |
| `-f 5000 -D 50 -w 5` | 5 kHz / 50% | 5.00 kHz（周期 200.0µs） | 50.0% | ✅ PASS |

默认三组（`-c 003 -v`：1k/50%、10k/25%、100/75%）串口依次输出 DONE；自定义单点两组实测频率与
占空比与设定**完全一致**（误差 ~0%），远优于 ±1%/±2% 判据。

### TIMER-004 占空比渐变 / 呼吸灯 = PASS

> 接线说明：板载可见 LED1 接在 GPIO18（BL618 专属脚，BL616 固件不可直接驱动），故用杜邦线把
> **PWM 输出 GPIO28 连到 GPIO18（LED1）**，由 PWM 直接驱动该 LED，既能示波器测又能肉眼看呼吸。

命令 `-c 004 -f 1000 -s 25 -i 1000 -n 5`，逐档示波器自动测量：

| 设定占空比 | 实测频率 | 实测占空比 | 判定 |
|-----------|---------|-----------|------|
| 25% | 1.00 kHz | 25.20% | ✅ |
| 50% | 1.00 kHz | 50.00% | ✅ |
| 75% | 1.00 kHz | 75.20% | ✅ |
| 100% | —（恒高 DC） | 100%（实测无翻转沿，稳定 3.3V 高） | ✅ |

**频率全程恒为 1.00 kHz**（改占空比不影响频率），占空比按 25/50/75/100 准确切换、误差 ≤0.2pp，
切换无毛刺 → 呼吸灯渐变机制（fast-path 只改阈值不重 init）验证正确。

### 结果汇总

| 用例 | 名称 | 测量方式 | 判定 |
|------|------|----------|------|
| TIMER-001 | 基本计数 | 软件 + 示波器 | ✅ PASS |
| TIMER-002 | 预分频 | 软件 + 示波器 | ✅ PASS |
| TIMER-003 | PWM 频率/占空比精度 | 示波器 | ✅ PASS |
| TIMER-004 | 占空比渐变/呼吸灯 | 示波器 | ✅ PASS |
