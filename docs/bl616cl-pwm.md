# BL616CL PWM 适配与验证

本文说明 BL616CL PWM 与 OpenVela PWM 子系统的最大能力交集、首版适配边界、
Kconfig 裁剪、资源所有权、软件测试和实物波形验收方法。本文只记录已经由源码、构建
或运行证据支持的结论；没有仪器的波形项目明确保留为待验证项。

## 背景

BL616CL 有一个 PWM controller 和四个硬件 channel。controller 共享 clock、divider、
period 和计数模式；每个 channel 有独立 threshold、正负输出、polarity、停止/刹车状态
和 deadtime。同步 LHAL 已提供初始化、启停、channel 配置、update gate、brake、ADC
trigger 等原语，但此前没有接入 OpenVela PWM lower-half，也没有 `/dev/pwm0` 和板级
pin owner。

OpenVela 通用 PWM upper-half 提供 SET/GET characteristics、START、STOP 和多 fd
生命周期。其 ABI 可选 multichannel、pulse count、fixed-point frequency 和 deadtime；
这些选项会改变 `pwm_info_s` 或 lower `start` 签名，不能在未实现时形成可见组合。

本适配不修改 CI 同步的 `vendor/bouffalolab/drivers/`。chip adapter 消费现有 LHAL，
board 固定选择 PWM0 CH3 正向输出 GPIO22，并注册 `/dev/pwm0`。

## 最大能力交集

| 能力 | 当前处理 | 依据和限制 |
|---|---|---|
| 单 controller | 纳入首版 | BL616CL 只有 `pwm_v2_0`；base、gate、reset、IRQ 与 TIMER 独立 |
| CH3 正向输出 | 纳入首版 | 同芯片 board 映射和 PWM pinmux 均支持 GPIO22 |
| continuous PWM | 纳入首版 | 对应非 pulse-count OpenVela lower ABI |
| 整数 frequency | 纳入首版 | 读取实际 PBCLK，在 16-bit divider/period 范围求最小误差 |
| b16 duty | 纳入首版 | 0..65535/65536 映射到 threshold；最大值不声明为 100% |
| `cpol`/`dcpol` | 纳入首版 | 正向 polarity 与 active/inactive stop-state 可无歧义映射 |
| live update | 纳入首版 | 同 divider 使用 update gate；跨 divider 允许 stop/reinit |
| 多 fd | 纳入首版 | 首次 open setup，最后 close shutdown，由 upper mutex 串行化 |
| multichannel | P2 | 硬件支持四 channel，但缺少完整 pin、owner、并发和多探针证据 |
| pulse count/IRQ | P2 | LHAL 有 repetition/IRQ 原语，尚无完成回调和取消合同 |
| complementary/deadtime | P2 | 硬件支持负向输出和 8-bit deadtime，缺 pin 与波形闭环 |
| brake/center-aligned | P2 | LHAL 有原语，尚未映射公共控制和安全状态 |
| ADC trigger | P2 | 硬件支持，需与后续 ADC owner 联合设计 |
| fixed-point frequency | P2 | OpenVela ABI 可选，首版只实现整数 frequency |
| PWM DMA | P3 排除 | BL616CL DMA request 表没有 PWM request |

完整枚举不等于一次性启用全部能力。P2 项保留为芯片共同能力和后续恢复入口，不能写成
“BL616CL 不支持”；首版只声明可裁剪且有明确软件与波形判据的单通道子集。

## 调用链与 archive 归属

```text
board_late_initialize()
  -> bl616cl_board_initialize()
  -> ai_m64l_kit_pwm_initialize()
  -> bl616cl_pwm_initialize(3, 22)
  -> pwm_register("/dev/pwm0", lower)

open / SET / START / STOP / close
  -> OpenVela pwm upper-half
  -> BL616CL lower setup/start/stop/shutdown
  -> PWM0 clock + GPIO22 pinmux
  -> bflb_pwm_v2_*()
```

- `chips/bl616cl/bl616cl_pwm.c`：OpenVela lower、频率求解、错误转换和 test diagnostic，
  编入 `libarch.a`；
- `boards/bl616cl/ai-m64l-32s-kit/src/ai_m64l_kit_pwm.c`：固定 CH3+/GPIO22 和
  `/dev/pwm0` 注册，编入 `libboard.a`；
- `cmake/bl616cl_lhal.cmake`：按 PWM Kconfig 选择现有 LHAL PWM/clock 依赖，
  编入 `libbl_lhal.a`；
- `apps/mcu_peripheral_tests/pwm/`：单一测试 main，独立生成测试 archive；
- `drivers/`：保持只读，chip adapter 不放入 `libbl_std.a`。

## Kconfig 与 GPIO22 所有权

| 选项 | 作用 | 关闭效果 |
|---|---|---|
| `BL616CL_PWM` | chip lower 总开关，并启用通用 PWM upper-half | 无 lower 和 PWM LHAL 对象 |
| `AI_M64L_KIT_PWM` | board CH3+/GPIO22 和 `/dev/pwm0` | 不接管 GPIO22，不注册节点 |
| `BL616CL_PWM_TEST` | timeout fault 与诊断 API | 产品无 test symbol |
| `BL_MCU_PERIPHERAL_TESTS_PWM` | `mcu_pwm_test` 单一测试 main | 无测试 archive、命令和字符串 |

`BL616CL_PWM` 依赖关闭 `PWM_PULSECOUNT`、`PWM_MULTICHAN`、
`PWM_FREQUENCY_FIXED` 和 `PWM_DEADTIME`。board 选项使用 `depends on BL616CL_PWM`，
不能通过 `select` 绕过 chip 选项依赖。

启用 board PWM 后：

1. GPIO 列表无条件移除 `/dev/gpio22`；
2. I2C0/I2C1 的 SCL 或 SDA 选择 GPIO22 时编译失败；
3. SPI0 的 CLK、MISO、MOSI、CS0 或 CS1 选择 GPIO22 时编译失败；
4. `/dev/pwm1` 不存在，不用运行时注册顺序解决双 owner。

## 频率、duty 与 polarity 合同

lower 读取实际 PBCLK。请求 frequency 为 0、源时钟为 0，或请求高于 `PBCLK/2` 时拒绝。
divider 遍历 1..65535；每个 divider 只比较理想 period 附近且落在 2..65535 的候选，
用 64-bit 有理误差比较选择最优组合。配置后必须读取实际 divider/period 并据此计算
actual frequency，不能用请求值或仅缓存配置冒充寄存器证据。

`duty` 是 `ub16_t`：

- 0 映射 `(threshold_low, threshold_high)=(0,0)`；
- 25%、50%、75% 按 period 量化；
- 最大 `65535/65536` 映射 `threshold_high=period-1`，不宣称恒定 100% 高电平。

`cpol=LOW/HIGH` 分别映射正向通道 active-low/active-high。硬件 stop-state 使用
active/inactive 语义，因此 `dcpol` 的转换真值为：

```text
stop_active = (cpol == HIGH && dcpol == HIGH) ||
              (cpol == LOW  && dcpol == LOW)
```

`CPOL_NDEF` 和 `DCPOL_NDEF` 均拒绝。软件测试回读 CONFIG1 polarity/stop-state bit；
STOP 和 close 后还需由仪器确认 GPIO22 实际电平。

## timeout 与 upper-half 限制

LHAL `init/start/stop/deinit` 返回 `void`，等待硬件状态超过 100ms 时静默返回。lower
必须在调用后读取 `PWM_STS_STOP`，把失败转换为 `-ETIMEDOUT`，并强制关闭 CH3+、
退出 PWM pinmux、gate clock 和清理内部 started 状态。test-only fault 只改变调用后的
状态判断，不写硬件寄存器、不导出产品 ioctl，也不绕过输入校验或清理。

通用 upper-half 有两项必须显式保留的限制：

- 停止态 SET 先保存参数并返回 OK，真正 lower 校验发生在后续 START；运行态 SET 会先
  覆盖保存参数，再返回 lower 错误，因此失败后 GET 可能是无效请求值；
- STOP 不论 lower 返回值都会清 upper `started`，close 也忽略 shutdown 返回值；测试只能
  断言 lower 强制清理和内部诊断，不能虚构 close errno 或 upper 状态保持；
- `pwm_register()` 注册失败时通用 upper allocation 未释放。本项只保证静态 lower 在
  注册前不占 pin/clock，不在运行时用重复注册制造泄漏。

ST034 补充修复了 active close 生命周期：最后一个 fd close 调用 lower shutdown 后，
upper 同步清除 `started`。否则 reopen 后 `PWMIOC_START` 会因旧状态跳过 lower start，
向应用返回成功但硬件不输出。

## 构建与裁剪门禁

专项配置为 `nsh-pwm` 和 `nsh`；基线关闭态使用 `nsh`。在 SDK 根目录执行：

```bash
python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/<config>
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/<config> -j14
```

验收必须同时回读 `.config`、Ninja source、archive、map、ELF symbol 和 NSH `help`：

| 配置 | 预期 lower/LHAL/board | 预期 app/hook | 当前结果 |
|---|---|---|---|
| `nsh` | 无 | 无 | clean build 通过，1224/1224 |
| `nsh-pwm` | 有 | 无 | clean build 通过，1229/1229 |
| `nsh` | 有 | 有 | clean build 通过，1231/1231；栈 6144 |

defconfig 通过 Kconfig 工具和 `savedefconfig` 生成，不手工追加选项。

## 软件与实物流程

测试 app 使用一个 `mcu_pwm_test` main，以 `-c 001..008` 选择 case。逐 case 的命令、
内部动作、errno、寄存器和资源清理判据见
`apps/mcu_peripheral_tests/pwm/README.md`。软件阶段还要回归现役 GPIO、TIMER0、
TIMER1、oneshot、WDT 和 RTC；PWM 配置中的 GPIO 回归排除已移交的 GPIO22。

G4 波形在 GPIO22 验证：

1. 100Hz/75%、1kHz/25%、1kHz/50%、1kHz/75%、10kHz/25%；
2. frequency 误差不超过 1%，duty 误差不超过 2 个百分点；
3. 四组 cpol/dcpol 的运行 polarity、STOP 电平和 close 电平；
4. 同 divider duty update 不出现超过一个 PWM 周期的异常脉冲；
5. 跨 divider update 记录停顿，但本轮不声明无毛刺。

当前没有可识别的逻辑分析仪或示波器。USB2 只承载控制台，板载绿灯只可作 smoke；
两者都不能证明 frequency、duty、polarity、停止电平或动态更新波形。软件闭环通过后
patch 可以进入 vendor 开发 `trunk`，但在补齐仪器数据前本能力保持 G4 waiting。

## 软件实测数据

测试固件为 `nsh`，通过 `/dev/ttyUSB2`、2,000,000 baud 启动，USB ID 为
`1a86:7523`。启动后出现 `NuttShell (NSH)` 和 `nsh>`；`help` 包含
`mcu_pwm_test`，`ls /dev` 包含 `/dev/pwm0`，且没有 `/dev/pwm1`、`/dev/gpio22`。

执行 `mcu_pwm_test -c all -w 0 -v` 的关键结果如下：

| Case | 关键结果 |
|---|---|
| PWM-001 | `setup=1`、`shutdown=1`；首 fd close 后输出仍 active；最终资源清理 |
| PWM-002 | START-before-SET、零频率、超范围频率和 4 类 polarity 均拒绝；errno 22/34；运行态非法 SET 不改变硬件 |
| PWM-003 | 100 Hz、1 kHz、10 kHz 均通过；PBCLK 160 MHz；实际频率分别 100/1000/10000 Hz |
| PWM-004 | duty 0、25%、50%、75%、65535/65536 全通过；最大 duty 为 `period-1` |
| PWM-005 | CPOL/DCPOL 四组合全部通过；CONFIG1 polarity/stop-state 回读匹配 |
| PWM-006 | 同 divider 更新保持 4/40000；跨 divider 为 25/64000 和 1/16000；重复 START/STOP 幂等 |
| PWM-007 | INIT、START、STOP timeout 均返回 errno 110；active-close/reopen 后 lower 重新 START |
| PWM-008 | `pwm0=yes pwm1=no gpio22=no`；owner 和 close 清理通过 |

汇总为 `PWM Summary: executed=8 passed=8 failed=0 -> PASS`。随后在同一固件和同一串口
完成现役回归：GPIO edge 3 cycles、TIMER-001 0.253% 最大误差、TIMER-002 周期比
2.000、TIMER-005、oneshot 100000 us、WDT-002（3026 ms 内 keepalive 6 次）、
WDT-003、`/dev/rtc0` 以及 `date` 两秒递增；系统存活检查为 `ST033_PWM_ALIVE=0`。

ST034 先在未修复 NuttX 上复现 `executed=8 passed=7 failed=1`，唯一失败为 active
close/reopen 未重新调用 lower。修复后同一 USB2、配置和命令得到 8/8 PASS；同次
TIMER-001 最大误差 0.275%，其他现役外设回归和系统存活检查继续通过。

最新 `nsh` whole image 为 4 MiB，boot2、双 partition 和 app magic 正确，
MFG 区域保持全 `0xff`；烧录到 USB2 后设备端 SHA256 与主机一致。产品配置无测试
命令和 test hook，关闭态无 PWM lower、board 和 LHAL PWM 对象。
