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
board 按 Kconfig 为 CH0–CH3 选择正、负输出脚，只注册一个 `/dev/pwm0`。首版只接了
CH3 正向输出（GPIO22），之后扩展为多通道和互补输出，本文两段历史都保留。

## 最大能力交集

| 能力 | 当前处理 | 依据和限制 |
|---|---|---|
| 单 controller | 纳入首版 | BL616CL 只有 `pwm_v2_0`；base、gate、reset、IRQ 与 TIMER 独立 |
| CH0–CH3 正/负输出 | 已纳入 | 有负脚时 `reg_pwm1_io_sel=1`，GPIO n 输出 CH((n%8)/2)，n%8 偶数为正、奇数为负；全是正输出时 `reg_pwm1_io_sel=0`，GPIO n 输出 CH(n%4)+ |
| continuous PWM | 纳入首版 | 对应非 pulse-count OpenVela lower ABI |
| 整数 frequency | 纳入首版 | 读取实际 PBCLK，取放得下 16-bit period 的最小 divider，period 四舍五入 |
| b16 duty | 纳入首版 | 0..65535/65536 映射到 threshold；最大值不声明为 100% |
| `cpol`/`dcpol` | 纳入首版 | 正向 polarity 与 active/inactive stop-state 可无歧义映射 |
| live update | 纳入首版 | 同 divider 使用 update gate；跨 divider 允许 stop/reinit |
| 多 fd | 纳入首版 | 首次 open setup，最后 close shutdown，由 upper mutex 串行化 |
| multichannel | 已纳入 | `PWM_MULTICHAN` 下通道号 1–4 对应 CH0–CH3；四通道共用 frequency |
| pulse count/IRQ | P2 | LHAL 有 repetition/IRQ 原语，尚无完成回调和取消合同 |
| complementary | 已纳入 | 负输出始终是正输出的反相（运行和停止都是），没有死区 |
| deadtime | P2 | 硬件有 8-bit deadtime，OpenVela `PWM_DEADTIME` ABI 未接入 |
| brake/center-aligned | P2 | LHAL 有原语，尚未映射公共控制和安全状态 |
| ADC trigger | P2 | 硬件支持，需与后续 ADC owner 联合设计 |
| fixed-point frequency | P2 | OpenVela ABI 可选，首版只实现整数 frequency |
| PWM DMA | P3 排除 | BL616CL DMA request 表没有 PWM request |

完整枚举不等于一次性启用全部能力。P2 项保留为芯片共同能力和后续恢复入口，不能写成
“BL616CL 不支持”；首版只声明可裁剪且有明确软件与波形判据的单通道子集，多通道和互补
输出随后按同样的判据加入。

## 调用链与 archive 归属

```text
board_late_initialize()
  -> bl616cl_board_initialize()
  -> ai_m64p_kit_pwm_initialize()
  -> bl616cl_pwm_initialize(outputs, noutputs)   /* 每个已选通道一项 */
  -> pwm_register("/dev/pwm0", lower)

open / SET / START / STOP / close
  -> OpenVela pwm upper-half
  -> BL616CL lower setup/start/stop/shutdown
  -> PWM0 clock + PWM io_sel + 各通道引脚 pinmux
  -> bflb_pwm_v2_*()
```

- `chips/bl616cl/bl616cl_pwm.c`：OpenVela lower、频率求解和错误转换，编入 `libarch.a`；
- `boards/bl616cl/ai-m64p-32s-kit/src/ai_m64p_kit_pwm.c`：按 Kconfig 生成通道和引脚表，
  编译期检查引脚槽位，注册 `/dev/pwm0`，编入 `libboard.a`；
- `cmake/bl616cl_lhal.cmake`：按 PWM Kconfig 选择现有 LHAL PWM/clock 依赖，
  编入 `libbl_lhal.a`；
- `apps/mcu_peripheral_tests/pwm/`：单一测试 main，独立生成测试 archive；
- `drivers/`：保持只读，chip adapter 不放入 `libbl_std.a`。

## Kconfig 与引脚所有权

| 选项 | 作用 | 关闭效果 |
|---|---|---|
| `BL616CL_PWM` | chip lower 总开关，并启用通用 PWM upper-half | 无 lower 和 PWM LHAL 对象 |
| `AI_M64P_KIT_PWM` | board 通道选择和 `/dev/pwm0` | 不接管引脚，不注册节点 |
| `AI_M64P_KIT_PWM_CHn` | 启用通道 n（0–3） | 该通道不可用 |
| `AI_M64P_KIT_PWM_CHn_POS_PIN` / `_NEG_PIN` | 通道 n 的正/负输出 GPIO，-1 表示不用，至少配一个 | — |
| `BL_MCU_PERIPHERAL_TESTS_PWM` | `mcu_pwm_test` 单一测试 main | 无测试 archive、命令和字符串 |

芯片 `select ARCH_HAVE_PWM_MULTICHAN`。`BL616CL_PWM` 依赖关闭 `PWM_PULSECOUNT`、
`PWM_FREQUENCY_FIXED` 和 `PWM_DEADTIME`。开 `PWM_MULTICHAN` 时可选 1–4 个通道；不开时
board 必须只选一个通道，否则编译失败，沿用单通道 ABI。板级默认只开 CH3、正输出 GPIO11；
各通道正输出默认 GPIO8、9、10、11，都按下面的单端布局选取。
board 选项使用 `depends on BL616CL_PWM`，不能通过 `select` 绕过 chip 选项依赖。

启用 board PWM 后，所有已配引脚都归 PWM（`ai_m64p_kit.h` 的 `AI_M64P_KIT_PWM_OWNS()`）：

1. GPIO 测试节点列表跳过这些引脚；
2. I2C0/I2C1、SPI0、UART1 选到这些引脚时编译失败；
3. 引脚不在所选通道的槽位上时编译失败；
4. `/dev/pwm1` 不存在，不用运行时注册顺序解决双 owner。

PWM 输出脚由 GLB `reg_pwm1_io_sel`（全局，一次只能一种）决定通道映射：

- 单端布局（值 0，复位值，`PWM_IO_SEL_SINGLE_END`）：只有正输出，GPIO n 输出
  CH(n%4)+，每个通道有 8 个候选脚。所有已配通道都没有负脚时使用。
- 差分布局（值 1，`PWM_IO_SEL_DIFF_END`）：GPIO n 输出 CH((n%8)/2)，n%8 偶数为正、
  奇数为负，每个通道正、负各 4 个候选脚。只要有一个通道配了负脚就使用。

lower 在 START 时按板级表写 io_sel，并在回读时核对；board 在编译期按同样的规则检查
引脚槽位，混用时报差分布局的规则。同一 GPIO 在两种布局下属于不同通道，例如 GPIO22
在单端布局是 CH2+、在差分布局是 CH3+，所以给某个通道加负脚后，其余通道的正脚也要按
差分布局重选。不同通道之间不会撞脚。板级 Kconfig 允许 GPIO0–31（32–36 是 USB、UART0
和 BOOT）：

| 通道 | 单端正输出（n%4） | 差分正输出（n%8） | 差分负输出（n%8） |
|---|---|---|---|
| CH0 | 0、4、8、12、16、20、24、28 | 0、8、16、24 | 1、9、17、25 |
| CH1 | 1、5、9、13、17、21、25、29 | 2、10、18、26 | 3、11、19、27 |
| CH2 | 2、6、10、14、18、22、26、30 | 4、12、20、28 | 5、13、21、29 |
| CH3 | 3、7、11、15、19、23、27、31 | 6、14、22、30 | 7、15、23、31 |

两种映射来自 PIN_MUX 表，已用逻辑分析仪在 GPIO12、13、16、22 上确认（见
“实物波形数据”“多通道与互补输出实测”和“单端布局实测”）。

`nsh-peripherals` 的分配：CH0 正 8/负 9，CH1 正 10/负 11，CH2 只用负输出 21，CH3 正
6/负 7。CH2 正输出的候选 4、12、20、28 分别被 I2C1、SPI0、GPIO 回环和暖白灯占用；
GPIO22 回到 GPIO 测试节点。

## 频率、duty 与 polarity 合同

lower 读取实际 PBCLK。请求 frequency 为 0、源时钟为 0，或请求高于 `PBCLK/2` 时拒绝；
所需 divider 超过 65535（频率过低）时同样拒绝。divider 取让 period 不超过 65535 的
最小值 `ceil(PBCLK / (frequency * 65535))`，period 取
`round(PBCLK / (frequency * divider))`，使 period 和 duty 分辨率最大，与 STM32、Linux
等 PWM 驱动的取舍相同。divider 大于 1 时 period 不小于 32768，频率误差不超过
`1/(2*period)`（约 15 ppm）；divider 为 1 时 period 就是最接近的整数。配置后必须读取
实际 divider/period 并据此计算 actual frequency，不能用请求值或仅缓存配置冒充寄存器
证据。

lower 记住上一次求解时的 PBCLK、frequency 和结果，两者都没变（只改 duty 或
polarity）时直接沿用。

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

`CPOL_NDEF` 和 `DCPOL_NDEF` 均拒绝。lower 在 START 时回读 CONFIG1 polarity/stop-state
bit，不一致返回 `-EIO`。测试 app 可用回环 GPIO 检查主通道的 STOP 电平；其余引脚和
close 后的电平由仪器确认。

`cpol`/`dcpol` 描述正输出。负输出用与正输出相同的 polarity（NPL=PPL）和相反的
stop-state（NSI=!PSI），所以运行和停止时都是正输出的反相：停止电平正输出为 dcpol、
负输出为 !dcpol。只配负脚时，引脚上就是反相波形，例如 duty 25% 时负脚高 75%。正负
输出由同一计数器比较产生，没有死区，不能直接驱动需要死区的半桥或功率级。

## 多通道合同

- 四个通道共用 clock、divider 和 period，频率必然相同，因此只注册一个 `/dev/pwm0`，
  不按通道分设备（分设备会互相改频率）。
- `PWM_MULTICHAN` 下 START 中 `channel=0` 的项跳过，`-1` 结束；提到的通道必须是板级
  已配的通道且不重复，一个都没提到也拒绝，均返回 `-EINVAL`，硬件不变。
- START 中没提到的通道保持原状态：已运行的按原 duty 继续，未运行的保持停止。频率不变
  时只改提到的通道；频率变化需要换 divider 时，全部通道停下后按新 period 重建，没提到
  的运行通道按原 duty 恢复。
- STOP 和最后一个 close 停全部通道。

## 启停顺序

STOP（含跨 divider 重建前的停止）：

1. 一次寄存器写清掉所有通道的 PEN/NEN，全部输出同时进入停止电平；
2. 停计数器；
3. 先把各引脚的 GPIO 输出锁存写成空闲电平，再把引脚切回 GPIO 输出。`bflb_gpio_init()`
   不改输出锁存（CFG136/137），所以切换时电平不动；
4. deinit 并关 clock。deinit 会把 polarity/stop-state 复位，此时引脚已不在 PWM 功能上。

START：

1. 配置 divider/period，以及各通道 polarity、stop-state、threshold；
2. 对新启动的通道，把 GPIO 引脚移到新设置的空闲电平（先写变为非有效的那一路，正负
   不会同时有效），再切到 PWM 功能，此时 PWM 停止电平与引脚电平相同；
3. 一次寄存器写打开所有新通道的 PEN/NEN，再启动计数器。

dcpol 与上次停止时相同时，START 没有中间电平；dcpol 改变时，引脚先在新空闲电平停留
几到几十 µs（实测 7–47 µs）再开始输出。

## timeout 与 upper-half 限制

LHAL `init/start/stop/deinit` 返回 `void`，等待硬件状态超过 100ms 时静默返回。lower
必须在调用后读取 `PWM_STS_STOP`，把失败转换为 `-ETIMEDOUT`，并强制关闭全部输出、
退出 PWM pinmux、gate clock 和清理内部 started 状态。超时只在硬件异常时出现，测试
app 不覆盖这条路径。

通用 upper-half 有两项必须显式保留的限制：

- 停止态 SET 先保存参数并返回 OK，真正 lower 校验发生在后续 START；运行态 SET 会先
  覆盖保存参数，再返回 lower 错误，因此失败后 GET 可能是无效请求值；
- STOP 不论 lower 返回值都会清 upper `started`，close 也忽略 shutdown 返回值；测试不能
  虚构 close errno 或 upper 状态保持；
- `pwm_register()` 注册失败时通用 upper allocation 未释放。本项只保证静态 lower 在
  注册前不占 pin/clock，不在运行时用重复注册制造泄漏。

后续补充修复了 active close 生命周期：最后一个 fd close 调用 lower shutdown 后，
upper 同步清除 `started`。否则 reopen 后 `PWMIOC_START` 会因旧状态跳过 lower start，
向应用返回成功但硬件不输出。PWM-001 用回环 GPIO 检查 reopen 后输出确实在翻转。

## 构建与裁剪门禁

当前专项验证使用 `nsh-peripherals`；下表的 `nsh-pwm` 是已删除配置的历史
裁剪证据。基线关闭态仍使用 `nsh`。在 SDK 根目录执行：

```bash
vendor/bouffalolab/vela clean \
  bl616cl/ai-m64p-32s-kit/configs/<config>
vendor/bouffalolab/vela build \
  bl616cl/ai-m64p-32s-kit/configs/<config> -j14
```

验收必须同时回读 `.config`、Ninja source、archive、map、ELF symbol 和 NSH `help`：

| 配置 | 预期 lower/LHAL/board | 预期 app | 当前结果 |
|---|---|---|---|
| `nsh` | 无 | 无 | clean build 通过，1224/1224 |
| `nsh-pwm`（历史，已删除） | 有 | 无 | clean build 通过，1229/1229 |
| `nsh` | 有 | 有 | clean build 通过，1231/1231；栈 6144 |

defconfig 通过 Kconfig 工具和 `savedefconfig` 生成，不手工追加选项。

## 软件与实物流程

测试 app 使用一个 `mcu_pwm_test` main，以 `-c` 选择 case（001～006、008、009）。
001～008 针对主通道（编号最小的已配通道），009 验证多通道和互补输出。测试只使用
`/dev/pwm0` 的标准 ioctl，判据是 ioctl 返回值、GET 读回，以及 `-g` 指定的回环 GPIO
读到的主通道电平；逐 case 的命令和判据见 `apps/mcu_peripheral_tests/pwm/README.md`。
软件阶段还要
回归现役 GPIO、TIMER0、TIMER1、oneshot、WDT 和 RTC；GPIO 回归排除已配给 PWM 的引脚。

单通道 G4 波形最初在 GPIO22（CH3+）验证：

1. 100Hz/75%、1kHz/25%、1kHz/50%、1kHz/75%、10kHz/25%；
2. frequency 误差不超过 1%，duty 误差不超过 2 个百分点；
3. 四组 cpol/dcpol 的运行 polarity、STOP 电平和 close 电平；
4. 同 divider duty update 不出现超过一个 PWM 周期的异常脉冲；
5. 跨 divider update 记录停顿，但本轮不声明无毛刺。

串口只承载控制台，GPIO22 也不接板载 LED；ioctl 返回值和 GET 读回不能证明
frequency、duty、polarity 或动态更新波形。逻辑分析仪已完成第 1～5 项（见“实物波形数据”）；第 3 项中 cpol=LOW/dcpol=LOW
的运行极性因 50% duty 且起始相位与停止电平相连，无法单独从波形区分。

## 软件实测数据

2026-10-16 当前程序在 Ai-M64P-32S-Kit `nsh-peripherals` 上执行 `mcu_pwm_test -c all -w 0`（未接 `-g` 回环）：8 个 case 全部 PASS，8 项电平检查 SKIP（`level_checks_skipped=8`）；带 `-g` 的电平检查尚未实板运行。以下是单通道时期的实板记录。

测试固件为 `nsh`，通过 `/dev/ttyUSB2`、2,000,000 baud 启动，USB ID 为
`1a86:7523`。启动后出现 `NuttShell (NSH)` 和 `nsh>`；`help` 包含
`mcu_pwm_test`，`ls /dev` 包含 `/dev/pwm0`，且没有 `/dev/pwm1`、`/dev/gpio22`。

执行 `mcu_pwm_test -c all -w 0` 时 PWM-001～006、008 均 PASS；START-before-SET、零频率、
超范围频率和 4 类 polarity 均被拒绝，errno 为 22/34。随后在同一固件和同一串口完成现役
回归：GPIO edge 3 cycles、TIMER-001 0.253% 最大误差、TIMER-002 周期比 2.000、
TIMER-005、oneshot 100000 us、WDT-002（3026 ms 内 keepalive 6 次）、WDT-003、
`/dev/rtc0` 以及 `date` 两秒递增；系统存活检查返回 0。

最新 `nsh` whole image 为 4 MiB，boot2、双 partition 和 app magic 正确，
MFG 区域保持全 `0xff`；烧录到 USB2 后设备端 SHA256 与主机一致。产品配置无测试
命令，关闭态无 PWM lower、board 和 LHAL PWM 对象。

## 实物波形数据

2026-10-13 在 Ai-M64P-32S-Kit（BL616CL QFN48，片内 4 MB flash、4 MB PSRAM）上测量。
固件为 `nsh-peripherals`，`nuttx.bin` SHA256 为
`7aa301b940a05d30f19ae2f6b749cd055547302c4d794dbb29f28fccd66e782f`。逻辑分析仪
DSLogic U3Pro32 接 GPIO22 和 GND，阈值 1.6 V，采样率 2 MHz，以 GPIO22 上升沿触发后
运行 `mcu_pwm_test -c 003`（`-w 1`）：

| 设定 | 实测频率 | 实测占空比 | 周期数 |
|---|---|---|---|
| 100 Hz / 75% | 100.00 Hz | 75.00% | 200 |
| 1 kHz / 25% | 999.97 Hz | 25.00% | 2013 |
| 1 kHz / 50% | 999.97 Hz | 50.00% | 2017 |
| 1 kHz / 75% | 999.97 Hz | 75.00% | 887（采集窗口结束） |

频率误差不超过 0.003%，占空比误差为 0，满足 G4 阈值。参数切换处的逐周期数据：

- 跨 divider（100 Hz → 1 kHz）：最后一个 100 Hz 周期从 10000 µs 拉长到 10114 µs，
  停顿约 114 µs；随后第一个 1 kHz 周期为 1002 µs，此后正常。
- 同 divider（1 kHz 25% → 50%）：出现一个 476.5 µs 的短周期（高 250 µs），再输出一个
  旧占空比周期，之后为 50%。
- 同 divider（1 kHz 50% → 75%）：出现一个 1118 µs 的周期（高 618 µs），之后为 75%。

同 divider 更新的异常只持续一个 PWM 周期，满足第 4 项判据，但切换不是无毛刺的。
采集时 lower 按全局最小频率误差遍历 divider，1 kHz 为 divider 4、period 40000；
改为最小 divider 后 1 kHz 为 divider 3、period 53333（理论 1000.006 Hz），复测见下。

### 闭式求解后的复测

同日改为闭式求解后复测，`nuttx.bin` SHA256 为
`2a23fa400ab4cce5c02ddfcf0324b76571ad0a149abf92f3ea55784837aab55a`，采样率 2 MHz、
阈值 1.6 V。`mcu_pwm_test -c 003 -w 2`：

| 设定 | divider/period（按上文公式） | 实测频率 | 实测占空比 | 周期数 |
|---|---|---|---|---|
| 100 Hz / 75% | divider 25、period 64000 | 100.00 Hz | 75.00% | 200 |
| 1 kHz / 25% | divider 3、period 53333 | 999.97 Hz | 25.00% | 2006 |
| 1 kHz / 50% | 同上 | 999.97 Hz | 50.00% | 2006 |
| 1 kHz / 75% | 同上 | 999.97 Hz | 75.00% | 2004 |
| 10 kHz / 25% | divider 1、period 16000 | 9999.68 Hz | 25.00% | 20067 |

1 kHz、10 kHz 都比理论值低约 32～36 ppm，偏差一致，按分析仪时基误差处理。切换处：
100 Hz → 1 kHz 有一个约 10121 µs 的周期；1 kHz 25% → 50% 有一个 699.5 µs 的短周期，
再输出一个旧占空比周期；50% → 75% 没有异常周期；1 kHz → 10 kHz 有一个 1112 µs 和
一个 102.5 µs 的周期。

`mcu_pwm_test -c 005 -w 1`，1 kHz/50%，四组依次 SET/START、保持 1 s、STOP，
随后立即进入下一组：

| cpol / dcpol | STOP 电平 | START 后首个相位 |
|---|---|---|
| LOW / LOW | 0（约 690 µs，到下一组 SET） | 与停止电平相连，无法区分 |
| LOW / HIGH | 1（约 732 µs） | 低 502.5 µs |
| HIGH / LOW | 0（与最后半周期的低电平相连，共 527 µs） | 高 258.5 µs、低 59.5 µs、高 502.5 µs，首周期不规则 |
| HIGH / HIGH | 1（约 322 µs），close 后保持 1 | 高 518 µs |

STOP 和 close 电平都等于请求的 dcpol。停止态下 SET 下一组时，输出先变为新组的停止
电平（LOW/HIGH 前 15 µs 高、HIGH/LOW 前 16 µs 低）；HIGH/HIGH 前只出现 1.5 µs 高，
随后低 204.5 µs 才开始输出；close 时有一个 12.5 µs 的低脉冲。以上是改为多通道前的单通道
驱动数据，现行启停顺序的结果见下节。

## 多通道与互补输出实测

2026-10-13 在同一块 Ai-M64P-32S-Kit 上测量。`nsh-peripherals` 的 PWM 引脚（6–11、21）
没有接逻辑分析仪，波形用三个临时板级配置（未提交，只改 PWM 引脚）在已接探头的
GPIO12、13、16、22 上测；DSLogic U3Pro32，阈值 1.6 V。

配置 A（`PWM_MULTICHAN`，CH0 正 16、CH1 负 19、CH2 正 12/负 13、CH3 正 22/负 23；19、23
未接探头），20 MHz，`mcu_pwm_test -c 009 -w 2`：

| 阶段 | GPIO16 CH0+ | GPIO12 CH2+ | GPIO13 CH2− | GPIO22 CH3+ | 频率 |
|---|---|---|---|---|---|
| 四通道同时 START | 20.00% | 60.00% | 40.00% | 80.00% | 999.97 Hz |
| 只提 CH0 改为 10 kHz/50% | 50.00% | 60.00% | 40.00% | 90.00% | 9999.67 Hz |
| 最后 STOP（dcpol LOW） | 0 | 0 | 1 | 0 | — |

CH3 在两段之间单独改为 90%，其余通道 threshold 不变。GPIO13 与 GPIO12 逐样本相等的比例
为 0.0006%，都在边沿上，边沿偏差中位数为 0。跨 divider 时四路在同一个样本进入空闲电平，
停顿 1.10 ms 后在同一个样本开始输出；同 divider 更新有一个缩短的周期（1 kHz 时
906 µs），之后为新 duty，与单通道时的行为相同。

配置 B（`PWM_MULTICHAN`，CH2 正 12/负 13 为主通道，CH3 正 22/负 23）：

- 10 MHz，`-c 005 -w 1`：四组 cpol/dcpol 都是 999.97 Hz/50%，GPIO13 始终是 GPIO12 的
  反相（相等样本 0.0002%）；STOP 电平正 = dcpol、负 = !dcpol；组间 START 先在新空闲电平
  停留 7–11 µs，过渡中正负从不同时处于有效电平（cpol LOW 时同为高、cpol HIGH 时同为低，
  都是同时无效）。
- 20 MHz，`-c 009 -w 1`：CH2 20%/80%（正/负）、CH3 40%，CH3 原地改为 90% 后其余不变，
  10 kHz 时 CH2 50%/50%、CH3 90%；全部 PASS。
- TIMER 测试 `mcu_timer_test -c 003 -f 2000 -D 40 -C 3`：GPIO12 1999.94 Hz/40.00%，GPIO13
  60.00%；`-c 004 -f 1000 -s 25 -i 300 -n 1 -C 3` 各档 25/50/75% 正确；`-C 2`（未配置的
  CH1）START 返回 `EINVAL`。

配置 C（不开 `PWM_MULTICHAN`，只配 CH2 负输出 GPIO13）：`-c all -w 1` 时 009 SKIP、其余 PASS；
10 MHz `-c 004 -w 1` 时 duty 0、25%、50%、75% 对应 GPIO13 高 100%、75.00%、50.00%、
25.00%，65535/65536 时每周期只有 1 个计数（约 19 ns）的高脉冲，close 后为 1（dcpol LOW）。

`nsh-peripherals` 实板：`mcu_pwm_test -c all -w 1` 全部 PASS，clean build 镜像（`nuttx.bin`
SHA256 `61f14972b54c1322a885577cf465fc074052bd18a2618399ed6c07c190cbdb02`）上复跑
`-c all -w 0` 仍全部 PASS。GPIO6–11、21 上的频率、占空比和停止电平没有用仪器测。

## 单端布局实测

2026-10-14 在同一块板上用临时配置（未提交）测量，DSLogic U3Pro32，阈值 1.6 V。

- 4 通道只配正输出（`PWM_MULTICHAN`，CH0 16、CH1 13、CH2 22、CH3 11；11 未接探头），
  20 MHz，`mcu_pwm_test -c 009 -w 1`：GPIO16 20.00%、GPIO13 40.00%、GPIO22 60.00%，
  都是 999.97 Hz；只提 CH0 改为 10 kHz 后 GPIO16 50%，GPIO13、GPIO22 保持 40%、60%，
  9999.67 Hz。GPIO13、GPIO22 在差分布局下分别是 CH2−、CH3+，这里输出 CH1、CH2，说明
  io_sel 已切到 0；同属 CH0 槽位但未配置的 GPIO12 始终不动。`-c 005` 四组 cpol/dcpol 的
  STOP 电平等于 dcpol，`-c all` 全部 PASS。
- 单通道（不开 `PWM_MULTICHAN`，CH2 正 22），2 MHz，`-c 003 -w 1`：GPIO22 输出 100 Hz/75%、
  1 kHz 25/50/75%、10 kHz/25%（9999.68 Hz）；`-c all` 时 009 SKIP、其余 PASS。
- 差分布局回归（CH2 12/13、CH3 22/23）：`-c 009` 波形与之前相同，GPIO12/13 互补，
  `-c all` 全部 PASS。
- 编译期：单端下 CH2 选 GPIO12 报 `needs pin % 4 == 2 (no negative pins)`；CH0 加负脚 17
  后，单端选的 13、22、11 按差分规则报错；板级默认（只开 CH3、正 GPIO11）可编过。
- 正式 `nsh-peripherals`（差分布局，`nuttx.bin` SHA256
  `9293866fe76e3647057f45f9656617d5ad8a42c8a9b7ef236147dc97d9f4823e`）：`-c all -w 0` 全部
  PASS；GPIO、SPI、I2C、UART 回归无失败。
