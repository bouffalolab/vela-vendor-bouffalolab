# BL616CL 堆分配归属与序号观测

本文说明 BL616CL 如何启用 OpenVela 通用 heap allocation record，并给出可重复的
多线程归属、sequence 窗口、realloc 和释放清除测试。烧录流程由 SDK 通用工具文档
统一维护，本文从配置和构建开始，运行流程从固件已经启动到 NSH 提示符开始。

## 背景

BL616CL 的芯片适配层只向 OpenVela 提供 heap 起止地址，实际分配由
`nuttx/mm/mm_heap/` 的通用 allocator 完成。基础 `/proc/memdump` 能遍历当前
未释放块，但默认 allocation node 没有线程和分配顺序字段，不能回答以下问题：

- 哪个线程申请了仍未释放的块；
- 某个动作发生期间新增了哪些块；
- `realloc` 后记录归属原线程还是执行 `realloc` 的线程；
- 释放和线程退出后记录是否消失。

`MM_RECORD_PID` 和 `MM_RECORD_SEQNO` 直接在通用 allocator 中补齐这些字段，
不需要 BL616CL、RISC-V 或 board 私有 hook。

## 方案与裁剪边界

正式 `nsh` 配置启用：

```text
CONFIG_MM_RECORD_PID=y
CONFIG_MM_RECORD_SEQNO=y
```

两项关闭时，PID/sequence 字段、sequence 计数器和按 PID 查询路径由 Kconfig
裁掉。正式配置不启用测试命令：

```text
# CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD is not set
```

需要复现实测时，可临时启用 `BL_OS_FEATURE_TESTS_MM_RECORD`。它依赖 flat build、
builtin app、pthread 和上述两个 MM record 选项，并单独控制 controller 与 worker
栈大小。关闭后 `mm_record_test` 不进入最终 ELF。

全量 memdump 的格式化和串口输出调用深度较大。当前 board 同时使用：

```text
CONFIG_INIT_STACKSIZE=4096
CONFIG_SYSTEM_NSH_STACKSIZE=4096
```

当前启动入口是 `nsh_main`，实际首先受 `CONFIG_INIT_STACKSIZE` 控制；只提高
`CONFIG_SYSTEM_NSH_STACKSIZE` 不能修复该入口的栈溢出。

## 接口语义

| 接口 | 作用 | 判读规则 |
|---|---|---|
| `cat /proc/<tid>/heap` | 汇总指定 TID 当前未释放块 | 线程退出后节点消失 |
| `cat /proc/memdump` | 读取下一次分配使用的 sequence | 返回值记为窗口起点或终点 |
| `echo '<tid> <min> <max>' > /proc/memdump` | 输出指定 TID 和闭区间的块 | 结果经 syslog 输出，不是文件内容 |
| `echo 'used <min> <max>' > /proc/memdump` | 输出所有 TID 在闭区间的块 | 适合未知执行线程的初查 |

sequence 读数是“下一次分配号”。动作前后分别读到 `S0`、`S1` 时，有效闭区间是
`[S0,S1-1]`。procfs 读取和串口命令本身也可能分配内存，因此不能把窗口中的所有
块都直接归因于待测动作，必须同时按 TID、地址和大小核对。

记录的是执行分配操作的 TID，不是进程组。`realloc` 需要新块时，新记录归执行
`realloc` 的线程；旧记录随原块释放而消失。该功能只保存当前未释放块，不保存
历史，也不包含调用栈。

## 配置与构建

正式能力已在 board defconfig 中启用。执行 clean build：

```sh
python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

完成判据：构建退出码为 0，生成 `final_nuttx`、`nuttx.bin` 和
`flash_prog_cfg.ini`；`final_nuttx` 含 MM record 符号，不含
`mm_record_test`。

复现测试时，先完成上述 configure/build，再仅在构建目录临时启用测试 app：

```sh
prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak \
  --file cmake_out/ai-m64l-32s-kit_nsh/.config \
  --enable BL_OS_FEATURE_TESTS_MM_RECORD
cmake --build cmake_out/ai-m64l-32s-kit_nsh -j14
```

该临时开关不执行 `savedefconfig`。测试完成后重新 clean build，即恢复正式配置。

## 固件运行测试

### 1. 建立单一串口会话

```sh
picocom --baud 2000000 --lower-rts /dev/ttyUSB2
```

串口打开会使 Ai-M64L-32S-Kit 重启一次。保持同一会话直到全部测试完成，看到
`NuttShell (NSH)` 和 `nsh>` 后开始后续步骤。

完成判据：启动过程无 assert/panic，NSH 能连续响应 `echo alive`。

### 2. 创建确定性多线程分配

```text
mm_record_test start 64 96 &
mm_record_test status
```

实测输出：

```text
MM_RECORD_TEST READY controller=4 worker0=5 ptr0=0x60fca830 size0=64 worker1=6 ptr1=0x60fce580 size1=96
```

完成判据：controller 和两个 worker 的 TID 均非 0、互不相同，两个地址非 0，
size 分别为 64 和 96。

### 3. 保存动作前窗口和线程汇总

```text
cat /proc/memdump
cat /proc/4/heap
cat /proc/5/heap
cat /proc/6/heap
```

实测下一 sequence 为 85，因此记 `S0=85`。完成判据：三个 TID 的 heap 节点
均可读，worker 汇总包含各自仍持有的测试分配。

### 4. 由 controller 执行 realloc

```text
mm_record_test realloc 0 2000
cat /proc/memdump
```

实测输出：

```text
MM_RECORD_TEST REALLOC controller=4 worker0=5 ptr0=0x60fcf2d0 size0=2000 worker1=6 ptr1=0x60fce580 size1=96
```

动作后下一 sequence 为 100，因此记 `S1=100`，有效窗口为 `[85,99]`。

完成判据：slot0 地址或大小反映 realloc 结果，命令返回成功，且 `S1>S0`。

### 5. 按 TID 核对窗口归属

```text
echo '4 85 99' > /proc/memdump
echo '5 85 99' > /proc/memdump
echo '6 85 99' > /proc/memdump
```

PID 4 实测关键输出：

```text
PID        Size Overhead    Sequence    Address
     4        2016       16          95 0x60fcf2d0
Total Blks  Total Size
         1        2016
```

PID 5 和 PID 6 在同一窗口均为 `Total Blks 0`。

完成判据：窗口只在 controller PID 4 下出现一个 sequence 95、总大小 2,016 B
的块；worker 不保留 realloc 前的旧归属记录。

### 6. 释放并验证清除

```text
mm_record_test free
echo '4 85 99' > /proc/memdump
cat /proc/5/heap
cat /proc/6/heap
```

实测输出：

```text
MM_RECORD_TEST FREED controller=4 worker0=5 ptr0=0 size0=2000 worker1=6 ptr1=0 size1=96
MM_RECORD_TEST STOPPED
```

释放后 PID 4 窗口为 `Total Blks 0`；PID 5、6 的 procfs 节点返回
`open failed: 2`。

完成判据：测试块全部消失，worker 退出，不存在残留测试实例。

### 7. 验证重复实例和非法参数

```text
mm_record_test start &
mm_record_test free
mm_record_test start &
mm_record_test free
mm_record_test realloc 0 12junk
```

两轮实例实测 controller TID 从 11 变为 15，均依次输出 `READY`、`FREED` 和
`STOPPED`。非法 size 实测输出：

```text
MM_RECORD_TEST failed: -22
```

完成判据：新实例不继承前一实例状态；非法参数返回 `EINVAL`，不发起 allocator
请求，系统仍响应 `echo alive`。

## 外设回归

MM record 保持开启，测试 app 运行并释放后，在同一固件依次执行：

```text
mcu_gpio_test -c edge --out /dev/gpio12 -n 3 -v
mcu_timer_test -c 001 -t 10000 -n 5 -e 5 -v
mcu_timer_test -c 002 -t 500000 -a 39 -b 79 -v
mcu_timer_test -c 005
mcu_wdt_test -c 002 -t 1000 -p 3000 -i 500 -v
mcu_wdt_test -c 003 -t 1000
```

实测结果：

| 用例 | 结果 |
|---|---|
| GPIO edge | 非法操作被拒绝，3 个周期均恢复，PASS |
| TIMER-001 | 10 ms、5 轮，最大误差 233 us（2.330%），PASS |
| TIMER-002 | divider 39/79 周期比 2.000，PASS |
| TIMER-005 | 非法请求、运行中更新和生命周期恢复，PASS |
| WDT-002 | 1,000 ms timeout、500 ms 喂狗，持续 3,006 ms 无复位，PASS |
| WDT-003 | 非法/live 修改和重复生命周期请求被拒绝，PASS |

完成判据：六项均输出 PASS 或正常 stop，串口无意外 assert、panic 或复位。

## 开销

### Allocation node

当前 RV32 default allocator、guard size 0 的理论开销：

| 配置 | allocation node | 相对关闭态 | 最小 chunk |
|---|---:|---:|---:|
| PID/SEQNO 均关闭 | 8 B | 0 B | 16 B |
| 仅 PID | 12 B | +4 B | 32 B |
| 仅 SEQNO | 12 B | +4 B | 32 B |
| PID+SEQNO | 16 B | +8 B | 32 B |

最小 chunk 的对齐放大会影响小对象：即使字段只增加 4 B，16 B 小块也可能增长
到 32 B。SEQNO 还增加一个每 CPU 4 B 的计数器。

### 固件制品

关闭/开启 clean build 的受控对照：

| 制品 | 关闭 | PID+SEQNO 开启 | 增量 |
|---|---:|---:|---:|
| `final_nuttx` | 688,528 B | 692,816 B | +4,288 B |
| text | 300,734 B | 302,174 B | +1,440 B |
| data | 14,292 B | 14,292 B | 0 B |
| bss | 11,128 B | 11,128 B | 0 B |
| `nuttx.bin` | 321,136 B | 322,576 B | +1,440 B |

测试 app 和 4,096 B NSH 栈用于验证，不计入上述 MM record 功能增量。是否在产品
配置默认开启，应同时按对象数量和小对象比例估算运行 heap 开销。
