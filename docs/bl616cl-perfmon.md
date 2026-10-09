# BL616CL 性能监视（perfmon）

`BL616CL_PERFMON` 在 chip 层接入 E907 硬件计数器和每个中断的耗时统计，
`perfmon` 命令在一个时间窗口内给出 IPC、I-cache/D-cache 缺失率、条件分支
预测失败率、各 IRQ 占用的周期，以及按 tick 采样的热点函数。宿主工具在
`tools/bl616cl/perf/`，负责和 iperf 一起采集、汇总，以及 Wi-Fi 热函数布局的
回归（见 [bl616cl-hot-code-layout.md](bl616cl-hot-code-layout.md)）。

## 背景

NuttX 自带的手段不够用：`SCHED_IRQMONITOR` 只记每个中断的次数和最长耗时，
精度 1 µs；通用 perf events（能力矩阵 A08）只有 cycle；tick 采样在中断里
执行，看不到中断处理本身和关中断区间。排列 Wi-Fi 热函数时，I-cache
缺失率和 Wi-Fi 中断每次的耗时是判断收益的主要依据，当时靠一个不提交的采样
补丁取得。本功能把这两项做成可配置的基础能力，并保留 tick 采样。

## 配置与裁剪

```text
CONFIG_BL616CL_PERFMON=y           # chip：选择计数事件、按 IRQ 计时
CONFIG_BL616CL_PERFMON_PCSAMPLE=y  # chip：tick PC 采样，默认随上项打开
CONFIG_BL_PERF_TOOLS_PERFMON=y     # apps：perfmon 命令
```

`nsh-peripherals` defconfig 打开 `BL616CL_PERFMON` 和 `BL_PERF_TOOLS_PERFMON`，
其他配置不打开。关闭 `BL616CL_PERFMON` 时 `riscv_dispatch_irq()` 直接调用
`riscv_doirq()`，镜像里没有 perfmon 的代码和数据。

`wifi` 不默认打开：打开会挪动 Wi-Fi 以外的代码，UDP TX 会随之变化（见“验证”）。
要在 Wi-Fi 负载下测量时，用 `tools/bl616cl/perf/perf_build.sh` 生成一个临时
镜像，它复制 `configs/wifi`、加上上面两项、构建并把镜像放到指定目录，然后删除
临时配置：

```sh
vendor/bouffalolab/tools/bl616cl/perf/perf_build.sh <out>/img
vendor/bouffalolab/vela flash --config <out>/img/flash_prog_cfg.ini \
  --port /dev/ttyUSB3 --baudrate 1000000
```

第二个参数可以换成其他配置名。

打开后常驻 RAM 为两个按 IRQ 号索引的数组，`NR_IRQS` 为 99 时共 1,188 字节，
PC 采样另有几个字。`perfmon prof` 运行时从堆申请直方图，大小为
`.text 字节数 / 2^shift × 2`；`wifi` 当前 `.text` 约 547 KiB，默认 32 字节
一桶时为 17,508 桶、35 KB。板上显示函数名需要 `ALLSYMS`，四个配置都已打开。

## 实现

### 计数事件

启动时 `bl616cl_perfmon_initialize()`（在 `bl616cl_bringup()` 中调用）写入
下面的事件选择。E907（RV32）的计数器 N 固定对应事件 N−2，与 LHAL
`rv_hpm.h` 的 T-Head 辅助函数一致：

| 计数器 | 事件号 | 含义 |
| --- | --- | --- |
| `mhpmcounter3` | 1 | I-cache 访问 |
| `mhpmcounter4` | 2 | I-cache 缺失 |
| `mhpmcounter8` | 6 | 条件分支预测失败 |
| `mhpmcounter9` | 7 | 条件分支 |
| `mhpmcounter14` | 12 | D-cache 读访问 |
| `mhpmcounter15` | 13 | D-cache 读缺失 |
| `mhpmcounter16` | 14 | D-cache 写访问 |
| `mhpmcounter17` | 15 | D-cache 写缺失 |

计数器从复位起自由运行，不清零，也不用溢出中断（64 位计数，320 MHz 下
不会回绕）。RV32 上按高、低、高三次读取，高位不一致就重读。
`bl616cl_perfmon_read()` 返回 `mcycle`、`minstret` 和上述计数，调用者取
两次读数的差。

### 按 IRQ 计时

`riscv_dispatch_irq()` 的尾部改为 `bl616cl_perfmon_dispatch()`：在
`riscv_doirq()` 前后读 `mcycle`，按 NuttX IRQ 号累加调用次数和周期数。
统计范围包括 ISR 本身和 NuttX 的中断后处理（唤醒、选择下一个任务），不包括
trap 入口和出口保存、恢复寄存器的指令。IRQ 11（ecall）是上下文切换的系统
调用，IRQ 23 是 MTIMER tick，IRQ 86 是 Wi-Fi。`bl616cl_perfmon_irq_read()`
关中断拷贝整张表。

### tick PC 采样

`bl616cl_perfmon_pc_start()` 登记调用者提供的 `uint16_t` 直方图后，每次
MTIMER 中断（1 kHz）按被打断的任务和 `mepc` 计数：打断的是 idle 任务就计入
idle；否则 PC 在 `_stext` 到 `__bl616cl_text_end`（`ld.script` 在 `.text`
末尾导出）之间时计入 `(pc - base) >> shift` 桶，桶满 65,535 后不再增加；
其余（RAM 中的代码）计入 outside。同一时间只允许一个采样者，第二个返回
`-EBUSY`。`bl616cl_perfmon_pc_stop()` 停止并返回总数、idle 数和 outside 数。

接口声明在 `chips/bl616cl/include/bl616cl_perfmon.h`，应用以
`<arch/chip/bl616cl_perfmon.h>` 包含，只在 `CONFIG_BL616CL_PERFMON` 打开时
存在。

## 使用

### perfmon stat

在负载旁边后台运行，窗口结束时打印一次：

```text
nsh> perfmon stat -d 2 -t 16 &
nsh> iperf -c 192.168.50.200 -p 7001 -i 5 -t 20
...
perfmon: 16.00 s, 5121175448 cycles, 459553079 instructions, IPC 0.089 (idle included)
  I-cache       517985812 access    7968521 miss   1.54%
  D-cache rd     68756416 access    4426660 miss   6.44%
  D-cache wr     39539189 access    1096793 miss   2.77%
  branch         46925779 cond.     9703601 miss  20.68%
  interrupts and exceptions: 8.0% of the cycles
   IRQ name                   calls       cycles  share  cyc/call
    86 wifi                    21896    191633266   3.7%      8751
    23 mtimer                  16004    164253621   3.2%     10263
    11 ecall                   43441     53672584   1.0%      1235
    60 uart0                       6        34048   0.0%      5674
```

`-d` 是窗口开始前的等待，用来跳过 TCP 慢启动；`-t` 是窗口长度，默认
10 秒。`perfmon` 的优先级是 100，同优先级或更高优先级的任务一直占着 CPU 时，
它醒来得晚，窗口会变长；以输出的秒数为准。`mcycle` 在 WFI 中继续计数，所以 IPC 含 idle 时间，只能在相同负载
之间比较。IRQ 表按周期数取前 8 项，`share` 是占整个窗口周期的比例。

### perfmon prof

```text
nsh> perfmon prof -d 2 -t 10 -n 10 &
nsh> iperf -u -c 192.168.50.200 -p 7301 -i 5 -t 14
...
  10006 ticks: idle 10.9%, busy outside .text (RAM code) 0.0%
      %  samples  address     function
   22.1     2212  0x80036b9a  memcpy
    8.4      840  0x80062d7a  psock_udp_sendto
    6.1      613  0x8003984c  nxsem_wait_slow
    4.7      466  0x80056ddc  netdev_list_lock
    3.2      323  0x8005edb4  netdev_txnotify_dev
    2.3      229  0x8005d330  arp_send
...
```

先打印与 `stat` 相同的计数和 IRQ 表，再加上 tick 统计和热点函数；百分比是
占全部 tick 的比例，idle 单独列出。`-s` 设桶大小为 `2^shift` 字节（2–12，
默认 5），`-n` 设显示的函数数（默认 20）。板上把一个桶算给桶首字节所在的函数，跨函数边界的
桶会把一部分样本算给前一个函数。

`-r` 不做符号化，改为输出原始直方图，供宿主工具按链接 map 归到模块：

```text
perfmon-raw base=0x80008000 shift=5 buckets=17508 samples=16002 idle=1672 outside=0
c 9
...
perfmon-raw end
```

每行是十六进制桶号和样本数，只列非零桶。

### 宿主工具

`tools/bl616cl/perf/wifi_bench.py` 复位开发板、连接 AP（`WIFI_TEST_PSK`
为空时按 open AP）、依次跑 TCP/UDP 收发，并可在每个场景的流量窗口内运行
`perfmon stat` 或 `perfmon prof -r`，把输出存为 `r<rep>-<case>.perfmon`：

```sh
python3 vendor/bouffalolab/tools/bl616cl/perf/wifi_bench.py <out> \
  --reps 1 --time 20 --udp-rx 60 --perfmon prof
```

镜像要带 perfmon，即上面 `perf_build.sh` 生成的镜像。`perfmon_report.py`
汇总这些文件；给出该镜像的 `nuttx.map` 和 `final_nuttx` 后，还会按模块和
函数归类样本：

```sh
python3 vendor/bouffalolab/tools/bl616cl/perf/perfmon_report.py \
  --map <out>/img/nuttx.map --elf <out>/img/final_nuttx <out>/*.perfmon
```

## 验证

`nsh-peripherals`（默认打开）：

- 空闲 3 秒：`mcycle` 增加 960,351,299（320 MHz × 3 s），tick 全部计入 idle。
  MTIMER 每次约 10.7k cycle，占 3.3%，是 `wifi` 的 6 倍，差别来自 KASAN 和
  stack canary 插桩。
- 演示负载 `perfmon prof -t 5 -n 8 &` 后接
  `dd if=/dev/zero of=/dev/null bs=512 count=200000`：IPC 0.74，idle 2.4%，
  热点前几位是 `kasan_check_report`（41%）、`__asan_store2_noabort`（10%）等
  KASAN 检查。`dd` 与 `perfmon` 同为优先级 100，`dd` 一直占着 CPU，窗口从
  5 秒拉长到 12.1 秒；输出的秒数是实际窗口。

Wi-Fi 数据取自打开 perfmon 的 `wifi` 镜像，与 `perf_build.sh` 生成的相同：

- 空闲 3 秒：`mcycle` 增加 960,181,069（320 MHz × 3 s），MTIMER 3,001 次，
  tick 全部计入 idle。
- open AP，每个场景 20 秒，UDP RX 由主机按 60 Mbit/s 发送。吞吐为不运行
  perfmon 时 3 轮的范围；计数取自在第 2 到第 18 秒运行的 `perfmon stat`，
  idle 取自同样窗口的 `perfmon prof -r`：

| 场景 | 吞吐 Mbps | IPC | I-cache 缺失 | D-cache 读缺失 | 分支预测失败 | idle | Wi-Fi IRQ 占比，周期/次 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| TCP TX | 20.8–21.0 | 0.090 | 1.54% | 6.4% | 20.7% | 42.1% | 3.7%，8,751 |
| UDP TX | 45.2–45.6 | 0.113 | 1.55% | 4.6% | 18.0% | 10.1% | 6.5%，11,129 |
| TCP RX | 21.4–21.5 | 0.110 | 1.33% | 5.1% | 18.5% | 30.5% | 3.4%，5,696 |
| UDP RX | 41.7–42.2 | 0.195 | 0.47% | 3.2% | 16.8% | 8.0% | 2.3%，2,055 |

- 重复性：两次开机、三次采集之间，各缺失率相差不超过 0.25 个百分点，Wi-Fi
  中断每次的周期数相差不超过 4%。
- 与此前采样补丁的数量级一致（补丁在布局优化后测得 TX 和 TCP RX 的
  I-cache 缺失率为 1.15%–1.38%，UDP TX 时 Wi-Fi 中断约 9.3k 周期/次）。两者
  不是同一个镜像，Wi-Fi 以外代码的位置不同，不能逐项比较。
- 开销：每次中断多执行两次 `mcycle` 读取和一次数组累加。UDP TX 时中断和
  异常约 6,000 次/秒，按每次 30 条指令估算不到 CPU 的 0.1%。同一源码关闭
  perfmon 的镜像（`nuttx.bin` 小 6,176 字节，堆多 1,232 字节）测得 TCP TX
  20.4–20.5、UDP TX 47.7–48.0、TCP RX 21.1–21.2、UDP RX 41.2–41.9。打开
  perfmon 后 UDP TX 低 5%，另外三项持平或略高；最吃 CPU 的 UDP RX（idle
  8%）没有变慢，说明差异来自 Wi-Fi 以外代码的位移，不是 perfmon 的运行开销。
  为了不让 `wifi` 的吞吐基线随它变化，`wifi` 默认不打开。
- `nsh`、`ostest`、`wifi` 不打开本功能，构建产物中没有 perfmon 符号；关闭
  `BL616CL_PERFMON_PCSAMPLE` 的变体编译通过。

## 限制

- tick 采样看不到中断处理和关中断区间。关中断期间到期的 tick 会落在重新开
  中断的位置，例如各场景里约 5% 的 `nxsem_wait_slow`；中断本身的开销看 IRQ
  表。
- 分支和 D-cache 事件直接取自 T-Head 定义，没有用微基准单独标定。条件分支
  预测失败率在负载下为 17%–21%，空闲时约 3%。
- 计数器是全局的，不区分任务；IRQ 表不区分同一 IRQ 的不同来源。
- 2 Mbaud 控制台偶尔丢字节。`perfmon_report.py` 的解析对此放宽，并在原始
  直方图样本数对不上时提示。
