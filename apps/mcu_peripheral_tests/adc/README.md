# MCU ADC 外设测试

本目录验证 BL616CL GPADC lower-half 与 OpenVela ADC upper-half 的组合。
`adc_test_main.c` 编译为独立测试 archive，注册 NSH 命令 `mcu_adc_test`，不进入
`libarch.a`。测试只通过 `/dev/adc0` 的 `read()`、`poll()` 和标准 ioctl
（`ANIOC_TRIGGER`、`ANIOC_GET_NCHANNELS`）访问驱动；通道表取自板级 Kconfig
（`CONFIG_AI_M64P_KIT_ADC_CHn`），传输方式取自 `CONFIG_BL616CL_ADC_POLL`/`_IRQ`。
驱动说明见 `docs/bl616cl-adc.md`。

| 用例 | 方式 | 内容 |
|---|---|---|
| ADC-001 | 两种 | `ANIOC_GET_NCHANNELS` 等于板级通道数；触发前非阻塞 `read()` 返回 `EAGAIN` |
| ADC-002 | 轮询 | 1000 次触发，每次恰好一组、按扫描顺序、读数在 0–3200 mV；单次触发不超过 10 ms |
| ADC-003 | 两种 | 已知电压：3 轮 × 每通道 256 个样本，接地点每轮均值误差 ≤10 mV，其余点 ≤20 mV 加表的误差（`-t`），电压高的读数也高；没有 `-e` 时 SKIP |
| ADC-004 | 两种 | 3 轮 × 256 个样本的均值、标准差和峰峰值，只报告；输入悬空时数字没有意义 |
| ADC-005 | 两种 | 两个 fd 共用一个队列；非最后一次 close 保留队列和连续扫描（之后再按顺序读到 128 个样本，多于上层队列容量）；最后一次 close 丢弃队列并停止扫描，重开后读不到旧数据，再触发从第一个通道开始；连续方式下第二次启动返回 `EBUSY`；20 轮 |
| ADC-006 | 轮询 | 队列放不下一组时触发返回 `EOVERFLOW` 且不入队；已入队的组完整，读完后能再触发 |
| ADC-007 | 连续 | 3 轮 × 10 s 连续读取，每轮至少 10,000 个样本，顺序连续、没有报错（未发现丢样；单通道或整轮丢失靠驱动的溢出标志发现） |
| ADC-008 | 连续 | 读者停 500 ms 让上层队列满：先读到满之前的样本，再读到一次 `EIO`，之后 `EAGAIN`，扫描已停；重新触发恢复；3 次 |
| ADC-009 | 连续 | 最高优先级线程忙等 50 ms，挡住 HPWORK，让 16 级硬件 FIFO 溢出：读到溢出前的样本、一次 `EIO`、`EAGAIN`；开 `DEBUG_ANALOG_WARN` 时驱动打印 `FIFO overrun`；3 次 |
| ADC-010 | 两种 | 空队列上 `poll()` 超时；另一线程 200 ms 后触发，`poll()` 以 `POLLIN` 唤醒 |
| ADC-011 | 两种 | open、触发、读、close 连续 200 次，每次第一个样本都是第一个通道 |

## 配置

产品配置只开板级通道：

```text
CONFIG_AI_M64P_KIT_ADC=y
CONFIG_AI_M64P_KIT_ADC_CH0=y        # 按需再选 CH1–CH9
```

测试配置再开：

```text
CONFIG_BL_MCU_PERIPHERAL_TESTS_ADC=y
```

`nsh-peripherals` 用轮询方式、CH0–CH3（GPIO0–3）。连续方式的用例需要临时配置
`CONFIG_BL616CL_ADC_IRQ=y`，要看 ADC-009 的驱动日志再开 `CONFIG_DEBUG_ANALOG`、
`CONFIG_DEBUG_ANALOG_WARN`。

## 运行

```text
nsh> mcu_adc_test -c all
nsh> mcu_adc_test -c 003 -e 0:0,1:1650,2:3000 -t 5
```

- `-c <id>`：001–011 或 `all`（默认）。不适用于当前传输方式的用例报 SKIP。
- `-e ch:mV[,ch:mV...]`：ADC-003 每个通道上加的电压，用万用表实测值；接地写 0。
- `-t <mV>`：表的标称误差，加到非零点的 20 mV 限值上。

ADC-003 前在对应 GPIO 上加电压，不超过 3.2 V（超过的部分读数饱和在 3199 mV）。
同一通道的电压在三轮中保持不变。程序判据是均值误差和单调性；噪声数字只报告。
