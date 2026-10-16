# BL616CL I2C 主机测试

本目录提供一个 NSH 命令 `mcu_i2c_test`，通过 OpenVela 的 `/dev/i2cN` 字符设备和 `I2CIOC_TRANSFER` 对实物总线做测试，只使用公开的设备节点和标准 ioctl。I2C0、I2C1 的控制器和引脚开关由芯片/板级 Kconfig 管理，测试 app 默认关闭。

## 背景与范围

BL616CL LHAL 是同步 polling master，文档频率范围为 305 Hz 至 400 kHz；P0 适配只开放已纳入验证的 100 kHz/400 kHz、7-bit 地址和 1..1024 字节传输。OpenVela 的 `struct i2c_msg_s` 还包含 `I2C_M_NOSTOP`、`I2C_M_NOSTART`、10-bit 地址等通用标志；适配层必须在访问硬件前检查这些标志，不能把公共 flags 原样传给 LHAL。

本 app 覆盖以下可重复的交集：

| 模式 | 命令 | 覆盖内容 |
| --- | --- | --- |
| hw | `hw probe` | 指定 7-bit 地址单字节访问，确认 ACK/错误返回 |
| hw | `hw combined` | EEPROM 地址前缀 + read 的一次 repeated-start |
| hw | `hw boundary` | 1、4、31、32、33、255、256、1024 字节读边界 |
| hw | `hw eeprom` | 显式允许后备份、分页写入、整段读回逐字节比较、恢复和恢复校验 |
| hw | `hw concurrent` | 同一总线多个线程反复 combined read，检查数据没有交叉 |
| hw | `hw dual` | I2C0 与 I2C1 两个总线并行访问各自 target |

DMA、10-bit 地址、三段以上 repeated-start、读续读、总线复位不在本 P0 app 中伪装为已支持能力；相应参数会被拒绝或留给后续测试。适配层对非法频率、地址、flags 和长度的拒绝路径没有对应的实物总线现象，本 app 不覆盖。

Ai-M64P-32S-Kit 缺外接 target、上拉和逻辑分析仪，`hw` case 在该板上只记录完整
执行流程和判据，不标记为已实测 PASS。2026-10-13 起 `hw probe
--no-target` 已在 GPIO4/5 上用逻辑分析仪确认（I2C0、I2C1 各一次，见
`docs/bl616cl-i2c.md`）；I2C0 在 GPIO4/5 可正常启动。2026-10-14 起 `nsh-peripherals`
同时注册 I2C0（GPIO30/31）和 I2C1（GPIO4/5），两条总线的 `hw probe --no-target`
均 PASS；`hw dual` 仍需两路 target。2026-10-16 起
BL616CL-AVE-V10 的 `nsh-peripherals` 注册 I2C0（GPIO20/11，板载 ES8388，地址 0x10），
`hw probe`、`hw combined`、单寄存器 `hw eeprom`（单消息和 `--split-write`）和 `hw concurrent` 对真实从机 PASS，
命令和结果见 `docs/bl616cl-i2c.md` 的“正向 target”。ES8388 读不自动递增地址，
`hw boundary` 和多字节读仍要 EEPROM。

## 配置与构建

打开 `CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C` 前必须同时启用 `I2C`、`I2C_DRIVER` 和 `BL616CL_I2C`，并由板级选项注册总线（`AI_M64P_KIT_I2C0/1` 或 `BL616CL_AVE_V10_I2C0`），其它板上不编译本测试。默认配置为：

```text
CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C=y
CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_PROGNAME="mcu_i2c_test"
CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_THREADS=4
CONFIG_BL_MCU_PERIPHERAL_TESTS_I2C_ITERATIONS=32
```

完整 EEPROM 命令超过 NSH 默认 64 字节行长。启用本测试 app 的验证配置同时使用
`CONFIG_NSH_LINELEN=256`，否则命令会在解析前被控制台截断；这不是 I2C 数据长度配置。

板级 `AI_M64P_KIT_I2C` 总开关默认关闭；打开总开关后，I2C0 实例默认开启并使用
GPIO4/5，I2C1 实例默认关闭且配置为 GPIO12/13。该 Kconfig 默认值只描述通用板级
候选 pin，不表示当前模组可直接使用 GPIO4/5；当前模组必须关闭 I2C0、显式打开
I2C1，或在确认模组资源后另选无冲突 pin。配置和编译使用 SDK 标准入口：

```bash
vendor/bouffalolab/vela menuconfig \
  bl616cl/ai-m64p-32s-kit/configs/nsh
vendor/bouffalolab/vela build \
  bl616cl/ai-m64p-32s-kit/configs/nsh -j14
```

`/dev/ttyUSB2` 只用于烧录、复位和 NSH 日志。I2C 波形必须由独立逻辑分析仪测量；USB2 不是 I2C 分析仪。

## 通用运行流程

1. 上电后在 USB2 控制台确认 `NuttShell (NSH)` 和 `nsh>`，再执行 `ls /dev`，确认所选 `/dev/i2cN` 节点存在。
2. 先接 3.3 V 外部 I2C target（`hw probe --no-target` 除外）。AT24C02/AT24C32 常用地址为 `0x50`，SCL/SDA 各使用 2.2 kOhm 至 4.7 kOhm 上拉。
3. 记录命令、参数、完整 case 顺序、每次返回值、地址、频率、长度和错误码；逻辑分析仪同时记录 START、repeated-start、STOP、ACK/NACK 和实测 SCL。
4. 单个 case 结束必须看到一条最终 `I2C_TEST RESULT ... PASS/FAIL`，然后返回 `nsh>`；
   参数错误的 usage 也必须出现在该终态行之前。失败前的诊断行保留 bus、errno
   或 EEPROM restore 状态，最终 RESULT 不重复覆盖这些字段。`FAIL` 不能用
   “设备不存在”替代硬件证据。

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

`--no-target` 使用单字节 write 触发 BL616CL LHAL 的 NACK/timeout 路径；正向 target probe 仍使用单字节 read。该命令只有在指定地址返回冻结合同中的 `-ETIMEDOUT` 时 PASS；`-ENODEV`、`-EINVAL`、`-EOPNOTSUPP` 等本地参数或资源错误均 FAIL。如果意外收到 ACK 也 FAIL。地址必须由接线表确认不存在，且代码只允许普通地址范围 0x08..0x77，不能选择广播或保留地址。

I2C1-only 在未连接 target 的 GPIO12/13 上实测：

```text
nsh> mcu_i2c_test hw probe --bus 1 --addr 82 --no-target
I2C_TEST RESULT mode=hw case=probe PASS bus=1 addr=0x52 freq=100000 expected=no-target ret=-110
nsh>
```

该输出证明真实控制器超时从 LHAL toolchain errno 116 规范化为 OpenVela errno 110；
没有逻辑分析仪和上拉，不能据此判断实际 SCL 或 NACK 波形。

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

加 `--split-write` 时，每页写改为两条消息：偏移一条，数据一条并带 `I2C_M_NOSTART`，线上
字节与单消息写相同，用来覆盖驱动的 `NOSTART` 续写（NuttX `i2c set` 也这样写）。RESULT
行的 `write=split|single` 标明本次用的写法。

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
board=ai-m64p-32s-kit chip=bl616cl console=/dev/ttyUSB2 baud=2000000
case=hw combined command="..."
target=AT24C32 addr=0x50 pullup=4.7kOhm pins=SCL4/SDA5
software=PASS/FAIL ret=<value> first=<byte> last=<byte>
analyzer=SCL=<measured>Hz start=<count> repeated_start=<count> stop=<count>
ack=<address/data summary> nack=<address/data summary>
restore=not-required/verified/failed
```

无外接 target 时，`hw` 的失败只能作为“总线调用和错误路径已执行”的证据，不能宣称 I2C 外设功能通过。
