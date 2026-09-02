# BL616CL SPI0/SPI1 Master 适配与验证

本文说明 BL616CL SPI master 与 OpenVela SPI 子系统的最大能力交集、实现归属、
Kconfig 裁剪、构建证据、软件实测和实物验收限制。当前已完成 SPI0/SPI1 lower-half、
七类裁剪构建和 USB2 fake 软件合同；SPI0 环回与波形、SPI1 pin 和板级注册仍待实物
资源冻结，因此能力声明限定在已有证据范围内。

## 背景

BL616CL LHAL 有 SPI0/SPI1 两个控制器，实现同步 polling exchange，并暴露 mode、
data width、bit/byte order、FIFO、部分 IRQ/DMA 控制。此前 OpenVela 没有 BL616CL
SPI lower-half、board pin/CS owner 或 `/dev/spiN` 注册。

本适配不修改 CI 同步的 `vendor/bouffalolab/drivers/`。chip adapter 翻译 OpenVela
SPI ABI，board 代码只接管从同源 BL616CLDK 选择的 SPI0 候选 pin 和 GPIO CS，
父仓 CMake wrapper 按实例选择 LHAL `bflb_spi.c`。该候选 pinmux 仍待 Ai-M64L
环回和波形闭环；SPI1 完整四线 pin 也未经当前板卡证实，故只开放 lower-half 和
软件测试，不创建虚假的 board 节点。

## 最大能力交集

| 能力 | 当前结论 | 依据和边界 |
|---|---|---|
| SPI0/SPI1 master | 纳入 | 独立 base、clock/reset、device、私有状态和 mutex |
| polling | 纳入 | LHAL `bflb_spi_poll_exchange()` 是当前完整同步路径 |
| mode 0..3 | 纳入 | OpenVela 与 LHAL 语义可直接映射；fake 全覆盖 |
| 8/16-bit | 纳入 | 公共 `nwords` 与 LHAL byte count 可无歧义换算 |
| 实际频率 | 纳入 | 基于 peripheral clock/divider 向下取整，返回不高于请求值 |
| MSB/LSB | 纳入 | `SPI_HWFEATURES(HWFEAT_LSBFIRST)`；独立 Kconfig 裁剪 |
| TX/RX/full-duplex | 纳入 | 空 buffer 由 LHAL polling 路径处理；fake 已覆盖 |
| GPIO CS | 纳入 SPI0 | 默认低有效，可选高有效，CS inactive latch 先于 output enable |
| 多 target | 纳入 SPI0 | target index 与 GPIO 独立；未知 target 抑制 exchange |
| sequence CS | 纳入 | 支持 transfer 间保持或释放 CS；fake 已验证调用合同 |
| 同总线互斥 | 纳入 | 一把实例 mutex 保护 consumer 的完整配置和 exchange 序列 |
| 双实例并发 | 纳入软件合同 | 独立 mutex；fake `overlap=2`，实物双实例待 SPI1 pin |
| 24/32-bit | 排除首版 | OpenVela buffer word 步长与 LHAL 3/4-byte word 不一致 |
| byte order | 排除首版 | 公共 ABI 无独立入口；当前固定为 LHAL `SPI_BYTE_LSB` |
| IRQ | 后续项 | 有 IRQ/FIFO 原语，无完整 ISR、完成和错误恢复合同 |
| DMA/trigger | 后续项 | 缺 channel、cache、完成、中止和实物验证闭环 |
| delay/cmd-data | 后续项 | 公共可选 ABI 当前明确返回 `-ENOSYS` |
| SPI slave | 排除 | 本任务和当前 OpenVela lower-half 只适配 master |

完整枚举不等于一次性打开全部能力。当前实现选择可裁剪且有明确软件/实物判据的
polling 子集；IRQ/DMA 等保留为独立后续项，不把“尚未适配”误写为硬件不支持。

## 调用链与代码归属

```text
board_late_initialize()
  -> bl616cl_board_initialize()
  -> ai_m64l_kit_spi_initialize()
  -> pin owner + GPIO CS inactive level
  -> bl616cl_spi_configure_pins()
  -> bl616cl_spibus_initialize(0, board_ops, board_arg)
  -> spi_register(spi, 0)
  -> /dev/spi0

SPIIOC_TRANSFER
  -> OpenVela spi upper-half
  -> lock + mode/bits/frequency/hwfeatures
  -> board select(devid, true)
  -> bl616cl_spi_exchange()
  -> bflb_spi_poll_exchange()
  -> board select(devid, false) + unlock
```

代码和 archive 归属：

- `chips/bl616cl/bl616cl_spi.c`：实例、clock、配置、exchange、mutex、recovery 和
  test diagnostic，编入 `libarch.a`；
- `boards/bl616cl/ai-m64l-32s-kit/src/ai_m64l_kit_spi.c`：SPI0 pin、target、GPIO CS
  和 `/dev/spi0` 注册，编入 `libboard.a`；
- `cmake/bl616cl_lhal.cmake`：有任一实例时编入同步仓 `bflb_spi.c`，生成
  `libbl_lhal.a`；
- `apps/mcu_peripheral_tests/spi/`：单一测试 main，独立生成
  `libapps_mcu_spi_test.a`；
- `drivers/`：保持未修改；chip 代码不放入 `libbl_std.a`。

## Kconfig 与资源所有权

| 选项 | 作用 | 关闭效果 |
|---|---|---|
| `BL616CL_SPI` | chip 总开关，选择 OpenVela SPI/exchange/bit-order 能力 | 无实例时不编 chip/LHAL SPI 对象 |
| `BL616CL_SPI0/1` | 独立 controller 实例 | 只生成所选实例私有状态 |
| `AI_M64L_KIT_SPI` | board SPI 总开关 | 不接管 pin、不注册节点 |
| `AI_M64L_KIT_SPI0` | SPI0 pin、target、GPIO CS 和 `/dev/spi0` | 关闭后无 board SPI 对象/节点 |
| `AI_M64L_KIT_SPI0_TARGET1` | 第二个 GPIO CS target | 关闭后无 target 1 数据和 pin owner |
| `BL616CL_SPI_TEST` | chip fake transport/diagnostic | 正式产品无注入接口 |
| `BL_MCU_PERIPHERAL_TESTS_SPI` | 测试命令 | 无测试 archive、命令和字符串 |

SPI0 默认 GPIO12=CS、GPIO13=CLK、GPIO18=MISO、GPIO19=MOSI。board Kconfig
禁止与 I2C1 同时启用；编译期还检查 signal 编号、pin 唯一性、保留 pin 和第二 CS
冲突。pinmux 配置会先清全局 MISO/MOSI swap，再按每个 GPIO group 的 signal 2/3
选择设置局部 swap，避免 Kconfig 接受的替代组合被路由到错误信号。

SPI1 只有 chip 开关，没有 Ai-M64L board 开关。这保证未知 pin 不会被软件占用，
同时允许独立构建和 fake 验证 lower-half。

## 频率、错误与恢复合同

请求频率通过 peripheral clock 和 8-bit divider 计算，actual frequency 向下取整并由
`SPI_SETFREQUENCY()` 返回。0 Hz 返回 0 并记录 `-EINVAL`；低于 divider 可实现下限
返回 0 并记录 `-EIO`。配置失败会设置 config error，后续 exchange 被抑制，直到同一
配置项成功写入；这样不会用一笔无效配置继续访问硬件。

LHAL/newlib 的 `ETIMEDOUT=116`，OpenVela app 的 `ETIMEDOUT=110`。chip adapter 在
跨 archive 边界时将 `-116` 规范化为 `-110`，其它 errno 原样保留。exchange 错误后
当前 recovery 清 TX/RX FIFO、deinit/init 并恢复最后有效配置。

OpenVela SPI `exchange` 回调返回 `void`，`spi_transfer()` 也不读取 chip diagnostic。
因此 ioctl 可能在 timeout、未知 target 或配置失败时返回 OK。测试 app 在每笔硬件
ioctl 后读取 test-only diagnostic，避免把 upper-half 返回值误当成 lower-half 成功。
普通 consumer 当前没有等价的同步错误传播合同，这是独立上游改进项。

当前 recovery 只由 fake 证明 hook 被调用且下一笔可成功；没有实物 BUSY、INTSTS、
FIFO 和 CS 故障注入，不能宣称完整 controller recovery 已通过。

## 构建和裁剪实测

以下历史矩阵来自已删除的一次性临时配置；复测时从 `nsh-spi` 派生，验证后删除临时目录。

在 SDK 根目录对 `nsh`、`nsh-spi`、`nsh`、`nsh`、
`nsh`、`nsh`、`nsh` 分别执行：

```bash
python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/<config>
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/<config> -j14
```

2026-08-30 fresh build 数据：

| 配置 | 构建 | chip/board/LHAL/test 对象 | ELF SPI0/SPI1 | 大小 | SHA256 |
|---|---:|---|---|---:|---|
| `nsh` | 1224/1224 | 0/0/0/0 | 0/0 | 479632 | `8fa89db568cff6dc13e0b4c325ee4eb929eeacff4496936fe4630fc5403e7eb7` |
| `nsh-spi` | 1234/1234 | 1/1/1/0 | 1/0 | 493312 | `5533d0417dd5a529190ece9b9d177f95da96fbda2db88966dd7e310cc7749e04` |
| `nsh` | 1236/1236 | 1/1/1/1 | 1/0 | 514448 | `53729fa86c06dc139574481418d4ee4820b69ff337b48088345b422b50b18428` |
| `nsh` | 1232/1232 | 1/0/1/0 | 0/0 | 479632 | `5067b5024900bfc9fffffe9a04dc2e35428f54ed191b7f2571b3321270a9f072` |
| `nsh` | 1234/1234 | 1/0/1/1 | 0/1 | 510624 | `b252dad6312ac3c0a1bb0317ff67f73c9b5ae1878fafc07283e325664f1366ba` |
| `nsh` | 1234/1234 | 1/1/1/0 | 1/1 | 494016 | `0e76cffd096c1f5d01f7c97b7f65a85584739e7cb1b848227cac8ba7a1c3dc1b` |
| `nsh` | 1236/1236 | 1/1/1/1 | 1/1 | 515360 | `e252b9dfa4782c221166af959073c60c145f2121395421c1ccc499d732e3b48d` |

`nsh` 的 archive 含 chip/LHAL 对象，但最终 ELF 没有 consumer，故链接裁剪为
与 `nsh` 相同大小；`nsh` 由测试 app 引用后保留实例 1。这组对照同时证明
源码选择和最终链接裁剪，不用“archive 中有对象”替代“产品镜像中存在”。

## USB2 软件实测

运行环境：Ai-M64L-32S-Kit、CH340 `1a86:7523`、`/dev/ttyUSB2`、2 Mbps。
`nsh` 固件 SHA256 为
`b252dad6312ac3c0a1bb0317ff67f73c9b5ae1878fafc07283e325664f1366ba`；单实例执行
`fake dual` 返回 `FAIL overlap=0` 和退出码 1，随后 alive 命令退出码为 0，全程没有
assert、panic、reset 或 hang，证明缺少另一实例时清理边界安全。

`nsh` 固件 SHA256 为
`e252b9dfa4782c221166af959073c60c145f2121395421c1ccc499d732e3b48d`。启动出现
`NuttShell (NSH)` 和 `nsh>`，`help` 可见 `mcu_spi_test`；`/dev/spi0` 存在，
`/dev/spi1` 不存在。

```text
mcu_spi_test fake all --bus 0
SPI_TEST fake lifecycle bus=0 result=PASS features=0 exchanges=0 recoveries=0
SPI_TEST fake config bus=0 result=PASS features=15 exchanges=11 recoveries=0
SPI_TEST fake exchange bus=0 result=PASS features=4 exchanges=11 recoveries=0
SPI_TEST fake sequence bus=0 result=PASS features=0 exchanges=4 recoveries=0
SPI_TEST fake errors bus=0 result=PASS features=0 exchanges=4 recoveries=2
SPI_TEST fake concurrent bus=0 result=PASS features=190 exchanges=64 recoveries=0
SPI_TEST fake all bus=0 result=PASS cases=6

mcu_spi_test fake all --bus 1
SPI_TEST fake lifecycle bus=1 result=PASS features=0 exchanges=0 recoveries=0
SPI_TEST fake config bus=1 result=PASS features=15 exchanges=11 recoveries=0
SPI_TEST fake exchange bus=1 result=PASS features=4 exchanges=11 recoveries=0
SPI_TEST fake sequence bus=1 result=PASS features=0 exchanges=4 recoveries=0
SPI_TEST fake errors bus=1 result=PASS features=0 exchanges=4 recoveries=2
SPI_TEST fake concurrent bus=1 result=PASS features=190 exchanges=64 recoveries=0
SPI_TEST fake all bus=1 result=PASS cases=6

mcu_spi_test fake dual
SPI_TEST fake dual result=PASS overlap=2

mcu_spi_test fake all --bus 2
SPI_TEST fake FAIL bus=2 reason=no-device
```

现役 timer/WDT/oneshot/RTC 回归同一固件通过：timer 001 最大误差 346 us
（0.346%，容限 0.50%），timer 002 周期比例 2.001，timer 005 生命周期和拒绝路径
PASS；WDT 连续喂狗 9 次、9017 ms 无复位，WDT 非法/重复生命周期 PASS；oneshot
100000 us 完成；`/dev/rtc0` 存在且 `date` 可读。期间没有 assert、panic 或 watchdog
reset。

USB2 只承载控制台。上述结果没有经过 SPI pin，不证明 CLK、CS、MOSI/MISO、mode、
频率或电气行为。

## 实物验收流程

SPI0 需要断电短接 GPIO19(MOSI) 与 GPIO18(MISO)，逻辑分析仪连接 GPIO13(CLK)、
GPIO12(CS0)、MOSI、MISO 和 GND。除烧录外，完整流程如下：

1. 记录板卡/模组、固件 SHA256、pin、短接和分析仪设置。
2. 启动后确认 `/dev/spi0` 存在、对应 GPIO 节点被移除、无 SPI 初始化错误。
3. 执行最小 mode 0、8-bit、100 kHz、32-word loopback。
4. 覆盖 mode 0..3、8/16-bit、100 kHz/400 kHz/1 MHz、MSB/LSB；数据必须逐字
   一致，分析仪确认实际 frequency、CPOL/CPHA 和 bit order。
5. 执行 TX-only、RX-only、full-duplex，验证三条 buffer 路径。
6. 执行 sequence hold/release；分析仪确认 transfer 间 CS 是否保持或释放。
7. 执行 1、31、32、33、256、1024 words 边界，共 48 组 mode/width 组合。
8. 断开环回制造负例，必须 FAIL；恢复短接后必须再次 PASS。
9. 重跑 GPIO 可用节点、timer 001/002/005、WDT 002/003、oneshot、RTC，确认
   `nsh>` 存活且无系统异常。

每条命令、内部动作和 PASS 判据见
`apps/mcu_peripheral_tests/spi/README.md`，该文档直接记录关键实测数据，不依赖外部
任务日志。

## 当前限制

- SPI0 fake、裁剪、启动和 `/dev/spi0` 注册已通过；SPI0 实物环回和波形尚未执行。
- SPI1 lower-half 的 fake 和裁剪已通过；四线 pin、模组引出、board 注册和实物测试
  尚未冻结，不能声明 SPI1/dual 实板通过。
- 普通 consumer 无法通过 `spi_transfer()` 返回值取得 `exchange(void)` 内部错误；
  当前 diagnostic 仅供测试配置使用。
- recovery 还缺真实 timeout/FIFO/BUSY/CS 故障注入；目前只证明 fake hook 和下一笔
  软件传输恢复。
- `spi_register()` 失败会释放 controller 引用，但尚未回滚已设置的 pinmux 和 CS
  输出；这是不阻塞当前软件闭环的后续改进项。
