# BL616CL Generic KASAN 配置与验证

本文说明 BL616CL 如何启用 OpenVela generic heap KASAN，处理 BL616CL 在 BSS
清零前执行 C 代码的启动约束，并给出可重复的合法访问、heap 左右越界、
use-after-free、性能和最终产品回归流程。烧录沿用 SDK 统一流程，本文不重复烧录
步骤；运行流程从目标固件已经启动到 USB2 的 NSH 提示符开始。

## 背景

BL616CL 当前使用 RV32 flat build、default allocator 和单一内部 SRAM heap。
芯片适配层的 `up_allocate_heap()` 只返回链接脚本定义的 `__HeapBase` 到
`__HeapLimit`，实际分配和 KASAN 注册均由 OpenVela 通用 MM 层完成。

Generic KASAN 包含两部分：

- 编译器为普通 C/C++ 内存访问插入 `__asan_load*`、`__asan_store*` 检查；
- allocator 注册 heap region，在 region 尾部保存 descriptor 和 shadow，按分配、
  释放状态 poison 或 unpoison 对应地址。

本项验证使用预置 GCC 13.4.0。RV32 `rv32imafc/ilp32f` 实测接受
`-fsanitize=kernel-address` 和全部 KASAN 参数；受控读写代码分别生成
`__asan_load4_noabort` 和 `__asan_store4_noabort`。

## 正式方案与裁剪边界

### 正式产品配置

正式 `nsh` 配置启用：

```text
CONFIG_MM_KASAN=y
CONFIG_MM_KASAN_GENERIC=y
CONFIG_MM_KASAN_INSTRUMENT_ALL=y
CONFIG_MM_KASAN_REGIONS=2
CONFIG_MM_KASAN_WATCHPOINT=0
CONFIG_MM_KASAN_DISABLE_NULL_POINTER_CHECK=y
# CONFIG_MM_KASAN_GLOBAL is not set
# CONFIG_MM_KASAN_DISABLE_READS_CHECK is not set
# CONFIG_MM_KASAN_DISABLE_WRITES_CHECK is not set
# CONFIG_MM_KASAN_DISABLE_READ_PANIC is not set
# CONFIG_MM_KASAN_DISABLE_WRITE_PANIC is not set
# CONFIG_TESTING_KASAN is not set
```

`CONFIG_MM_KASAN_INSTRUMENT` 由 generic 模式自动选择。`REGIONS=2` 为系统 heap
和临时 `kasantest` custom heap 各留一个注册槽；正式产品虽然关闭测试 app，仍
保留该值，避免重新启用验收命令时遗漏容量调整。

RISC-V CMake 自动向普通 C/C++ 编译单元传播：

```text
-fsanitize=kernel-address
--param asan-stack=0
--param asan-instrumentation-with-call-threshold=0
--param asan-globals=0
```

读写检查和 fatal panic 均保持默认开启。`asan-stack=0` 表明本方案不检查栈对象；
`asan-globals=0` 与 `CONFIG_MM_KASAN_GLOBAL=n` 一致，不生成或依赖 BL616CL linker
中的 KASAN global section。

关闭 `CONFIG_MM_KASAN` 后，runtime、allocator hook、编译器插桩和启动 early
stop 均被裁掉。正式产品只关闭 `CONFIG_TESTING_KASAN` 时，KASAN 能力继续保留，
但 `kasantest` 命令、独立 archive、故意越界代码和命令字符串不进入制品。

### 测试配置

受控验收固件在正式配置上临时增加：

```text
CONFIG_TESTING_KASAN=y
CONFIG_TESTING_KASAN_PRIORITY=101
CONFIG_TESTING_KASAN_STACKSIZE=8192
CONFIG_TESTING_KASAN_PERF_HEAP_SIZE=32768
CONFIG_TESTING_KASAN_PERF_CYCLES=256
```

测试 app 依赖 generic、instrument-all、`SCHED_WAITPID` 和非 kernel build。每个
fault case 创建独立 child task 和 custom heap 注册窗口。预期 fault 如果没有触发、
测试函数正常返回，child 明确输出 `KASANTEST expected fault was not triggered` 并以
正常状态结束；controller 必须将其判为 `FAIL`。child 非零退出只能证明发生了故障，
controller 输出 `FAULT verification=required` 而不宣称通过；宿主侧必须继续核对
唯一 KASAN report、访问类型、大小、目标地址和 backtrace 后才能判 PASS。

## 调用链与启动 early stop

### 构建和运行调用链

```text
CONFIG_MM_KASAN_GENERIC
  -> select CONFIG_MM_KASAN_INSTRUMENT
  -> CONFIG_MM_KASAN_INSTRUMENT_ALL
     -> RISC-V Toolchain.cmake
        -> -fsanitize=kernel-address
        -> kasan_params.cmd
        -> 普通 C/C++ 生成 __asan_load* / __asan_store*

nx_start()
  -> up_allocate_heap(__HeapBase, __HeapLimit)
  -> kumm_initialize() / umm_initialize()
  -> mm_initialize_pool()
  -> mm_initialize_heap()
  -> kasan_register(heapbase, &heapsize)
     -> region descriptor 放在 region 尾部
     -> shadow 紧随 descriptor
     -> kasan_start()
     -> poison 整个 region
     -> 从 heapsize 扣除 descriptor + shadow
  -> allocator 建立 guard/free node
  -> 分配时 unpoison，释放时 poison
```

`nuttx/mm/kasan/CMakeLists.txt` 对 KASAN runtime 本身显式加入
`-fno-sanitize=kernel-address`，ALLSYMS 和多重链接生成源也显式关闭 sanitizer，
避免 runtime 自插桩和生成源污染。最终 ELF 必须同时满足：普通目标存在插桩调用、
`mm/libmm.a` 提供对应 `__asan_*` 定义、最终 ELF 没有未解析 sanitizer 符号。

### BL616CL warm reset 约束

BL616CL 的 `__bl616cl_start()` 在 `bl616cl_section_load()` 清零/装载 section 之前
调用多个普通 C 函数。warm reset 时，RAM 中旧的 `g_region_init`、region count 和
region 指针可能仍保留；如果插桩的早期 C 代码先执行，旧标志可能让检查逻辑访问
过期 region。

启动入口因此采用以下顺序：

```text
__start
  -> 建立 gp、sp、mscratch
  -> 关闭中断并设置 mtvec
  -> CONFIG_MM_KASAN_INSTRUMENT 下调用 kasan_stop()
  -> riscv_fpuconfig()
  -> __bl616cl_start()
     -> 普通 early C
     -> bl616cl_section_load()
     -> nx_start()
        -> 初始化 allocator
        -> kasan_register() 重新启动检查
```

`kasan_stop()` 位于未插桩 KASAN runtime 中，只把检查标志清零，不依赖尚未初始化的
heap。它必须放在 gp/sp/mtvec 有效之后、`riscv_fpuconfig()` 和所有普通 C 之前。

fatal 报告会先执行 `kasan_stop()`，再打印错误地址、大小、返回地址、shadow 和
backtrace，最后进入 `PANIC()`。因此同一个注册窗口中的后续访问不能作为第二份
检测证据。`kasantest` 每条命令结束后注销 custom heap，下一条命令重新执行
`mm_initialize()`/`kasan_register()`，从而建立新的检测窗口并重新 `kasan_start()`；
不得在一个 child 中串联多个预期 fault。

## 配置、savedefconfig 与构建

以下命令均在 SDK 根目录执行。`defconfig` 是生成文件，只通过 `.config` 和
`savedefconfig` 更新，不直接编辑。先加载仓库环境，确保后续 Ninja target 使用
仓库预置的 `savedefconfig`、CMake 和 Python 依赖，而不是宿主机偶然安装的命令：

```sh
source build/envsetup.sh
```

### 1. 建立关闭态对照

先完成一次 configure/build，再关闭 KASAN 和测试命令：

```sh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14

prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak \
  --file cmake_out/ai-m64l-32s-kit_nsh/.config \
  --disable TESTING_KASAN \
  --disable MM_KASAN
cmake --build cmake_out/ai-m64l-32s-kit_nsh -t savedefconfig

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

关闭态完成判据：clean build 成功；`.config` 不含已启用的 `MM_KASAN*`；没有
`kasan_params.cmd`；普通编译命令不含 `-fsanitize=kernel-address`；最终 ELF 不含
KASAN runtime 或插桩符号。

### 2. 写入正式配置并 clean build

```sh
prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak \
  --file cmake_out/ai-m64l-32s-kit_nsh/.config \
  --enable MM_KASAN \
  --enable MM_KASAN_GENERIC \
  --enable MM_KASAN_INSTRUMENT_ALL \
  --set-val MM_KASAN_REGIONS 2 \
  --set-val MM_KASAN_WATCHPOINT 0 \
  --enable MM_KASAN_DISABLE_NULL_POINTER_CHECK \
  --disable MM_KASAN_GLOBAL \
  --disable MM_KASAN_DISABLE_READS_CHECK \
  --disable MM_KASAN_DISABLE_WRITES_CHECK \
  --disable MM_KASAN_DISABLE_READ_PANIC \
  --disable MM_KASAN_DISABLE_WRITE_PANIC \
  --disable TESTING_KASAN
cmake --build cmake_out/ai-m64l-32s-kit_nsh -t savedefconfig

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

### 3. 构建临时测试固件

在正式配置的 `.config` 上临时启用测试 app，并保存后执行 clean build：

```sh
prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak \
  --file cmake_out/ai-m64l-32s-kit_nsh/.config \
  --enable TESTING_KASAN
cmake --build cmake_out/ai-m64l-32s-kit_nsh -t savedefconfig

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

测试结束后必须恢复正式产品并再次 clean build：

```sh
prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak \
  --file cmake_out/ai-m64l-32s-kit_nsh/.config \
  --disable TESTING_KASAN
cmake --build cmake_out/ai-m64l-32s-kit_nsh -t savedefconfig

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

## 静态制品核查

```sh
OUT=cmake_out/ai-m64l-32s-kit_nsh
TOOL=prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf

grep -E '^CONFIG_MM_KASAN|^# CONFIG_MM_KASAN|^CONFIG_TESTING_KASAN|^# CONFIG_TESTING_KASAN' \
  "$OUT/.config"
sed -n '1p' "$OUT/kasan_params.cmd"

grep -c '"command"' "$OUT/compile_commands.json"
grep -o -- '-fsanitize=kernel-address' "$OUT/compile_commands.json" | wc -l
grep -o -- '@[^ ]*kasan_params.cmd' "$OUT/compile_commands.json" | wc -l

"$TOOL-nm" -C "$OUT/mm/libmm.a" | \
  grep -E '__asan_(load|store|report)|kasan_(start|stop|register|unregister)'
"$TOOL-nm" -C "$OUT/final_nuttx" | \
  grep -E '__asan_(load|store)|kasan_(start|stop|register|unregister)'
test -z "$("$TOOL-nm" -u "$OUT/final_nuttx" | grep -E '__asan|__ubsan')"

"$TOOL-objdump" -d "$OUT/final_nuttx" | \
  sed -n '/<__start>:/,/^$/p'
"$TOOL-readelf" -S "$OUT/final_nuttx" | grep -E 'kasan|\.text|\.data|\.bss'
"$TOOL-nm" -n "$OUT/final_nuttx" | grep -E '__Heap(Base|Limit)'
"$TOOL-size" "$OUT/final_nuttx"
sha256sum "$OUT/nuttx.bin"
```

正式产品额外执行裁剪门禁：

```sh
OUT=cmake_out/ai-m64l-32s-kit_nsh
test ! -e "$OUT/apps/testing/mm/kasantest/libapps_kasantest.a"
! strings "$OUT/final_nuttx" | grep -E 'KASANTEST|kasantest'
```

完成判据：

- 正式 `.config` 与本文正式配置一致；
- `kasan_params.cmd` 只包含 `asan-stack=0`、threshold 0、`asan-globals=0`；
- 普通编译命令含 sanitizer 和 response file；runtime/ALLSYMS 自身可额外出现
  `-fno-sanitize=kernel-address`；
- `mm/libmm.a` 和最终 ELF 定义 `__asan_*`，最终 ELF 没有未解析 sanitizer 符号；
- `__start` 在 `riscv_fpuconfig` 和 `__bl616cl_start` 前调用 `kasan_stop`；
- 不存在 `.kasan.global`，链接脚本 RAM ASSERT 全部通过；
- 正式产品不存在测试 archive、命令符号和字符串。

## USB2 单 fd 运行流程

Ai-M64L-32S-Kit 打开串口时会因 USB-UART modem line 瞬时变化而重启。完整负测、
warm reset 和回归使用同一个 `/dev/ttyUSB2` fd：

宿主侧验收流程如下：

1. 以 2,000,000 baud、8N1、raw mode、无流控且无 `HUPCL` 打开一次 USB2。
2. open 后立即设置运行态 `DTR=1, RTS=0`，等待 `NuttShell (NSH)` 和 `nsh>`。
3. 逐字节发送命令并等待下一次 `nsh>`，期间不 close/reopen fd。
4. 每个 fault case 单独发送一条 `kasantest` 命令；核对 target、唯一 KASAN
   report、1 B write、backtrace、child status 和 `FAULT verification=required`。
5. 测试模式在同一 fd 上执行三次 warm reset：`DTR up -> 50 ms -> RTS up ->
   50 ms -> RTS down -> 100 ms`，每轮等待两个启动标志并拒绝 early fault。
6. 逐条运行 GPIO、timer、oneshot、WDT 回归；每条命令独立核对命令回显、返回
   `nsh>`、对应 case 的 PASS/完成标志，并拒绝该命令输出中的 KASAN report。
   oneshot 必须同时出现 `Starting oneshot timer` 和 `Finished`；WDT-002、WDT-003
   分别核对各自 PASS 行，不能用共享 summary 代替。
7. 退出前恢复 `DTR=1, RTS=0`，写入原始日志，再关闭 fd；任一检查失败时进程
   返回非零，并在 `failures=[...]` 中列明原因。

烧录沿用 SDK 的标准分区烧录流程，并在开始上述步骤前确认主机与设备校验一致。
不得用反复启动 `picocom` 的方式替代单 fd 验收，因为每次 open 都会额外复位模组。

## 受控 KASAN 验收

在测试固件的同一单 fd 会话中依次执行：

```text
kasantest 21
kasantest 1
kasantest 2
kasantest 3
kasantest 21
kasantest 34
kasantest 35
```

### 合法访问

case 21 在三个 fatal case 前后各执行一次：

```text
KASANTEST access: case=heap legal memchr requested=1 base=0x60fcdd00 usable=20 target=0x60fcdd13
heap legal memchr spending 0.3968000s
KASANTEST result: case=21 name=heap legal memchr PASS status=0
...
KASANTEST access: case=heap legal memchr requested=1 base=0x60fcdd00 usable=20 target=0x60fcdd13
heap legal memchr spending 0.3909000s
KASANTEST result: case=21 name=heap legal memchr PASS status=0
```

两次窗口均没有 KASAN report。第二次合法访问通过，证明三个 fatal case 结束后新的
custom heap 注册窗口已经恢复检测状态且正常访问未被误报。

### Heap 左越界

```text
KASANTEST access: case=heap underflow requested=1 base=0x60fcdd00 usable=20 target=0x60fcdcff
kasan_report: kasan detected a write access error, address at 0x60fcdcff,size is 1
sched_dumpstack: ... test_heap_underflow+0x78/0xa6
sched_dumpstack: ... run_child+0x80/0x10e
KASANTEST result: case=1 name=heap underflow FAULT status=256 verification=required
```

### Heap 右越界

```text
KASANTEST access: case=heap overflow requested=1 base=0x60fcdd00 usable=20 target=0x60fcdd14
kasan_report: kasan detected a write access error, address at 0x60fcdd14,size is 1
sched_dumpstack: ... test_heap_overflow+0x7c/0xaa
sched_dumpstack: ... run_child+0x80/0x10e
KASANTEST result: case=2 name=heap overflow FAULT status=256 verification=required
```

### Use-after-free

```text
KASANTEST access: case=heap use after free requested=1 base=0x60fcdd00 usable=20 target=0x60fcdd00
kasan_report: kasan detected a write access error, address at 0x60fcdd00,size is 1
sched_dumpstack: ... test_heap_use_after_free+0x82/0xb0
sched_dumpstack: ... run_child+0x80/0x10e
KASANTEST result: case=3 name=heap use after free FAULT status=256 verification=required
```

三个 fault case 的完成判据相同：预打印 target 与 report 地址完全一致，访问类型为
write、大小为 1 B，只出现一份 report，backtrace 包含对应测试函数和 `run_child`，
child 非零退出后 controller 只输出 `FAULT status=256 verification=required`。宿主
宿主侧完成其余匹配后才把该 case 判为 PASS；缺少 report、地址/类型/大小不匹配、
backtrace 不匹配，或预期 fault 未触发并正常返回，均必须判 FAIL。

### 性能样例

```text
Kasan insert performance spending 2.546035000s
KASANTEST result: case=34 name=Kasan insert performance PASS status=0
Kasan algorithm performance spending 2.255525000s
KASANTEST result: case=35 name=Kasan algorithm performance PASS status=0
```

这些数字是当前固件、当前配置下 32 KiB buffer、256 个 cycle 的样例，只用于确认
性能用例可执行和记录本次结果，不能外推为通用 KASAN 百分比开销。

## Warm reset 验证

测试固件保持同一 USB2 fd，连续执行三次固定 DTR/RTS warm reset。每轮捕获 27 B
启动输出，均同时匹配：

```text
NuttShell (NSH)
nsh>
```

三轮均未在 early C、section load 或 allocator 注册前出现 KASAN report、assert 或
panic，证明 startup early stop 能清除 warm reset 遗留的启用标志，并由后续系统
heap 注册正常重新启动检测。

## 最终产品裁剪与外设回归

关闭 `CONFIG_TESTING_KASAN`、执行 `savedefconfig` 和最终 clean build 后，沿用 SDK
流程烧录正式产品。先执行 `help`：Builtin Apps 中不存在 `kasantest`；静态检查也
确认测试 archive 和 `KASANTEST`/`kasantest` 字符串均无残留。

保持正式 KASAN 开启，在同一 USB2 会话依次执行完整回归：

```text
mcu_gpio_test -c edge --out /dev/gpio12 -n 3 -v
mcu_timer_test -c 001 -t 100000 -n 5 -e 5 -v
mcu_timer_test -c 002 -t 500000 -a 39 -b 79 -v
mcu_timer_test -c 005
oneshot -d 100000 /dev/oneshot
mcu_wdt_test -c 002 -t 1000 -p 3000 -i 500 -v
mcu_wdt_test -c 003 -t 1000
echo ST012_FINAL_ALIVE
```

实测关键输出：

```text
[GPIO-edge] PASS rejected invalid operations and recovered for 3 cycles

[TIMER-001] overflow period accuracy timeout=100000us rounds=5 tol=5.00%
  round 1 interval=99194.0us err=-806.0us
  round 2 interval=100075.0us err=+75.0us
  round 3 interval=100009.0us err=+9.0us
  round 4 interval=99985.0us err=-15.0us
  round 5 interval=100577.0us err=+577.0us
  RESULT max_err=806.0us (0.806%) tol=5000.0us (5.00%)
  [TIMER-001] PASS accuracy within tolerance

[TIMER-002] clock prescaler effect (div 39 vs 79)
  div=39 period=0.4993s
  div=79 period=1.0000s
  ratio period_b/period_a=2.003 (expected 2.000, tol +/-5%)
  [TIMER-002] PASS prescaler takes effect

[TIMER-005] PASS rejected requests preserved state; live update fired; lifecycle recovered

Starting oneshot timer with delay 100000 microseconds
Finished

[WDT-002] Periodic keepalive, no reset
  fed #6 elapsed=3026ms timeleft=1000ms
  PASS: fed 6 times over 3026ms, no reset; watchdog stopped

[WDT-003] Lifecycle and rejected requests, no reset
  PASS: invalid/live changes rejected; duplicate lifecycle preserved state

ST012_FINAL_ALIVE
```

全部用例返回 NSH，全程没有意外 KASAN report。

## 制品与 heap 开销

三组数据均来自 clean build：

| 制品或内存 | KASAN 关闭 | 测试态 | 正式产品 | 正式比关闭 |
|---|---:|---:|---:|---:|
| 构建目标 | 1217/1217 | 1220/1220 | 1218/1218 | - |
| `final_nuttx` | 758,412 B | 855,384 B | 833,792 B | +75,380 B |
| text | 364,894 B | 454,348 B | 440,020 B | +75,126 B |
| data | 14,848 B | 15,424 B | 15,040 B | +192 B |
| bss | 20,076 B | 30,332 B | 20,092 B | +16 B |
| `nuttx.bin` | 385,472 B | 475,424 B | 460,704 B | +75,232 B |
| linker raw heap | 274,192 B | 259,072 B | 269,696 B | -4,496 B |

测试态 `nuttx.bin` SHA256：

```text
7b4df89f4789b8950b30efa19bd85f243ddaf241917458e0a69c0778f51b7dbd
```

最终产品 `nuttx.bin` SHA256：

```text
81a56dc3580a4b535f0a98290d2a6251797e4ebeb624b0ae09bd1702b244ea37
```

正式产品 raw heap 为 269,696 B。RV32 generic KASAN 在该 region 尾部扣除 8,440 B：
12 B region descriptor 加 8,428 B shadow，allocator 最终注册 region 为 261,256 B。
相对关闭态 274,192 B，linker raw heap 因制品 RAM 增长减少 4,496 B；再扣除
descriptor/shadow 后，allocator region 合计减少 12,936 B。

## 限制与判定边界

- 本结论只覆盖已注册的 generic heap region；不覆盖 stack、global、未注册内存、
  外设寄存器或初始化前访问。
- `CONFIG_MM_KASAN_GLOBAL` 保持关闭。BL616CL linker 当前没有 KASAN global section，
  不能把 `asan-globals=0` 的构建结果宣称为全局变量检查。
- null pointer 检查显式关闭；不能用 null dereference 作为本方案的正向验收。
- fatal report 会全局 `kasan_stop()`。只有后续新的 `kasan_register()` 才重新启动
  检查；产品发生首个 fatal 后，不能假设同一运行窗口仍持续受保护。
- 默认 read/write panic 开启。若以后关闭 panic，报告后继续运行的语义和测试判据
  会变化，必须重新设计验收，不能复用本次 `status=256` 判据。
- 全镜像插桩显著增加 text、运行路径指令和 heap shadow 开销。上述尺寸和性能只适用
  当前 GCC、配置与固件，产品代码、工具链或优化级别变化后必须重新量化。
- `REGIONS=2` 是当前系统 heap 加测试 custom heap 的容量，不代表已支持 PSRAM、
  多 heap 或 protected/kernel 双 heap；BL616CL 的 `riscv_addregion()` 当前仍为空。
- 测试 app 含故意越界/UAF，只能在受控验收固件中启用，不得加入启动脚本或保留在
  正式产品中。
