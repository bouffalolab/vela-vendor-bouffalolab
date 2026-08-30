# BL616CL SPI Master 测试

本目录提供一个 NSH 命令 `mcu_spi_test`。同一个 `main` 通过参数选择 fake
软件合同测试和实物 SPI0 环回测试，不按 case 拆成多个 app。测试 app 编译成独立的
`libapps_mcu_spi_test.a`；chip adapter 在 `libarch.a`，board 注册在
`libboard.a`，同步驱动仓的 LHAL 在 `libbl_lhal.a`。

## 背景与边界

BL616CL 有 SPI0、SPI1 两个独立控制器。当前 OpenVela 适配开放 polling master、
mode 0..3、8/16-bit、实际频率返回、MSB/LSB、TX-only、RX-only、full-duplex、
GPIO CS、多 target、sequence CS、同总线互斥和双实例并发。

LHAL 还暴露 24/32-bit、IRQ、DMA、trigger 和 byte order，但这些能力没有形成与
OpenVela 公共 SPI ABI 一致且可验证的闭环：

- 24/32-bit 与 OpenVela `nwords` 缓冲区步长不一致；
- IRQ/DMA 只有局部原语，没有完整完成、中止、cache 和恢复合同；
- `SPI_DELAY_CONTROL`、`SPI_CMDDATA`、`SPI_TRIGGER` 当前返回 `-ENOSYS`；
- byte order 没有独立公共配置入口。

因此测试不得把这些项目写成已支持。SPI1 lower-half 可独立编译和执行 fake 测试，
但 Ai-M64L-32S-Kit 的 SPI1 四线 pin 和模组引出尚未冻结，不注册 `/dev/spi1`，
也不得把 bus 1 fake PASS 解释为 SPI1 实物总线通过。

SPI0 当前板级映射如下：

| 信号 | GPIO | 所有权与限制 |
|---|---:|---|
| CS0 | 12 | 默认低有效；与 I2C1 默认 pin 冲突 |
| CLK | 13 | SPI0 signal 1；与 I2C1 默认 pin 冲突 |
| MISO | 18 | SPI0 signal 2 |
| MOSI | 19 | SPI0 signal 3 |
| CS1 | 20 | 仅启用第二 target 时使用 |

开启 board SPI0 后，这些 pin 从 GPIO 测试表移除，避免同一 pin 同时被 GPIO、I2C
和 SPI 驱动。Flash、USB、UART0 和 boot pin 在编译期拒绝作为 SPI0 pin。

## 配置与构建

测试 app 的主要配置为：

```text
CONFIG_BL616CL_SPI=y
CONFIG_BL616CL_SPI0=y              # SPI0 测试配置
CONFIG_BL616CL_SPI1=y              # SPI1/dual 测试配置
CONFIG_AI_M64L_KIT_SPI=y           # 只注册所选的 SPI0 board 节点
CONFIG_BL_MCU_PERIPHERAL_TESTS_SPI=y
CONFIG_BL_MCU_PERIPHERAL_TESTS_SPI_PROGNAME="mcu_spi_test"
CONFIG_BL_MCU_PERIPHERAL_TESTS_SPI_ITERATIONS=32
```

`CONFIG_BL_MCU_PERIPHERAL_TESTS_SPI` 会选择 test-only transport/diagnostic hook 和
`SPI_BITORDER`。正式产品配置应关闭该 app；关闭后不会保留 fake 注入入口和测试命令。

在 SDK 根目录执行 fresh build，目标依次替换为下面七项：

```bash
python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh-spi-dual-test
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh-spi-dual-test -j14
```

```text
nsh
nsh-spi0
nsh-spi0-test
nsh-spi1
nsh-spi1-test
nsh-spi-dual
nsh-spi-dual-test
```

2026-08-30 fresh clean build 和裁剪数据：

| 配置 | 构建 | arch SPI | board SPI0 | LHAL SPI | test app | ELF SPI0/SPI1 | `nuttx.bin` |
|---|---:|---:|---:|---:|---:|---|---:|
| `nsh` | 1224/1224 | 0 | 0 | 0 | 0 | 0/0 | 479632 B |
| `nsh-spi0` | 1234/1234 | 1 | 1 | 1 | 0 | 1/0 | 493312 B |
| `nsh-spi0-test` | 1236/1236 | 1 | 1 | 1 | 1 | 1/0 | 514448 B |
| `nsh-spi1` | 1232/1232 | 1 | 0 | 1 | 0 | 0/0 | 479632 B |
| `nsh-spi1-test` | 1234/1234 | 1 | 0 | 1 | 1 | 0/1 | 510624 B |
| `nsh-spi-dual` | 1234/1234 | 1 | 1 | 1 | 0 | 1/1 | 494016 B |
| `nsh-spi-dual-test` | 1236/1236 | 1 | 1 | 1 | 1 | 1/1 | 515360 B |

`nsh-spi1` 中 chip/LHAL 对象进入 archive，但没有 board 或 app consumer，最终 ELF
通过 archive selection/section GC 移除全部 SPI1 代码；这不是 SPI1 lower-half 未编译。
`nsh-spi1-test` 由测试 app 引用实例 1，最终 ELF 才保留该实例。

对应 SHA256：

```text
nsh                 8fa89db568cff6dc13e0b4c325ee4eb929eeacff4496936fe4630fc5403e7eb7
nsh-spi0            5533d0417dd5a529190ece9b9d177f95da96fbda2db88966dd7e310cc7749e04
nsh-spi0-test       53729fa86c06dc139574481418d4ee4820b69ff337b48088345b422b50b18428
nsh-spi1            5067b5024900bfc9fffffe9a04dc2e35428f54ed191b7f2571b3321270a9f072
nsh-spi1-test       b252dad6312ac3c0a1bb0317ff67f73c9b5ae1878fafc07283e325664f1366ba
nsh-spi-dual        0e76cffd096c1f5d01f7c97b7f65a85584739e7cb1b848227cac8ba7a1c3dc1b
nsh-spi-dual-test   e252b9dfa4782c221166af959073c60c145f2121395421c1ccc499d732e3b48d
```

裁剪检查使用 `ar t` 确认对象归属，使用 RISC-V `nm` 检查最终 ELF：

```bash
ar t cmake_out/ai-m64l-32s-kit_nsh-spi-dual-test/arch/libarch.a \
  | grep -Fx bl616cl_spi.c.o
ar t cmake_out/ai-m64l-32s-kit_nsh-spi-dual-test/boards/libboard.a \
  | grep -Fx ai_m64l_kit_spi.c.o
ar t cmake_out/ai-m64l-32s-kit_nsh-spi-dual-test/apps/vendor/bouffalolab/libbl_lhal.a \
  | grep -Fx bflb_spi.c.o
ar t cmake_out/ai-m64l-32s-kit_nsh-spi-dual-test/apps/vendor/bouffalolab/apps/mcu_peripheral_tests/spi/libapps_mcu_spi_test.a \
  | grep -Fx spi_test_main.c.o
prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf-nm \
  cmake_out/ai-m64l-32s-kit_nsh-spi-dual-test/final_nuttx \
  | grep -E 'g_bl616cl_spi[01]|mcu_spi_test_main'
```

## 命令入口

```text
mcu_spi_test fake <lifecycle|config|exchange|sequence|errors|concurrent|all> --bus N
mcu_spi_test fake dual
mcu_spi_test hw <loopback|paths|sequence|boundary> [options]
```

硬件选项：

```text
--bus N       bus 编号，默认 0
--freq HZ     请求频率，默认 400000
--mode N      mode 0..3，默认 0
--bits N      8 或 16，默认 8
--length N    word 数，1..4096，默认 32
--target N    SPIDEV_USER index，默认 0
--lsb         使用 LSB first；省略时为 MSB first
```

## fake case 完整流程

fake transport 替换 select/config/exchange/recovery 的传输阶段，不访问 GPIO pin 或
外部 target；test-owned 实例在安装 fake 前仍执行 controller clock、init，并在结束时
deinit。它用于证明 OpenVela lower-half 的软件合同、错误抑制和并发边界，不能替代
CLK/CS/MOSI/MISO 波形。

### `fake lifecycle`

命令：

```text
mcu_spi_test fake lifecycle --bus 0
```

流程和判据：

1. 取得指定实例，确认 test device 与 lower-half 指针一致。
2. 对 bus `-1`、bus `2` 初始化，必须返回 `-ENODEV`；对空 device uninitialize，
   必须返回 `-EINVAL`。
3. 对已由 board 注册的 bus 增加引用；对 test-owned bus 再次 initialize。
4. 释放一个引用后 device 必须仍存在，证明引用计数没有提前销毁实例。
5. 读取 diagnostic，默认必须是 mode 0、8-bit。
6. 移除并重新安装 fake transport；任一步失败则 case FAIL。

### `fake config`

命令：

```text
mcu_spi_test fake config --bus 0
```

流程和判据：

1. 依次设置 mode 0、1、2、3，每次执行一笔 exchange，四个 mode 都必须进入 transport。
2. 分别设置 8-bit、16-bit并 exchange，两种 word width 都必须被观察到。
3. 请求 100 kHz、1 MHz、1.1 MHz；返回值必须非零且不得高于请求频率。
4. 请求 1 Hz 必须返回 0 并记录 `-EIO`；请求 0 Hz 必须返回 0 并记录 `-EINVAL`。
5. 请求 `UINT32_MAX` 必须返回可实现频率，diagnostic 的 actual frequency 与返回值一致。
6. 注入 feature `-EIO` 后设置 mode 1；后续 exchange 必须被抑制，transport
   exchange 计数不得增加，原始 `-EIO` 不得被覆盖。
7. 清除注入并重新设置 mode 1，随后验证 LSB first 和恢复 MSB first。
8. mode 4、bits 0/7/24/32 必须被拒绝；最后有效状态仍为 mode 1、16-bit。
9. mode、bits、frequency、bit-order 覆盖 mask 必须完整，否则 FAIL。

### `fake exchange`

命令：

```text
mcu_spi_test fake exchange --bus 0
```

流程和判据：

1. `nwords=0` 不得调用 transport。
2. 8-bit full-duplex 依次传输 1、31、32、33、128 words，逐字节比较 TX/RX。
3. 执行 TX-only；随后执行 RX-only，fake filler 必须全部为 `0xff`。
4. 执行 TX/RX 都为空的一字节 exchange，确认该公共路径不会误访问 buffer。
5. 切换 16-bit，传输四个 word，并用 `SPI_SEND()` 验证 `0xa55a` 回读。
6. 对 16-bit 使用奇地址 buffer，必须在 transport 前记录 `-EINVAL`，exchange
   计数不得增加。
7. 恢复 8-bit 后下一笔 exchange 必须 PASS，diagnostic 恢复为 0。

### `fake sequence`

命令：

```text
mcu_spi_test fake sequence --bus 0
```

流程和判据：

1. 构造两段 `spi_sequence_s`。第一段 `deselect=false` 保持 CS，第二段
   `deselect=true` 释放 CS，两个 RX word 必须等于 TX。
2. 将第一段改为 `deselect=true` 重跑，两段结束都释放 CS；select/deselect
   计数必须与两种 sequence policy 匹配。
3. 连续两次选中和释放 target 1，确认同一 target 的 CS 调用保持可重复。
4. 对未知 target 执行 sequence。OpenVela upper-half 可能返回 OK，但 lower-half
   diagnostic 必须是 `-ENODEV`，且 transport exchange 计数不得增加。
5. 重新选择 target 0 并释放，确认 selection error 不阻塞后续合法 target。

### `fake errors`

命令：

```text
mcu_spi_test fake errors --bus 0
```

流程和判据：

1. 注入 LHAL/newlib `-116` timeout；adapter 必须规范化为 OpenVela
   `-ETIMEDOUT=-110`，error count 加一，recovery count 为一。
2. 清除故障，下一笔 exchange 必须回读原字节，diagnostic 恢复为 0。
3. 注入 `-EIO`；必须保留 `-EIO`，error count 再加一，累计 recovery count 为二。
4. 再次清除故障，下一笔 exchange 必须 PASS。该结果只证明 recovery hook 与
   下一笔软件传输，不证明实物 BUSY/INTSTS/FIFO/CS 已恢复。

### `fake concurrent`

命令：

```text
mcu_spi_test fake concurrent --bus 0
```

流程和判据：

1. 创建两个线程并用 semaphore 同时放行。
2. 每个线程循环 32 次；线程 0 使用 mode 0、8-bit、100 kHz，线程 1 使用
   mode 3、16-bit、1 MHz。
3. 每轮显式 `SPI_LOCK`，设置配置，执行 16-byte exchange，再解锁并比较数据。
4. 总 exchange 数必须是 64，单实例 transport 的 `max_active` 必须是 1；两组
   mode/bits/frequency mask 都必须出现。

### `fake all`

命令：

```text
mcu_spi_test fake all --bus 0
mcu_spi_test fake all --bus 1
```

按 lifecycle、config、exchange、sequence、errors、concurrent 顺序执行。前一项
FAIL 后不再把后续项伪报为 PASS；最终必须显示 `cases=6` 和 `result=PASS`。

### `fake dual` 与非法 bus

命令：

```text
mcu_spi_test fake dual
mcu_spi_test fake all --bus 2
```

`fake dual` 为 SPI0、SPI1 分别安装 transport，各创建一个 worker 并同时放行。
每个实例内部仍由自己的 mutex 串行，但跨实例 tracker 的 `overlap` 必须为 2，证明
两个控制器的软件状态和锁互不串扰。bus 2 必须显示 `reason=no-device` 并以失败退出。
单实例固件执行 `fake dual` 必须正常返回 FAIL，不得 assert、panic 或销毁未初始化
mutex；该负测用于确认缺少另一个实例时的清理边界。

## 2026-08-30 USB2 软件实测

环境：Ai-M64L-32S-Kit、CH340 `1a86:7523`、`/dev/ttyUSB2`、2,000,000 baud。
先烧录 `nsh-spi1-test`，固件 SHA256 为
`b252dad6312ac3c0a1bb0317ff67f73c9b5ae1878fafc07283e325664f1366ba`，验证单实例
失败清理：

```text
nsh> mcu_spi_test fake dual
SPI_TEST fake dual result=FAIL overlap=0
nsh> echo $?
1
nsh> echo ST023_SPI1_SINGLE_ALIVE
ST023_SPI1_SINGLE_ALIVE
```

该负测后控制台继续响应，没有 assert、panic、reset 或 hang。随后烧录
`nsh-spi-dual-test`，固件 SHA256 为
`e252b9dfa4782c221166af959073c60c145f2121395421c1ccc499d732e3b48d`。启动后出现
`NuttShell (NSH)` 和 `nsh>`；`help` 列出 `mcu_spi_test`。当前 board 只注册 SPI0，
因此 `/dev/spi0` 存在，`/dev/spi1` 返回 `stat failed: 2`，符合设计边界。

运行命令和完整关键结果：

```text
nsh> ls /dev/spi0
 /dev/spi0
nsh> ls /dev/spi1
nsh: ls: stat failed: 2

nsh> mcu_spi_test fake all --bus 0
SPI_TEST fake lifecycle bus=0 result=PASS features=0 exchanges=0 recoveries=0
SPI_TEST fake config bus=0 result=PASS features=15 exchanges=11 recoveries=0
SPI_TEST fake exchange bus=0 result=PASS features=4 exchanges=11 recoveries=0
SPI_TEST fake sequence bus=0 result=PASS features=0 exchanges=4 recoveries=0
SPI_TEST fake errors bus=0 result=PASS features=0 exchanges=4 recoveries=2
SPI_TEST fake concurrent bus=0 result=PASS features=190 exchanges=64 recoveries=0
SPI_TEST fake all bus=0 result=PASS cases=6

nsh> mcu_spi_test fake all --bus 1
SPI_TEST fake lifecycle bus=1 result=PASS features=0 exchanges=0 recoveries=0
SPI_TEST fake config bus=1 result=PASS features=15 exchanges=11 recoveries=0
SPI_TEST fake exchange bus=1 result=PASS features=4 exchanges=11 recoveries=0
SPI_TEST fake sequence bus=1 result=PASS features=0 exchanges=4 recoveries=0
SPI_TEST fake errors bus=1 result=PASS features=0 exchanges=4 recoveries=2
SPI_TEST fake concurrent bus=1 result=PASS features=190 exchanges=64 recoveries=0
SPI_TEST fake all bus=1 result=PASS cases=6

nsh> mcu_spi_test fake dual
SPI_TEST fake dual result=PASS overlap=2

nsh> mcu_spi_test fake all --bus 2
SPI_TEST fake FAIL bus=2 reason=no-device
```

上述结果证明两个 lower-half 的软件合同、实例隔离和错误诊断。USB2 仅是控制台，
不经过 SPI pin，不能证明 SPI0/SPI1 的实际数据、频率、mode 或 CS 波形。

## 实物 SPI0 环回准备

实物测试前必须记录板卡/模组版本、固件 SHA256、GPIO 映射、短接方式、供电和逻辑
分析仪通道。断电后短接 GPIO19(MOSI) 到 GPIO18(MISO)，保持 GPIO13(CLK) 和
GPIO12(CS0) 不短接到其它信号。上电后确认 `/dev/spi0` 存在。

逻辑分析仪至少连接 CLK、CS0、MOSI、MISO 和 GND。解码设置必须与命令的 mode、
bit order 和 word width 一致。没有分析仪时只能证明环回数据，不声明频率、mode 或
CS 波形通过。

### `hw loopback`

```text
mcu_spi_test hw loopback --bus 0 --freq 100000 --mode 0 --bits 8 --length 32 --target 0
```

流程：打开 `/dev/spi0`，生成确定性 pattern，执行一段 `SPIIOC_TRANSFER`，读取
lower diagnostic，再逐字节比较 TX/RX。PASS 必须同时满足 ioctl 成功、diagnostic
为 0、数据完全一致。分别覆盖 mode 0..3、8/16-bit、100 kHz/400 kHz/1 MHz 和
MSB/LSB；分析仪确认实际频率不高于请求值以及 CPOL/CPHA、bit order 正确。

### `hw paths`

```text
mcu_spi_test hw paths --bus 0 --freq 400000 --mode 0 --bits 8 --length 33 --target 0
```

依次执行 full-duplex、TX-only 和 RX-only。full-duplex 必须逐字节回环；TX-only
必须无 diagnostic；RX-only 由 controller 发送 filler，短接下应全部读回 `0xff`。
每段都重新检查 diagnostic，任一段失败则整个 case FAIL。

### `hw sequence`

```text
mcu_spi_test hw sequence --bus 0 --freq 400000 --mode 0 --bits 8 --target 0
```

第一轮两段 transfer 让第一段保持 CS、第二段释放；第二轮让两段各自释放。两轮数据
都必须环回。分析仪必须确认第一轮中间没有 CS inactive edge，第二轮两段之间出现
一次释放；只有终端 PASS 不能单独证明 CS 波形。

### `hw boundary`

```text
mcu_spi_test hw boundary --bus 0 --freq 400000 --target 0
```

自动执行 mode 0..3、8/16-bit 和 1、31、32、33、256、1024 words 的笛卡尔积，
共 48 次环回。每一行都必须 PASS；测试中出现 assert、panic、hang、数据错位或
diagnostic 非零都判失败。随后以 `--lsb` 对同一集合重跑，补齐 bit order 边界。

## 实物验收顺序

1. 记录固件 hash、board pin 和接线，确认 SPI0 独占 GPIO12/13/18/19。
2. 确认 `/dev/spi0` 存在、`/dev/gpio19` 不存在；检查启动无 SPI 初始化错误。
3. 运行 mode 0、8-bit、100 kHz 的单次 loopback，先验证最小正向路径。
4. 运行 `hw paths`，确认 full-duplex、TX-only、RX-only。
5. 运行 `hw sequence`，同时保存 CS/CLK/MOSI/MISO 解码结果。
6. 运行 `hw boundary`，再以 LSB first 重跑；保存 100 kHz、400 kHz、1 MHz 的
   实际频率以及 mode 0..3 的 CPOL/CPHA。
7. 断开 MOSI-MISO 短接后运行一笔 loopback，必须 FAIL；恢复短接后必须再次 PASS。
8. 重跑 GPIO 可用节点、timer 001/002/005、WDT 002/003、oneshot 和 RTC；最后确认
   `nsh>` 存活且无 assert、panic 或 watchdog reset。

## 已完成回归与当前限制

dual-test 固件上的回归关键结果：

```text
[TIMER-001] PASS accuracy within tolerance
RESULT max_err=346.0us (0.346%) tol=500.0us (0.50%)
[TIMER-002] PASS prescaler takes effect
ratio period_b/period_a=2.001 (expected 2.000, tol +/-5%)
[TIMER-005] PASS rejected requests preserved state; live update fired; lifecycle recovered
PASS: fed 9 times over 9017ms, no reset; watchdog stopped
PASS: invalid/live changes rejected; duplicate lifecycle preserved state
Starting oneshot timer with delay 100000 microseconds
Finished
 /dev/rtc0
Mon, Jan 01 00:00:13 2018
```

当前没有 MOSI-MISO 安全短接和逻辑分析仪证据，因此所有 `hw` case 尚未记录实测
PASS。SPI0 的 controller、pinmux、CS、数据和波形仍待实物闭环；SPI1 的四线 pin、
板级注册和实物验证也尚未冻结。OpenVela `spi_transfer()` 不能传播 `exchange(void)`
内部错误，当前测试通过 chip diagnostic 读取 timeout/config/select 结果；普通 consumer
不能据 ioctl 返回值推断所有 lower-half 错误。
