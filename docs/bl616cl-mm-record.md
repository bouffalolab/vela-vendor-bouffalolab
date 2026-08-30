# BL616CL 堆分配归属与序号观测

本文说明 BL616CL 如何启用 OpenVela 通用 heap allocation record，并给出可重复的
多线程归属、sequence 窗口、realloc stack 记录和释放清除测试。烧录流程由 SDK 通用工具文档
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

## Realloc 失败保留调用栈记录

### 背景与能力交集

OpenVela default allocator 的 allocation node 可同时保存 PID、sequence 和
backtrace pool entry。`realloc()` 可能走四类路径：缩小、原地扩展、移动扩展和
释放旧块后的 fallback。旧实现进入 heap lock 后立即移除旧 stack；当 fallback
分配失败时，旧地址和内容虽然按 `realloc` 合同保留，诊断记录却已经丢失。

BL616CL 的最大可验证交集是 `MM_RECORD_PID`、`MM_RECORD_SEQNO`、
`MM_RECORD_STACK` 与 default allocator 的成功/失败路径。测试使用 vendor-only
private heap 读取 node 的 PID、sequence、stack index 和 raw frames，不新增
NuttX public test hook；backtrace pool 的 refcount 没有固件查询 API，只在 worker
静止、调度锁定的 dump 区间用 marker 判读。

### 方案与裁剪

修复只延迟旧 stack 引用的释放时机：

| 路径 | 旧 stack 所有权 | 结果 |
|---|---|---|
| shrink/同尺寸 | 原地成功前不释放，成功点释放一次 | 解锁后重建 controller 记录 |
| 原地 grow | 原地成功前不释放，成功点释放一次 | 地址不变并重建记录 |
| moved grow | 保存旧引用，成功点释放一次 | 新地址重建记录 |
| fallback success | 不显式释放 | `mm_free(oldmem)` 唯一释放旧记录 |
| fallback failure | 完全不修改 | 旧 node、PID、sequence、stack 和内容不变 |

专项配置只在测试固件打开：

```text
CONFIG_LIBC_BACKTRACE_DEPTH=8
CONFIG_MM_RECORD_PID=y
CONFIG_MM_RECORD_SEQNO=y
CONFIG_MM_RECORD_STACK=y
CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD=y
CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD_REALLOC_STACK=y
CONFIG_INIT_STACKSIZE=4096
```

`BL_OS_FEATURE_TESTS_MM_RECORD_REALLOC_STACK` 依赖 default allocator、
`MM_RECORD_STACK`、`FS_PROCFS` 和可观测的 meminfo entry。关闭该选项时
`mm_realloc_stack_test.c` 不进入 archive；关闭 `MM_RECORD_STACK` 时专项选项
不可选。专项测试使用 16 KiB、`nokasan=true` 的 private heap，避免占用现役
KASAN 区域；allocation 在 worker 线程执行，`realloc` 在 controller 线程执行。

### 测试命令与完整流程

以下流程从固件已经启动并出现 `nsh>` 开始，烧录步骤按 SDK 通用文档执行。

1. 确认串口启动和测试命令存在：

   ```text
   help
   mm_record_test realloc_stack R01
   ```

   2 Mbps USB2 实测启动输出为 `NuttShell (NSH) NuttX-3.6.1` 和 `nsh>`；
   R01 返回 `MM_REALLOC_STACK R01 PASS`。

2. 依次执行六个 case。单独执行时使用：

   ```text
   mm_record_test realloc_stack R01
   mm_record_test realloc_stack R02
   mm_record_test realloc_stack R03
   mm_record_test realloc_stack R04
   mm_record_test realloc_stack R05
   mm_record_test realloc_stack R06
   ```

   一次性回归使用：

   ```text
   mm_record_test realloc_stack
   ```

   每个命令都必须等待 `nsh>` 再发送下一条；命令返回非 0、出现 assert/panic
   或没有 prompt 时停止，不把启动重试当成 case 通过。

3. R01 缩小：worker 分配 512 B 并写入 `0xa5`，controller 缩小到 128 B。
   检查地址不变、前 128 B 内容、PID 切换为 controller、sequence 变化和新
   stack 非空，然后释放 private heap。

4. R02 原地向后扩展：先占用 prefix，再由 worker 分配 256 B target，保留
   next remainder，controller 扩展到 512 B。检查地址不变、原内容保留、PID
   切换和旧引用只释放一次。

5. R03 向前移动扩展：布局为 512 B prefix、256 B target、256 B suffix，释放
   prefix 后把 target 扩展到 512 B。检查地址向低地址移动至少 256 B、原内容
   保留、PID/sequence 更新和新 stack 有效。

6. R04 fallback 成功：target 前后各保留 occupied chunk，请求 1,024 B，迫使
   allocator 在其它 free 区分配新块。检查返回地址改变、原内容复制、旧块由
   `mm_free()` 清理且新记录归 controller。该 case 的比较顺序必须先保存旧
   target，再判断 `result != target`。

7. R05 fallback 失败：target 前后各保留 occupied chunk，读取 private heap 的
   最大空闲块并用 filler 耗尽剩余空间，再请求 1,024 B。检查返回 `NULL` 后，
   旧地址仍可读、前 128 B 仍为 `0xa5`、worker PID、sequence、stack index 和
   raw frames 全部保持不变。

8. R06 重复引用：同一 noinline allocation callsite 分配三个 64 B block，
   在静止区间执行 `backtrace_dump()`，再依次执行 target shrink、释放一个
   block 并处理 delay list、target 原地 grow。四个 marker 的 expected ref
   必须为 `3`、`2`、`1`、`0`；主机只按同一次 `final_nuttx` 的 ELF 将 raw PC
   符号化，不把其它 pool entry 归因于本 case。

### 旧版 red 实测

旧版未修复固件在 USB2 执行 `mm_record_test realloc_stack R05`：

```text
MM_REALLOC_STACK R05 INFO largest=15224 overhead=16
MM_REALLOC_STACK R05 INFO before=0x60fd6d0c/3 after=0/0
MM_REALLOC_STACK R05 FAIL old stack changed
MM_RECORD_TEST failed: -1
```

此结果同时满足 `realloc` 返回失败、旧块仍在和旧内容未破坏，但 stack entry
从有效地址/depth 3 变为 NULL/depth 0，证明修复前存在诊断记录丢失。

### 修复版 green 实测

修复版专项固件 clean build 为 `1192/1192`，USB2 分区烧录三段 SHA 校验一致，
复位匹配 `NuttShell (NSH)` 和 `nsh>`。逐 case 实测结果如下：

| Case | 固件关键结果 |
|---|---|
| R01 | `MM_REALLOC_STACK R01 PASS` |
| R02 | `MM_REALLOC_STACK R02 PASS` |
| R03 | `MM_REALLOC_STACK R03 PASS` |
| R04 | `MM_REALLOC_STACK R04 PASS` |
| R05 | `largest=15224 overhead=16`，`MM_REALLOC_STACK R05 PASS` |
| R06 | `expected_ref=3/2/1/0`，`MM_REALLOC_STACK R06 PASS` |

一次性执行六个 case 的最终输出为 `MM_REALLOC_STACK ALL PASS`。R06 的静止
dump 关键数据为：

```text
expected_ref=3: slot 43 refcount 3
expected_ref=2: slot 43 refcount 2
expected_ref=1: slot 43 refcount 1
expected_ref=0: slot 43 absent
capacity: 64
```

同一 dump 中新生成的 controller stack 使用其它 slot，说明旧 entry 的引用
在 `3 -> 2 -> 1 -> 0` 过程中按成功路径释放；测试判据比较 raw frames 内容，
不要求 pool entry 地址必须变化。

### 构建、裁剪与回归门禁

专项配置的 `libapps_mm_record_test.a` 独立生成，并导出
`mm_realloc_stack_test`；`final_nuttx`、`nuttx.bin`、`nuttx.whole.bin` 和
`System.map` 均生成。标准 `nsh` clean build 为 `1224/1224`，关闭专项选项
且不生成 `libapps_mm_record_test.a`，证明测试代码可裁剪。NuttX
`mm_realloc.c` 的 nxstyle、checkpatch 和 `git diff --check` 均通过。

修复版执行完 R01-R06 后，现役 GPIO edge、TIMER-001/TIMER-002/TIMER-005、
oneshot 和 WDT-002/WDT-003 回归保持通过；串口无意外 assert、panic 或复位。

### 限制与判读边界

- R06 的 `capacity/used/ref` 来自静止 dump 的主机解析，不是固件 public API，
  不能推广为运行时查询合同。
- 测试使用 default allocator private heap，不覆盖 TLSF、mempool、task heap
  或 IOB；这些 allocator 需要独立能力审计和测试。
- `R03` 的移动复制沿用上游现有 `memcpy` 语义；当前 pattern 实测通过，若未来
  布局允许重叠而出现失败，应另建独立 finding，不混入本修复。


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
