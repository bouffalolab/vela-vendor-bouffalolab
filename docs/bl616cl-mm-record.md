# BL616CL 堆分配归属、序号与回溯观测

本文说明 BL616CL 如何启用 OpenVela 通用 heap allocation record，并给出可重复的
多线程归属、sequence 窗口、动态回溯门控、realloc stack 记录、pool 引用与释放清除
测试。烧录流程由 SDK 通用工具文档统一维护，本文从配置和构建开始，运行流程从
固件已经启动到 NSH 提示符开始。

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
CONFIG_LIBC_BACKTRACE_DEPTH=12
CONFIG_MM_RECORD_STACK=y
# CONFIG_MM_RECORD_STACK_DEFAULT is not set
```

对应选项关闭时，PID/sequence/stack 字段、sequence 计数器、backtrace pool 和按 PID
查询路径分别由 Kconfig 裁掉。产品保留按需诊断能力，但默认不为新分配捕获调用栈；
正式配置不启用测试命令：

```text
# CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD is not set
```

需要复现实测时，使用 `nsh-mm-record-test`、`nsh-mm-record-default-on` 或
`nsh-mm-record-expand` 专项配置。它们复用同一个 `mm_record_test` main；关闭
`BL_OS_FEATURE_TESTS_MM_RECORD` 后测试代码不进入最终 ELF。

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
历史。产品默认关闭 stack capture，需要通过 `/proc/memdump` 全局或按 TID 开启后，
后续分配才会保存调用栈；关闭门控不清除已有记录。

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

复现完整回溯合同时使用独立配置：

```sh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh-mm-record-test -j14
```

默认开启语义使用 `nsh-mm-record-default-on`；pool 扩容使用
`nsh-mm-record-expand`。三个专项配置均包含 GPIO、timer、watchdog 和 RTC 回归
命令，测试完成后重新构建并烧录正式 `nsh`，恢复产品状态。

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

## 堆分配回溯与动态门控

### 背景与最大能力交集

BL616CL 使用 OpenVela default allocator。芯片只提供 SRAM heap 边界，stack record、
backtrace pool 和 `/proc/memdump` 门控全部位于通用 MM 子系统。本轮审计结果如下：

| 路径 | OpenVela 能力 | 本次处理 |
|---|---|---|
| default allocator | node stack、去重 pool、引用计数、扩容、全局/TID 门控、free/realloc 清理 | 纳入并完整验证 |
| TLSF | 有 record 字段；free/realloc 引用清理和 skip 语义不完整 | P2 后续，不切换产品 allocator |
| integrated mempool | 可记录 alloc/free stack；`/proc/mempool` 只读 | 独立能力任务，不冒充动态门控 |
| task heap | flat build 可建立 task group heap | 独立生命周期和 RAM 任务 |
| kernel heap | 需要独立布局和 `up_allocate_kheap()` | 当前 BL616CL 未实现 |
| IOB | 固定 IOB 是静态池，只有 `IOB_ALLOC` 进入 heap | 动态 IOB 另行验证 |

default allocator 的有效依赖不仅是 `LIBC_BACKTRACE_DEPTH>0`，还需要
`FS_PROCFS=y`、`FS_PROCFS_EXCLUDE_MEMINFO=n`。本次动态门控还要求
`FS_PROCFS_EXCLUDE_MEMDUMP=n`。产品选择 depth 12、动态默认关闭：保留现场诊断入口，
正常运行时不持续采集 stack，也不创建 pool entry。

### 配置矩阵与裁剪

| 配置 | 关键选项 | 用途 |
|---|---|---|
| `nsh-mm-record-off` | `MM_RECORD_STACK=n`、测试 app 关闭 | 完全裁剪对照 |
| `nsh` | depth 12、`MM_RECORD_STACK=y`、default off、测试 app 关闭 | 产品配置 |
| `nsh-mm-record-default-on` | 产品能力 + default on + 测试 app | 默认状态专项 |
| `nsh-mm-record-test` | 产品能力 + default off + 测试 app | M02-001..013 主测试 |
| `nsh-mm-record-expand` | init size 4、load factor 75、测试 app | fresh-boot 4→8 扩容 |
| `nsh-mm-realloc-stack` | realloc 专项开启、M02 专项关闭 | 保留 ST027 独立入口 |

五类 M02 配置 clean build 实测：off 和产品均为 `1224/1224`，default-on、test、
expand 均为 `1230/1230`；历史 realloc 专项为 `1227/1227`。目标身份检查结果：

- off 无 `backtrace_record`、`backtrace_dump`、M02 对象和测试 archive；
- 产品含 `backtrace_record`，不含 M02 对象和测试 archive；
- default-on/test/expand 均含 `mm_record_stack_test`、`mm_realloc_stack_test` 和
  `libapps_mm_record_test.a`；
- expand 的生成配置为 `CONFIG_LIBC_BACKTRACE_INIT_SIZE=4`；
- realloc 专项只含 `mm_realloc_stack_test.c.o`，不含 M02 对象。

最终制品数据：

| 配置 | text | data | bss | `final_nuttx` | `nuttx.bin` |
|---|---:|---:|---:|---:|---:|
| off | 465,028 B | 15,744 B | 20,380 B | 864,012 B | 486,416 B |
| 产品 | 467,492 B | 15,744 B | 20,396 B | 868,412 B | 488,880 B |
| default-on | 513,292 B | 16,624 B | 53,292 B | 920,924 B | 535,568 B |
| test | 513,284 B | 16,624 B | 53,292 B | 920,924 B | 535,552 B |
| expand | 515,196 B | 16,688 B | 53,292 B | 921,340 B | 537,536 B |

产品相对完全关闭态增加 2,464 B text、16 B bss、4,400 B ELF 和 2,464 B bin。
测试 app、4 KiB app 栈、worker 栈和 RTC 回归命令不计入产品能力增量。

### 门控语义

`/proc/memdump` 的 stack record 条件是 `heap_global || tid_flag`：

```text
echo -n off > /proc/memdump
echo -n on > /proc/memdump
echo -n <tid>on > /proc/memdump
echo -n <tid>off > /proc/memdump
```

- global off 后分配 A：A 无 stack；
- global on 后分配 B：B 有 stack；
- 再 global off 后分配 C：C 无 stack，B 的既有 stack 保留到 free；
- global off 时可以只打开目标 TID；其它 TID 不记录；
- global on 时 `<tid>off` 不能覆盖全局开关；
- 开启不追补旧 allocation，关闭不删除旧 entry；
- 普通 `echo` 带换行，必须使用 `echo -n`。

### 测试命令与逐 case 流程

普通专项固件启动到 `nsh>` 后执行：

```text
mm_record_test stack_test all
```

`all` 依次执行 M02-001..008、010..012，并明确把 M02-009、013 标为外部证据；
M02-009 必须在 expand 固件 fresh boot 后作为第一条 `stack_test` 单独执行：

```text
mm_record_test stack_test M02-009
```

各 case 的操作和判据：

| Case | 操作 | 完成判据 |
|---|---|---|
| M02-001 | 创建 private heap 并读取初始门控 | 编译默认值与 runtime 一致；default-off 为 0/0，default-on 为 1/1 |
| M02-002 | global off/on/off 期间保持 A/B/C 同时存活 | 只有 B 有 stack；关闭不清已有 B |
| M02-003 | controller、worker0、worker1 分别 on/off，再打开 global | TID 隔离和 global OR 语义成立 |
| M02-004 | 同一 worker 创建 before/target/after，按 TID+sequence 闭区间查询 | 仅 target 命中，PID/SEQNO/stack 一致 |
| M02-005 | 执行 R01..R06 | shrink、两类 grow、fallback 成功/失败、重复引用全部通过 |
| M02-006 | 同 callsite 分配三块并逐个 free | 完整 raw trace 对应 ref `3→2→1→0`；pool used 回到测试前基线 |
| M02-007 | 两个 worker 并发 alloc、realloc、free | 两条目标 trace 引用正确；node 和 pool 都回到基线 |
| M02-008 | 不存在 TID、非数字、坏 sequence 后继续分配 | 实测 `-EINVAL/0/0`，heap 仍可正常分配释放 |
| M02-009 | init size 4，四个唯一 noinline callsite | 第 4 条触发 capacity `4→8`，旧 entry 保留，free 后 used 为 0 |
| M02-010 | size 1..128 sweep；off/on 各 1,000 次 alloc/free | 请求边界、used 恢复、两种模式活动时长可读 |
| M02-011 | KASAN private heap 正常 alloc/write/free/unregister | active allocation 的 stack 指针非空且 depth 大于 0；无 KASAN report，used 回到 `600→600` |
| M02-012 | 打印 Note/coredump 组合 | 明确 `heap_note=0 note_driver=1 coredump_syslog=1`，不宣称采集 pool |
| M02-013 | 同固件执行现役外设回归 | GPIO/timer/oneshot/WDT/RTC 全部通过，无 assert/panic/复位 |

M02-006、007、009 的 pool 数据必须在 worker 静止时采集，并在主机解析。不能仅凭
设备打印 PASS，也不能假设全局 pool 原本为空。解析器同时要求 M02-006/009 的
`PARTIAL` 终态、M02-007 的 `PASS` 终态，每个 case 必须恰好出现一次预期终态；
重复终态或 `PASS`/`PARTIAL`/`SKIP`/`FAIL` 冲突均拒绝日志。

### USB2 实测数据

expand 固件 fresh boot 第一条 stack test 输出：

```text
M02-009 BEFORE: capacity=0 used=0
M02-009 AFTER step=1/2/3: capacity=4 used=1/2/3
M02-009 AFTER step=4: capacity=8 used=4
M02-009 ASSERT old_entry=present step=4
M02-009 FREE step=1/2/3/4: capacity=8 used=3/2/1/0
```

主测试固件输出 `SUMMARY pass=8 partial=5 skip=0 fail=0`。其中 partial 是已定义的
外部证据边界，不是失败：M02-006 需要主机 pool 解析，M02-009 需要另一次 fresh boot，
M02-010 需要结合制品/profile，M02-012 不包含 pool capture，M02-013 由随后外设命令
提供。主机解析确认：

```text
M02-006 target refcount: 3 -> 2 -> 1 -> 0
M02-006 pool used: 0 -> 1 -> 1 -> 1 -> 0
M02-007 pool used: 0 -> 2 -> 0
M02-007 ASSERT realloc=2 free=2 residual=0
```

M02-010 在当前 100 MHz system timer 下实测：1..128 B 两种模式共 256 次，实际
allocation size 为 16..128 B，private heap used 始终为 `600/600/600`；1,000 次
alloc/free 的 off/on 活动时长为 5.992/49.314 ms。depth 8/12 的 31 次有效 profile
进一步给出 depth 12 record-on malloc median/p95 为 20.480/20.894 us，realloc 为
24.269/24.410 us，1,000 次活动 median/p95 为 27.400/27.559 ms。controller/worker
最大栈水位为 2,564/820 B，最小余量为 1,436/1,188 B；首次全局 pool retained 为
4,128 B。

raw PC 使用同次 `final_nuttx` 离线解析，目标 trace 命中
`stack_alloc_same`、`stack_realloc`、`stack_worker_main`、`stack_case_duplicate`、
`mm_record_stack_test`、`mm_record_test_main`、`pthread_startup` 和 `nxtask_start`。

default-on 专项单独输出：

```text
MM_RECORD_STACK M02-001 default compile=1 runtime=1
MM_RECORD_STACK M02-001 PASS
```

### 回归与最终产品恢复

M02 测试固件随后执行：

```text
mcu_gpio_test -c edge --out /dev/gpio12 -n 3 -v
mcu_timer_test -c 001 -t 10000 -n 5 -e 5 -v
mcu_timer_test -c 002 -t 500000 -a 39 -b 79 -v
mcu_timer_test -c 005
oneshot -d 100000 /dev/oneshot
mcu_wdt_test -c 002 -t 1000 -p 3000 -i 500 -v
mcu_wdt_test -c 003 -t 1000
mcu_rtc_test -c all
```

GPIO edge、TIMER-001/002/005、oneshot、WDT-002/003 均输出 PASS；RTC 最终输出
`RTC test PASS (0 failures)`。测试结束后重新烧录标准 `nsh`，产品固件的 RTC 节点、
date 递增、random、GPIO、timer、oneshot 和 WDT 基线全部通过，模组最终保持产品固件。

### 限制

- pool 是进程全局资源，首次开启会保留 bucket/entry 内存；动态 off 不表示回收 pool。
- `backtrace_dump()` 没有运行时锁定接口，只能在本测试的静止窗口使用，不能作为产品
  并发查询 API。
- M02-010 是单轮 ops profile，不声称是完整 benchmark；跨版本性能决策应重新执行
  多轮 median/p95、realloc 和 CPU load profile。
- KASAN 下不得由测试代码遍历已 poison 的 free node；M02-011 只在 allocation
  存活时通过 test-only no-sanitize introspection 读取 allocator 元数据，确认 stack
  指针和 depth，再用 heap used 前后值和正常注销证明生命周期。malloc、write、free
  和 register/unregister 路径仍由 KASAN 检查。
- TLSF、mempool、task heap、kernel heap 和动态 IOB 不在本次 default allocator
  结论内，后续必须各自建立最大能力交集和专项测试。


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
| TIMER-001 | 10 ms、5 轮，最大误差 242 us（2.420%），PASS |
| TIMER-002 | divider 39/79 周期比 2.000，PASS |
| TIMER-005 | 非法请求、运行中更新和生命周期恢复，PASS |
| WDT-002 | 1,000 ms timeout、500 ms 喂狗，持续 3,017 ms 无复位，PASS |
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
