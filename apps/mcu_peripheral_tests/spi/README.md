# BL616CL SPI Master 测试

本目录提供一个 NSH 命令 `mcu_spi_test`，通过参数选择 SPI0 实物环回 case，不按
case 拆成多个 app。测试只通过 `/dev/spi0` 和标准 `SPIIOC_TRANSFER` 访问驱动，
判据是短接 MISO/MOSI 后的收发数据逐字节比对。测试 app 编译成独立的
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

因此测试不得把这些项目写成已支持。Ai-M64P-32S-Kit 的 SPI1 四线 pin 和模组引出
尚未冻结，板级不注册 `/dev/spi1`，本测试只覆盖 SPI0。

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
CONFIG_BL616CL_SPI0=y
CONFIG_AI_M64P_KIT_SPI=y           # 只注册所选的 SPI0 board 节点
CONFIG_BL_MCU_PERIPHERAL_TESTS_SPI=y
CONFIG_BL_MCU_PERIPHERAL_TESTS_SPI_PROGNAME="mcu_spi_test"
```

`CONFIG_BL_MCU_PERIPHERAL_TESTS_SPI` 依赖 `BL616CL_SPI0`，并选择 `SPI_BITORDER`
供 `--lsb` 使用。正式产品配置应关闭该 app。

构建使用 `nsh-peripherals`：

```bash
vendor/bouffalolab/vela clean \
  bl616cl/ai-m64p-32s-kit/configs/nsh-peripherals
vendor/bouffalolab/vela build \
  bl616cl/ai-m64p-32s-kit/configs/nsh-peripherals -j14
```

裁剪检查使用 `ar t` 确认对象归属，使用 RISC-V `nm` 检查最终 ELF：

```bash
ar t cmake_out/ai-m64p-32s-kit_nsh/arch/libarch.a \
  | grep -Fx bl616cl_spi.c.o
ar t cmake_out/ai-m64p-32s-kit_nsh/boards/libboard.a \
  | grep -Fx ai_m64p_kit_spi.c.o
ar t cmake_out/ai-m64p-32s-kit_nsh/apps/vendor/bouffalolab/libbl_lhal.a \
  | grep -Fx bflb_spi.c.o
ar t cmake_out/ai-m64p-32s-kit_nsh/apps/vendor/bouffalolab/apps/mcu_peripheral_tests/spi/libapps_mcu_spi_test.a \
  | grep -Fx spi_test_main.c.o
prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf-nm \
  cmake_out/ai-m64p-32s-kit_nsh/final_nuttx \
  | grep -E 'g_bl616cl_spi[01]|mcu_spi_test_main'
```

## 命令入口

```text
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

流程：打开 `/dev/spi0`，生成确定性 pattern，执行一段 `SPIIOC_TRANSFER`，再逐字节
比较 TX/RX。PASS 必须同时满足 ioctl 成功、数据完全一致。分别覆盖 mode 0..3、8/16-bit、100 kHz/400 kHz/1 MHz 和
MSB/LSB；分析仪确认实际频率不高于请求值以及 CPOL/CPHA、bit order 正确。

### `hw paths`

```text
mcu_spi_test hw paths --bus 0 --freq 400000 --mode 0 --bits 8 --length 33 --target 0
```

依次执行 full-duplex、TX-only 和 RX-only。full-duplex 必须逐字节回环；TX-only
（`rxbuffer` 为 NULL）本身没有回读数据，它的波形由逻辑分析仪在 MOSI/CLK 上确认；
RX-only 由 controller 发送 filler，短接下应全部读回 `0xff`，TX-only 若在 RX FIFO
留下数据，这里会读到旧数据而 FAIL。任一段失败则整个 case FAIL。

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
共 48 次环回。每一行都必须 PASS；测试中出现 assert、panic、hang 或数据错位都判
失败。随后以 `--lsb` 对同一集合重跑，补齐 bit order 边界。

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

SPI 测试固件上其它外设的回归关键结果：

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

2026-10-13 已在 Ai-M64P-32S-Kit 上短接 GPIO18-19 完成 SPI0 实物验证：`hw boundary`
MSB/LSB 共 96 组、`hw loopback`（100 kHz、1 MHz、mode 1..3）、`hw paths`、
`hw sequence` 全部 PASS；断开短接时 `hw loopback` 按预期 FAIL。逻辑分析仪确认了
MOSI 方向、sequence 的 CS 保持/释放、实际频率和 CPOL，数据见
`docs/bl616cl-spi.md` 的“实物实测数据”。同日补测 mode 0..3 × MSB/LSB 的 CPHA 和
bit order，波形与请求一致；SPI1 的四线 pin、板级注册和实物验证尚未冻结。OpenVela
`spi_transfer()` 不能传播 `exchange(void)` 内部错误，timeout、配置失败或 target
选择失败时 ioctl 仍可能返回成功；这类传输不会正确写入 RX，因此测试以数据比对判定，
不依赖 ioctl 返回值推断 lower-half 结果。
