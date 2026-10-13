# MCU GPIO 外设测试

实现入口为 `gpio_case_main.c`，编译为独立的 `libapps_mcu_gpio_test.a`，注册 NSH 命令
`mcu_gpio_test`。本 README 集中说明该 main 中的全部用例。

## 背景

GPIO 测试既要覆盖正常电平、上下拉和边沿中断，也要验证错误 ioctl 不会污染设备状态。
默认回环使用 `/dev/gpio23`（输出）和 `/dev/gpio20`（输入），需用跳线连接两脚。这两脚不与
`nsh-peripherals` 的其他外设冲突，该测试编入 `nsh-peripherals` 和 `ostest`。

## 方案与用例

| 用例 | 覆盖内容 | 主要判定 |
|---|---|---|
| GPIO-001 | 推挽输出/输入回环 | 写入值与读回值一致 |
| GPIO-002 | 开漏低电平与释放 | 两种状态均完成 |
| GPIO-003 | 输入上拉/下拉 | 稳定采样符合配置 |
| GPIO-004 | 上升沿中断 | 收到指定次数信号 |
| GPIO-005 | 下降沿中断 | 收到指定次数信号 |
| GPIO-007 | 输出电气保持 | 输出保持，电压/电流外测 |
| GPIO-008 | 快速翻转波形 | 示波器检查边沿、过冲、振铃 |
| GPIO-010 | 启动状态记录 | 输出 pintype 和 value |
| GPIO-edge | 异常请求与恢复 | 错误码正确且后续读写恢复 |
| board | 板载 LED，给出按键时含按键 | LED 操作完成；给出按键时空闲值为 1 |
| pair | 依次执行 001、003、004、005、002 | 全部 PASS |
| all | 010、board、pair；跳过仪器用例 | 全部执行项 PASS |

## 命令与流程

```text
mcu_gpio_test -c 001 --out /dev/gpio23 --in /dev/gpio20 -n 20 -p 100000
mcu_gpio_test -c 002 --out /dev/gpio23 --in /dev/gpio20
mcu_gpio_test -c 003 --in /dev/gpio20
mcu_gpio_test -c 004 --out /dev/gpio23 --in /dev/gpio20 -n 20
mcu_gpio_test -c 005 --out /dev/gpio23 --in /dev/gpio20 -n 20
mcu_gpio_test -c 007 --out /dev/gpio23 -H 5000
mcu_gpio_test -c 008 --out /dev/gpio23 -p 100000 -n 20
mcu_gpio_test -c 010
mcu_gpio_test -c edge --out /dev/gpio23 -n 64 -r
```

每个用例依次打开设备、设置 pintype、执行读写或信号等待并关闭设备；`-r` 恢复原 pintype。
GPIO-edge 先验证未知 ioctl=`ENOTTY`、非法 pintype=`EINVAL` 且状态不变、输入态写入=`EACCES`，
再做 output write/read 与 input pulldown 快速切换。

### board

Ai-M64P-32S-Kit 只有 EN、BOOT 两个按键，没有用户按键；LED 默认用高电平点亮的板载蓝灯
`/dev/gpio16`。

1. 打开 LED；用 `--key0`、`--key1` 给出按键时（两者须同时给出）一并打开。
2. 将 LED 配成推挽输出，执行 3 次 active-high blink。
3. 没有按键时跳过以下两步，直接输出 PASS。
4. 将两个按键配置为输入，连续稳定采样；两者空闲值必须为 1。
5. 默认跳过人工按键中断；加 `--interactive` 时依次等待按下/释放并验证下降沿、上升沿。

### GPIO-001

打开 OUT/IN，分别设置推挽输出/输入。循环 `count` 次：写入交替 0/1，等待 `period_us/2`，读取 IN；
任何读回不等于期望值立即打印 sample/expected/actual 并失败。完成后关闭两个 fd。

完成判据：20 次交替电平全部匹配，打印 `PASS input followed push-pull output` 并返回 NSH。

### GPIO-002

OUT 设置开漏、IN 设置上拉；写 0 后等待 5ms，读值必须为 0；写 1（释放）后再等 5ms，读值必须为 1。

完成判据：drive-low=0、release-high=1，打印 `PASS open-drain low/release checks completed`。

### GPIO-003

先将 IN 设上拉并稳定采样为 1，再设下拉并稳定采样为 0；随后 OUT 置输出高、IN 下拉并确认外部高覆盖下拉，
再 OUT 置低、IN 上拉并确认外部低覆盖上拉。

完成判据：内部上拉=1、下拉=0，外部高/低均能覆盖相反内部拉电阻，最终打印 PASS。

### GPIO-004 / GPIO-005

屏蔽测试信号并注册 GPIOC_REGISTER；先设置空闲电平，再等待 10ms 让 IRQ 生效。每轮制造一个目标边沿，
用 `sigtimedwait` 等待信号，超时即失败；004 目标为上升沿，005 目标为下降沿，完成后注销并恢复信号掩码。

完成判据：每个目标边沿都恰有一次可接收信号；默认各累计 20 次并打印 PASS。

### GPIO-007 / GPIO-008 / GPIO-010

007 将 OUT 置高保持 `hold_ms`，只提供稳定测量窗口；外部仪器记录电压/电流。008 每轮写高、等待半周期、
写低、再等待半周期，示波器记录边沿和振铃。010 依次打开已配置的 GPIO（按键未给出时跳过），只读 pintype/value 并打印 RECORD，
不改变硬件状态。

完成判据：007/008 的软件流程无 ioctl 失败，且外部测量已记录；010 的每个已配置 GPIO 均打印 RECORD。

### pair / all

`pair` 严格按 001→003→004→005→002 执行；`all` 先执行 010，再执行 board 和 pair，明确跳过 007/008，
因为这两个用例需要外部仪器。

## 实测数据

- 既有回归：GPIO-001/002/003 PASS；GPIO-004、005 各收到 20 次中断并 PASS。
- 2026-08-28，Ai-M64P-32S-Kit，`/dev/ttyUSB2` @ 2 Mbps：GPIO-edge 64 次恢复循环 PASS，返回 `nsh>`。
- GPIO-007/008 的电压、电流和波形质量需使用外部仪器记录。

## 固件运行关键证据

```text
nsh> mcu_gpio_test -c 001 --out /dev/gpio12 --in /dev/gpio20 -A 0
[GPIO-001] START out=/dev/gpio12 in=/dev/gpio20 period_us=100000 count=20
[GPIO-001] PASS input followed push-pull output
nsh> mcu_gpio_test -c 002 --out /dev/gpio12 --in /dev/gpio20
[GPIO-002] PASS open-drain low/release checks completed
nsh> mcu_gpio_test -c 003 --out /dev/gpio12 --in /dev/gpio20
[GPIO-003] PASS input pull-up/down checks completed
nsh> mcu_gpio_test -c 004 --out /dev/gpio12 --in /dev/gpio20
[GPIO-004] START out=/dev/gpio12 in=/dev/gpio20 irq=GPIO_INTERRUPT_RISING_PIN
[GPIO-004] PASS 20 interrupts received
nsh> mcu_gpio_test -c 005 --out /dev/gpio12 --in /dev/gpio20
[GPIO-005] START out=/dev/gpio12 in=/dev/gpio20 irq=GPIO_INTERRUPT_FALLING_PIN
[GPIO-005] PASS 20 interrupts received
nsh> mcu_gpio_test -c edge --out /dev/gpio12 -n 64 -r
[GPIO-edge] PASS rejected invalid operations and recovered for 64 cycles
nsh>
```

以上 001~005 和 edge 已在 Ai-M64P-32S-Kit `/dev/ttyUSB2` @ 2Mbps 闭环（当时回环为
GPIO12→GPIO20）。

2026-10-13 默认回环改为 GPIO23→GPIO20 后，在 `nsh-peripherals` 上短接两脚复测：

```text
nsh> mcu_gpio_test -c all -A 0
KEY0=none KEY1=none LED=/dev/gpio16 OUT=/dev/gpio23 IN=/dev/gpio20
[GPIO-010] RECORD /dev/gpio16 pintype=GPIO_OUTPUT_PIN value=0
[GPIO-010] RECORD /dev/gpio23 pintype=GPIO_OUTPUT_PIN value=0
[GPIO-010] RECORD /dev/gpio20 pintype=GPIO_INPUT_PIN value=0
[GPIO-board] SKIP key checks; use --key0 and --key1
[GPIO-board] PASS board GPIO checks completed
[GPIO-001] PASS input followed push-pull output
[GPIO-003] PASS input pull-up/down checks completed
[GPIO-004] PASS 20 interrupts received
[GPIO-005] PASS 20 interrupts received
[GPIO-002] PASS open-drain low/release checks completed
nsh> mcu_gpio_test -c edge -n 64 -r -A 0
[GPIO-edge] PASS rejected invalid operations and recovered for 64 cycles
```

逻辑分析仪（1 MHz 采样）确认 board 用例中 GPIO16 输出 3 个高电平脉冲，每个约 100 ms、
周期约 200 ms；`-c 008 -p 20000 -n 3` 在所选 GPIO 上输出 3 个脉冲。007 的电压、电流
和 008 的边沿质量仍需示波器等仪器记录。
