# BL616CL I2C 主机测试

本目录提供一个 NSH 命令 `mcu_i2c_test`，同一个 `main` 按参数选择软件桩和实物总线测试。`hw` 模式调用 OpenVela 的 `/dev/i2cN` 字符设备；`fake` 模式直接调用芯片 lower-half 的 `I2C_TRANSFER()`，通过芯片层测试注入接口检查消息翻译和错误清理。I2C0、I2C1 的控制器和引脚开关由芯片/板级 Kconfig 管理，测试 app 默认关闭。

## 背景与范围

BL616CL LHAL 是同步 polling master，文档频率范围为 305 Hz 至 400 kHz；P0 适配只开放已纳入验证的 100 kHz/400 kHz、7-bit 地址和 1..1024 字节传输。OpenVela 的 `struct i2c_msg_s` 还包含 `I2C_M_NOSTOP`、`I2C_M_NOSTART`、10-bit 地址等通用标志；适配层必须在访问硬件前检查这些标志，不能把公共 flags 原样传给 LHAL。

本 app 覆盖以下可重复的交集：

| 模式 | 命令 | 覆盖内容 |
| --- | --- | --- |
| fake | `fake flags` | STOP 分组、combined 消息和频率配置次数 |
| fake | `fake boundary` | 单消息 read/write 1/1024，combined prefix 1/16 + read 1/1024 |
| fake | `fake invalid` | 空数组、地址/频率/长度/flag/combined 边界，确认硬件未被访问 |
| fake | `fake errors` | 保留 LHAL 原始 errno，失败后按 `transfer -> status -> cleanup` 取证，并确认下一次传输恢复 |
| fake | `fake concurrent` | 同一总线多线程调用的互斥和整组原子性 |
| fake | `fake dual` | I2C0/I2C1 各自串行且两条总线可同时进入 transfer |
| hw | `hw probe` | 指定 7-bit 地址单字节访问，确认 ACK/错误返回 |
| hw | `hw combined` | EEPROM 地址前缀 + read 的一次 repeated-start |
| hw | `hw boundary` | 1、4、31、32、33、255、256、1024 字节读边界 |
| hw | `hw eeprom` | 显式允许后备份、分页写入、整段读回逐字节比较、恢复和恢复校验 |
| hw | `hw concurrent` | 同一总线多个线程反复 combined read，检查数据没有交叉 |
| hw | `hw dual` | I2C0 与 I2C1 两个总线并行访问各自 target |

DMA、10-bit 地址、三段以上 repeated-start、`NOSTART` continuation、总线复位不在本 P0 app 中伪装为已支持能力；相应参数会被拒绝或留给后续测试。

2026-08-30 已在 Ai-M64L-32S-Kit 的 I2C1-only 固件上完成 clean build、烧录、
启动和全部单总线 fake case。实物总线仍缺外接 target、上拉和逻辑分析仪，`hw`
case 只记录完整执行流程和判据，不标记为已实测 PASS。I2C0 默认 GPIO4/5 会与
当前模组的 32.768 kHz/DC-DC 资源冲突，I2C0-only 和 dual 也不标记为运行通过。

## 配置与构建

打开 `CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C` 前必须同时启用 `I2C`、`I2C_DRIVER` 和 `BL616CL_I2C`。该选项会选择芯片层的 `BL616CL_I2C_TEST` hook，默认配置为：

```text
CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C=y
CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_PROGNAME="mcu_i2c_test"
CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_THREADS=4
CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_ITERATIONS=32
```

完整 EEPROM 命令超过 NSH 默认 64 字节行长。启用本测试 app 的验证配置同时使用
`CONFIG_NSH_LINELEN=256`，否则命令会在解析前被控制台截断；这不是 I2C 数据长度配置。

板级 `AI_M64L_KIT_I2C` 总开关默认关闭；打开总开关后，I2C0 实例默认开启并使用
GPIO4/5，I2C1 实例默认关闭且配置为 GPIO12/13。该 Kconfig 默认值只描述通用板级
候选 pin，不表示当前模组可直接使用 GPIO4/5；当前模组必须关闭 I2C0、显式打开
I2C1，或在确认模组资源后另选无冲突 pin。配置和编译使用 SDK 标准入口：

```bash
python3 vendor/bouffalolab/bl_build.py menuconfig \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

`/dev/ttyUSB2` 只用于烧录、复位和 NSH 日志。I2C 波形必须由独立逻辑分析仪测量；USB2 不是 I2C 分析仪。

## 通用运行流程

1. 上电后在 USB2 控制台确认 `NuttShell (NSH)` 和 `nsh>`，再执行 `ls /dev`，确认所选 `/dev/i2cN` 节点存在。
2. 对 fake case 直接运行命令，不需要外接器件；每个 PASS 行包含 case、调用次数和关键断言。
3. 对 hw case 先接 3.3 V 外部 I2C target。AT24C02/AT24C32 常用地址为 `0x50`，SCL/SDA 各使用 2.2 kOhm 至 4.7 kOhm 上拉。
4. 记录命令、参数、完整 case 顺序、每次返回值、地址、频率、长度和错误码；逻辑分析仪同时记录 START、repeated-start、STOP、ACK/NACK 和实测 SCL。
5. 单个 case 结束必须看到一条最终 `I2C_TEST RESULT ... PASS/FAIL`，然后返回 `nsh>`；
   参数错误的 usage 也必须出现在该终态行之前。失败前的诊断行保留 bus、errno、
   raw status 或 EEPROM restore 状态，最终 RESULT 不重复覆盖这些字段。`fake all`
   会保留各子 case 的 PASS RESULT，并以一条 aggregate RESULT 结束。`FAIL` 不能用
   “设备不存在”替代硬件证据。

## fake 测试

单总线 fake case 都接受 `--bus N`；I2C0-only 用默认 bus0，I2C1-only 必须显式指定 bus1：

```text
mcu_i2c_test fake all --bus 0
mcu_i2c_test fake all --bus 1
```

`fake all` 只跑所选单总线的 flags、boundary、invalid、errors、concurrent，因此 I2C0-only/I2C1-only 配置都可独立 PASS。只有同时启用 I2C0/I2C1 时才显式执行 `mcu_i2c_test fake dual`。

### `fake flags`

命令：

```text
mcu_i2c_test fake flags --bus 0
```

流程：

1. 通过 `bl616cl_i2c_test_install(bus, ...)` 为所选实例安装 fake lower-half，再取得该实例测试设备。
2. 提交两个普通消息：一个 1 字节 write、一个 4 字节 read，频率分别为 100 kHz 和 400 kHz。适配层必须拆成两个 LHAL transfer，每次 transfer 前调用一次 configure。
3. 提交 `WRITE|NOSTOP` + `READ` 两条同频同地址消息。适配层必须把它们作为一个 LHAL 双消息调用，首条保持 `NOSTOP`，第二条只有 `READ`，不得生成公共头文件之外的 restart flag。
4. 检查 fake 收到的 count、flags、frequency、读缓冲区填充值和调用次数；最后卸载 fake，恢复真实 ops。

I2C1-only 实测关键输出：

```text
I2C_TEST RESULT mode=fake case=flags PASS separate_calls=2 combined_calls=1
```

判定：普通 STOP 边界是两个调用，combined 是一个双消息调用；两种路径均返回成功且读缓冲区发生变化。

### `fake boundary`

命令：

```text
mcu_i2c_test fake boundary --bus 0
```

流程先对单消息 write/read 分别执行 1 字节和 1024 字节，再对 combined 传输执行 prefix=1/16 与 read=1/1024 的四种笛卡尔组合。每一项检查 fake 收到的长度、消息数和 flags；1024 字节 read 还检查 buffer 已被 fake 写入。该 case 是最大长度的正向证据，与 invalid 的 1025 字节拒绝形成边界闭环。

I2C1-only 实测关键输出：

```text
I2C_TEST RESULT mode=fake case=boundary PASS single_lengths=1,1024 combined_prefixes=1,16 read_lengths=1,1024 bus=1
```

### `fake invalid`

命令：

```text
mcu_i2c_test fake invalid --bus 0
```

依次提交 16 个故意非法或未支持输入：NULL 消息数组、count=0、频率 0、未支持频率 200 kHz、地址 0x80、10-bit flag、NOSTART、未知 flag、零长度、1025 字节、NULL buffer、终止 NOSTOP、17 字节 combined 前缀、combined 地址不一致、combined 频率不一致、三段无 STOP 链。每一项都要求返回预期 `-EINVAL` 或 `-EOPNOTSUPP`，且 fake 的 configure/transfer 计数保持 0，证明拒绝发生在硬件访问之前。prefix=17 是结构合法但超过当前硬件适配能力的 combined 传输，按 lower-half 合同返回 `-EOPNOTSUPP`。该值在 chip/arch 和 app 的 errno ABI 中均为 95，避免工具链与 NuttX 对 `ENOTSUP` 编号不同造成跨 archive 判定漂移。

I2C1-only 实测关键输出：

```text
I2C_TEST fake-invalid PASS name=combined-prefix-17 ret=-95 hardware_calls=0
I2C_TEST RESULT mode=fake case=invalid PASS rejected=16
```

### `fake errors`

命令：

```text
mcu_i2c_test fake errors --bus 0
```

fake configure 先返回 `-ERANGE`，要求事件严格为 `configure -> status -> cleanup`，且 transfer 不得执行；然后 fake transfer 返回 `-EIO`，raw status 设置为 `NACK|FER`，要求事件为 `configure -> transfer -> status -> cleanup`。两条路径必须原样保留 fake 注入的 OpenVela errno，不在 app 中擅自映射为 NACK/ARB/FER errno。真实 LHAL 的 toolchain `ETIMEDOUT=116` 由 chip adapter 规范化为 OpenVela `ETIMEDOUT=110`；最后把 fake 恢复为成功，重新传输一次，确认错误不会污染下一次调用。

I2C1-only 实测关键输出：

```text
I2C_TEST fake-errors configure-injected ret=-34 raw_status=00000040 order=configure,status,cleanup
I2C_TEST fake-errors injected ret=-5 raw_status=00000028 order=configure,transfer,status,cleanup
I2C_TEST RESULT mode=fake case=errors PASS configure_errno=-34 transfer_errno=-5 recovered=1
```

### `fake concurrent`

命令：

```text
mcu_i2c_test fake concurrent --bus 0
```

启动 Kconfig 指定数量的线程，每个线程重复单字节 read。fake transfer 人为延迟 1 ms 并统计同时进入的数量；`max_active` 必须为 1，调用总数必须等于 `threads * iterations`。该检查验证一把总线 mutex 覆盖整个 transfer，而不是只保护 configure。

I2C1-only 实测关键输出：

```text
I2C_TEST RESULT mode=fake case=concurrent PASS threads=4 iterations=32 calls=128 max_active=1
```

### `fake dual`

命令：

```text
mcu_i2c_test fake dual
```

为 I2C0/I2C1 分别安装独立 fake，同时启动两个 worker。每条总线的 `max_active` 必须等于 1，证明实例内部互斥；跨总线共享计数的 `max_active` 必须至少为 2，证明没有错误使用一把全局 mutex 把两条独立总线串行化。仅双实例配置执行该 case。

双实例预期输出格式；当前 I2C0 GPIO4/5 资源未冻结，尚无双实例运行实测：

```text
I2C_TEST RESULT mode=fake case=dual PASS bus0_calls=32 bus1_calls=32 per_bus_max_active=1 cross_bus_max_active=2
```

## 实物总线测试

### `hw probe`

命令：

```text
mcu_i2c_test hw probe --bus 0 --addr 0x50 --freq 100000
mcu_i2c_test hw probe --bus 0 --addr 0x50 --freq 400000
```

每条正向命令打开所选的 `/dev/i2cN`，发出一个 1 字节 read 并关闭设备。两种频率都需分别执行；地址被 target ACK 时返回 PASS，未连接或 NACK 时保留负 errno。串口 PASS 仅证明协议调用成功，ACK/NACK 和实际 SCL 仍以分析仪为准。

对确认未接器件的普通地址执行单地址负向测试：

```text
mcu_i2c_test hw probe --bus 0 --addr 0x52 --freq 100000 --no-target
```

`--no-target` 使用单字节 write 触发 BL616CL LHAL 会保留 raw status 的 NACK/timeout 路径；正向 target probe 仍使用单字节 read。该命令只有在指定地址返回冻结合同中的 `-ETIMEDOUT` 时 PASS，并直接打印 lower-half 保存的 `raw_status`；`-ENODEV`、`-EINVAL`、`-EOPNOTSUPP` 等本地参数或资源错误均 FAIL。如果意外收到 ACK 也 FAIL。地址必须由接线表确认不存在，且代码只允许普通地址范围 0x08..0x77，不能选择广播或保留地址。

I2C1-only 在未连接 target 的 GPIO12/13 上实测：

```text
nsh> mcu_i2c_test hw probe --bus 1 --addr 82 --no-target
I2C_TEST RESULT mode=hw case=probe PASS bus=1 addr=0x52 freq=100000 expected=no-target ret=-110 raw_status=0000000b
nsh>
```

该输出证明真实控制器超时从 LHAL toolchain errno 116 规范化为 OpenVela errno 110，
并保留 raw status；没有逻辑分析仪和上拉，不能据此判断实际 SCL 或 NACK 波形。

### `hw combined`

命令：

```text
mcu_i2c_test hw combined --bus 0 --addr 0x50 --offset 0x20 \
  --addr-width 2 --length 32 --freq 400000
```

流程：构造 1 或 2 字节 EEPROM 地址前缀，第一条消息为 `WRITE|NOSTOP`，第二条消息为
`READ`，同地址、同频率、read 长度为 1..1024；用一整段目标 buffer 执行一次
`I2CIOC_TRANSFER`。命令只有在整段 combined read 成功后才 PASS，首尾字节只是日志
摘要，不代表只读取两个字节。分析仪必须看到 START、地址+W、前缀、repeated-start、
地址+R、完整数据段、ACK、STOP，不能出现前缀与读之间的 STOP。

### `hw boundary`

命令：

```text
mcu_i2c_test hw boundary --bus 0 --addr 0x50 --offset 0 \
  --addr-width 2 --length 1024 --freq 100000
```

程序按 1、4、31、32、33、255、256、1024 字节逐项执行 combined read；大于 `--length` 的项目跳过。每项输出首尾字节，任何一项失败即停止。`--length 1024` 才能得到完整边界覆盖。参数解析还会检查 `offset + length` 不超过 1/2 字节地址宽度可表达的空间，`offset2` 同样检查，禁止 EEPROM 地址回绕后误写其它区域。

### `hw eeprom`

该 case 会改写外部 EEPROM，命令中必须显式写出 `--allow-write`，没有该参数时在打开设备前失败：

```text
mcu_i2c_test hw eeprom --bus 0 --addr 0x50 --offset 0x100 \
  --addr-width 2 --capacity 4096 --length 1024 --page-size 32 --freq 100000 \
  --allow-write
```

流程：

1. 读取并保存目标区域的完整 backup；backup 失败时绝不写入。
2. 生成非固定字节模式，按 EEPROM 页边界拆分为 `addr_prefix + data` 单消息写；每页写前检查 `current + chunk <= --capacity`，每页写后等待 `--write-delay-us`（默认 10000 us）。`--capacity` 必须明确填写，例如 AT24C32 为 4096。
3. 读回同一区域，逐字节比较 pattern；失败时仍进入恢复流程。
4. 用 backup 按同样页算法写回原数据，再次读回并逐字节比较。
5. 只有 pattern 验证成功且 restore 验证成功，才输出总 PASS；恢复失败明确输出 `data-may-be-modified`，不能伪报成功。

进入 EEPROM case 后，任何写入前失败都记录 `restore=not-required`；尝试写入后，
诊断行只能记录 `restore=verified` 或 `restore=failed`。每次调用最后仍只有一条
aggregate RESULT，避免后续通用 FAIL 覆盖恢复状态。

### `hw concurrent`

命令：

```text
mcu_i2c_test hw concurrent --bus 0 --addr 0x50 --offset 0 \
  --length 32 --iterations 32 --freq 400000
```

先读出 32 字节参考数据，再启动 4 个线程反复执行 combined read。每次结果与参考数据逐字节比较；任何混线、短读或 errno 都失败。该 case 只能证明同一总线调用没有数据交叉，不能证明外部 target 支持多主机并行。

### `hw dual`

命令示例（两个总线各接一个 target）：

```text
mcu_i2c_test hw dual --bus 0 --addr 0x50 --offset 0 \
  --bus2 1 --addr2 0x51 --offset2 0 --length 32 \
  --iterations 32 --freq 400000
```

程序先分别读取两个参考区域，然后同时启动两个线程，每个线程只访问自己的 `/dev/i2cN`。两个 bus 必须不同；两边所有循环均完成且数据匹配才 PASS。使用默认 I2C1 GPIO12/13 时，确认这两脚已从 GPIO lower-half 移除；使用自定义 pin 时核对实际配置 pin。

## 证据记录模板

每次实测至少记录以下内容，不把烧录步骤当成运行证据：

```text
board=ai-m64l-32s-kit chip=bl616cl console=/dev/ttyUSB2 baud=2000000
case=hw combined command="..."
target=AT24C32 addr=0x50 pullup=4.7kOhm pins=SCL4/SDA5
software=PASS/FAIL ret=<value> first=<byte> last=<byte>
analyzer=SCL=<measured>Hz start=<count> repeated_start=<count> stop=<count>
ack=<address/data summary> nack=<address/data summary>
restore=not-required/verified/failed
```

无外接 target 时，`hw` 的失败只能作为“总线调用和错误路径已执行”的证据，不能宣称 I2C 外设功能通过。fake case 不需要硬件，适合在每次 lower-half 代码变更后先做回归。
