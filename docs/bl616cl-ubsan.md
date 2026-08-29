# BL616CL UBSAN 配置与验证

本文说明 BL616CL 如何使用 OpenVela UBSAN runtime，对指定目标进行局部插桩，
并给出可重复的合法计算、有符号加法溢出、越界移位、正式产品裁剪和外设回归
流程。运行环境为 Ai-M64L-32S-Kit、`/dev/ttyUSB2`、2,000,000 baud。

## 背景

Undefined Behavior Sanitizer（UBSAN）由编译器插入检查点，在运行时调用
`__ubsan_handle_*` handler 输出未定义行为类型和源码位置。OpenVela 的
`nuttx/mm/ubsan/ubsan.c` 提供 runtime，RISC-V CMake 可通过
`CONFIG_MM_UBSAN_ALL` 把 `CONFIG_MM_UBSAN_OPTION` 传播到全部 C/C++ 目标。

BL616CL 当前正式基线已经启用 generic KASAN。直接叠加全镜像
`-fsanitize=undefined` 存在两个确定问题：

- clean build 链接时 cacheable RAM 溢出 67,920 B；
- GCC 13.4.0 默认 `undefined` 可能引用当前 NuttX runtime 未实现的
  `__ubsan_handle_vla_bound_not_positive`、
  `__ubsan_handle_nonnull_return_v1` 和
  `__ubsan_handle_missing_return`。

显式加入 `float-cast-overflow` 还会引用未实现的
`__ubsan_handle_float_cast_overflow`。因此不能把“工具链接受
`-fsanitize=undefined`”等同于“当前 runtime 支持任意 UBSAN 类别”。

## 正式方案与裁剪边界

### 正式产品配置

正式 `nsh` 配置为：

```text
CONFIG_MM_UBSAN=y
# CONFIG_MM_UBSAN_ALL is not set
# CONFIG_MM_UBSAN_TRAP_ON_ERROR is not set
# CONFIG_MM_UBSAN_DUMMY is not set
# CONFIG_BL_OS_FEATURE_TESTS_UBSAN is not set
```

该配置只把 UBSAN runtime 编入 `libmm.a`，不向普通编译单元传播 sanitizer
参数。正式固件没有 `__ubsan_handle_*` 引用时，链接器不会把 runtime object
拉入最终 ELF，因此默认运行路径、代码尺寸和 RAM 均不增加。

需要检查的独立目标必须显式添加当前 runtime 已闭包的 sanitizer 类别。例如本文
测试 app 只使用：

```text
-fsanitize=signed-integer-overflow,shift
```

关闭 `CONFIG_MM_UBSAN` 后，runtime 不再进入 `libmm.a`。关闭
`CONFIG_BL_OS_FEATURE_TESTS_UBSAN` 后，测试命令、独立 archive、故意触发未定义
行为的代码和字符串均不进入固件。

### 测试配置

受控验收固件在正式配置上临时增加：

```text
CONFIG_BL_OS_FEATURE_TESTS_UBSAN=y
CONFIG_BL_OS_FEATURE_TESTS_UBSAN_PRIORITY=100
CONFIG_BL_OS_FEATURE_TESTS_UBSAN_STACKSIZE=2048
```

测试选项依赖 flat builtin、`MM_UBSAN=y`、`MM_UBSAN_ALL=n`，并拒绝 trap 和
dummy 模式。只有测试 app 的编译命令增加
`-fsanitize=signed-integer-overflow,shift`；KASAN 参数仍按正式基线传播到普通
目标，两种 sanitizer 在该 app 中同时生效。

测试命令包含三个场景：

```text
ubsan_test legal
ubsan_test add-overflow
ubsan_test shift-out-of-bounds
```

fault case 在 handler 返回后输出 `FAULT verification=required`。该字符串只表示
故障注入函数执行到 handler 之后，不能单独判 PASS；宿主 runner 还必须核对唯一
UBSAN report、准确 reason、`ubsan_test.c` 源码位置、无 panic/assert、返回
`nsh>`，以及后续合法用例仍然通过。

## 模式选择

### Recover 模式

GCC 13.4.0 默认 UBSAN 为 recover 模式。当前 runtime 的
`add-overflow` 和 `shift-out-of-bounds` handler 输出报告后返回，适合受控局部变量
负测和后续存活验证。

Recover 只表示 handler 返回，不保证任意未定义操作之后的程序状态仍可靠。本文只
触发编译器可稳定生成检查点、且不访问 flash、固定地址、任意指针或外设寄存器的
局部整数操作。

### 不使用 trap

`CONFIG_MM_UBSAN_TRAP_ON_ERROR` 会让 GCC 为 RV32 生成 `ebreak`，由 RISC-V
breakpoint exception 进入通用 panic。该路径没有 UBSAN reason 和源码位置，只能
证明异常处理工作，不满足本项诊断验收。

### 不使用 abort 和 dummy

`-fno-sanitize-recover=undefined` 会引用当前 runtime 未实现的 `*_abort` handler，
不能链接为完整方案。`CONFIG_MM_UBSAN_DUMMY` 只保留空 handler，无法证明检测和
报告工作。因此两者均不进入本配置。

## 调用链

```text
CONFIG_MM_UBSAN
  -> nuttx/mm/ubsan/ubsan.c
  -> libmm.a 提供 __ubsan_handle_*

CONFIG_BL_OS_FEATURE_TESTS_UBSAN
  -> ubsan_test.c
  -> -fsanitize=signed-integer-overflow,shift
  -> 编译器插入 __ubsan_handle_add_overflow
     和 __ubsan_handle_shift_out_of_bounds
  -> 链接器从 libmm.a 拉入对应 runtime object
  -> handler 通过 _alert 输出 reason、文件、行和列
  -> recover 返回测试函数
  -> 测试输出 FAULT verification=required
  -> 返回 NSH
```

正式产品关闭测试 app 后没有 handler 引用。`libmm.a` 仍含定义，但
`final_nuttx` 不含 handler、测试命令或 sanitizer 插桩。

## 配置与构建

以下命令均在 SDK 根目录执行。`defconfig` 是生成文件，使用 `.config` 和
`savedefconfig` 更新。

### 1. 建立 UBSAN 关闭态对照

```sh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14

prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak \
  --file cmake_out/ai-m64l-32s-kit_nsh/.config \
  --disable BL_OS_FEATURE_TESTS_UBSAN \
  --disable MM_UBSAN
cmake --build cmake_out/ai-m64l-32s-kit_nsh -t savedefconfig

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

完成判据：clean build 退出码为 0；`.config` 中 UBSAN runtime 和测试 app 均
关闭；`libmm.a`、最终 ELF、编译命令和字符串均不存在 UBSAN 内容。

### 2. 构建受控测试固件

```sh
prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak \
  --file cmake_out/ai-m64l-32s-kit_nsh/.config \
  --enable MM_UBSAN \
  --disable MM_UBSAN_ALL \
  --disable MM_UBSAN_TRAP_ON_ERROR \
  --disable MM_UBSAN_DUMMY \
  --enable BL_OS_FEATURE_TESTS_UBSAN
cmake --build cmake_out/ai-m64l-32s-kit_nsh -t savedefconfig

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

### 3. 恢复正式产品并 clean build

```sh
prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak \
  --file cmake_out/ai-m64l-32s-kit_nsh/.config \
  --disable BL_OS_FEATURE_TESTS_UBSAN
cmake --build cmake_out/ai-m64l-32s-kit_nsh -t savedefconfig

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

## 静态制品核查

### 测试固件

```sh
OUT=cmake_out/ai-m64l-32s-kit_nsh
TOOL=prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf

grep -E 'CONFIG_(MM_UBSAN|BL_OS_FEATURE_TESTS_UBSAN)' "$OUT/.config"
grep -n -- '-fsanitize=signed-integer-overflow,shift' \
  "$OUT/compile_commands.json"

"$TOOL-nm" "$OUT/final_nuttx" | \
  grep -E '__ubsan_handle_(add_overflow|shift_out_of_bounds)'
test -z "$("$TOOL-nm" -u "$OUT/final_nuttx" | grep __ubsan_handle_)"
"$TOOL-size" "$OUT/final_nuttx"
sha256sum "$OUT/final_nuttx" "$OUT/nuttx.bin" "$OUT/nuttx.whole.bin"
```

完成判据：只有 `ubsan_test.c` 编译命令包含本文两类插桩；最终 ELF 定义两个
handler 且没有未定义 UBSAN 符号。

### 正式产品

```sh
OUT=cmake_out/ai-m64l-32s-kit_nsh
TOOL=prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf

test ! -e "$OUT/apps/vendor/bouffalolab/apps/os_feature_tests/ubsan/libapps_ubsan_test.a"
! strings "$OUT/final_nuttx" | grep -E 'UBSAN_TEST|ubsan_test'
! grep -E -- '-fsanitize=(signed-integer-overflow|shift|undefined)' \
  "$OUT/compile_commands.json"
! "$TOOL-nm" "$OUT/final_nuttx" | grep __ubsan_handle_
"$TOOL-nm" "$OUT/mm/libmm.a" | \
  grep -E '__ubsan_handle_(add_overflow|shift_out_of_bounds)'
```

完成判据：测试 archive、命令、字符串、插桩和最终 handler 均被裁掉；runtime
定义只留在 `libmm.a`，供以后显式插桩目标按引用拉入。

## USB2 单 fd 运行流程

Ai-M64L-32S-Kit 打开 USB-UART 时会因 modem line 瞬时变化而重启。测试使用仓库
runner，在同一个 fd 内完成启动、命令和全部回归：

测试固件：

```sh
python3 vendor/bouffalolab/tools/ai-m64l-32s-kit/ubsan_validate.py \
  --mode test \
  --port /dev/ttyUSB2 \
  --baudrate 2000000 \
  --log ubsan-test-runtime.log
```

正式产品：

```sh
python3 vendor/bouffalolab/tools/ai-m64l-32s-kit/ubsan_validate.py \
  --mode product \
  --port /dev/ttyUSB2 \
  --baudrate 2000000 \
  --log ubsan-product-runtime.log
```

runner 的固定流程如下：

1. 以 2,000,000 baud、8N1、raw mode、无流控且无 `HUPCL` 打开 USB2 一次。
2. open 后立即设置运行态 `DTR=1, RTS=0`，等待 `NuttShell (NSH)` 和 `nsh>`。
3. 测试模式依次运行 legal、add-overflow、legal、shift-out-of-bounds、legal；
   每条命令等待回显之后的新 `nsh>`，不接受前一条命令残留的 prompt。
4. fault case 核对唯一报告、准确 reason、文件名和非零行列；legal case 拒绝任何
   UBSAN report。两类 fault 总报告数必须严格等于 2。
5. 正式模式先运行 `help`，确认 Builtin Apps 中不存在 `ubsan_test`。
6. 两种模式均逐条执行 GPIO、timer、oneshot、WDT 回归；每条命令独立核对自己的
   PASS/完成标志、返回 `nsh>`，并拒绝 UBSAN、panic、assert 和寄存器转储。
7. 输出 `ST013_FINAL_ALIVE`，退出前恢复 `DTR=1, RTS=0`，写入原始串口数据后
   关闭 fd。任一条件不满足时 runner 返回非零并列出 `failures`。

## 受控 UBSAN 实测

测试固件启动后依次执行：

```text
ubsan_test legal
ubsan_test add-overflow
ubsan_test legal
ubsan_test shift-out-of-bounds
ubsan_test legal
```

### 合法对照

三次 legal 均得到相同结果，且窗口内没有 UBSAN report：

```text
UBSAN_TEST BEGIN case=legal
UBSAN_TEST value=42
UBSAN_TEST RESULT case=legal PASS
nsh>
```

第一次证明正常加法不误报；两次 fault 之后分别再次通过，证明 recover handler 返回
后 NSH 和测试 app 仍可继续执行合法路径。

### 有符号加法溢出

```text
UBSAN_TEST BEGIN case=add-overflow
ubsan_prologue: UBSAN: add-overflow in /home/miot/Work/miot/code/bl_vela_sdk/apps/vendor/bouffalolab/apps/os_feature_tests/ubsan/ubsan_test.c:39:16
UBSAN_TEST RESULT case=add-overflow FAULT verification=required
nsh>
```

该窗口只出现一条 `add-overflow` 报告，文件、行和列均存在；没有 panic、assert、
寄存器转储或重启。

### 越界移位

```text
UBSAN_TEST BEGIN case=shift-out-of-bounds
ubsan_prologue: UBSAN: shift-out-of-bounds in /home/miot/Work/miot/code/bl_vela_sdk/apps/vendor/bouffalolab/apps/os_feature_tests/ubsan/ubsan_test.c:44:16
__ubsan_handle_shift_out_of_bounds: shift exponent 32 is too large for 32-bit type 'long int'
UBSAN_TEST RESULT case=shift-out-of-bounds FAULT verification=required
nsh>
```

该窗口只出现一条 `shift-out-of-bounds` 报告，并准确说明 32 位类型的移位指数
32 越界；没有 panic、assert、寄存器转储或重启。

## 外设回归实测

测试固件和正式产品均按以下顺序执行：

```text
mcu_gpio_test -c edge --out /dev/gpio12 -n 3 -v
mcu_timer_test -c 001 -t 100000 -n 5 -e 5 -v
mcu_timer_test -c 002 -t 500000 -a 39 -b 79 -v
mcu_timer_test -c 005
oneshot -d 100000 /dev/oneshot
mcu_wdt_test -c 002 -t 1000 -p 3000 -i 500 -v
mcu_wdt_test -c 003 -t 1000
echo ST013_FINAL_ALIVE
```

正式产品的关键数据如下：

```text
[GPIO-edge] PASS rejected invalid operations and recovered for 3 cycles

[TIMER-001] overflow period accuracy timeout=100000us rounds=5 tol=5.00%
  round 1 interval=99173.0us err=-827.0us
  round 2 interval=100092.0us err=+92.0us
  round 3 interval=99993.0us err=-7.0us
  round 4 interval=99992.0us err=-8.0us
  round 5 interval=100021.0us err=+21.0us
  RESULT max_err=827.0us (0.827%) tol=5000.0us (5.00%)
  [TIMER-001] PASS accuracy within tolerance

[TIMER-002] clock prescaler effect (div 39 vs 79)
  div=39 period=0.4999s
  div=79 period=1.0000s
  ratio period_b/period_a=2.000 (expected 2.000, tol +/-5%)
  [TIMER-002] PASS prescaler takes effect

[TIMER-005] PASS rejected requests preserved state; live update fired; lifecycle recovered

Starting oneshot timer with delay 100000 microseconds
Finished

[WDT-002] Periodic keepalive, no reset
  fed #6 elapsed=3026ms timeleft=1000ms
  PASS: fed 6 times over 3026ms, no reset; watchdog stopped

[WDT-003] Lifecycle and rejected requests, no reset
  PASS: invalid/live changes rejected; duplicate lifecycle preserved state

ST013_FINAL_ALIVE
```

全部命令返回 NSH，正式产品全程没有 UBSAN report。`help` 中不存在
`ubsan_test`。

## 构建、制品与开销

三组数据均来自同一 KASAN 正式基线：

| 制品或资源 | UBSAN 关闭 | 测试固件 | 正式产品 | 正式比关闭 |
|---|---:|---:|---:|---:|
| clean build 目标 | 1218/1218 | 1221/1221 | 1219/1219 | - |
| `final_nuttx` | 833,792 B | 838,692 B | 833,792 B | 0 B |
| text | 440,028 B | 442,668 B | 440,028 B | 0 B |
| data | 15,040 B | 15,156 B | 15,040 B | 0 B |
| bss | 20,092 B | 20,088 B | 20,092 B | 0 B |
| `nuttx.bin` | 460,720 B | 463,472 B | 460,720 B | 0 B |
| linker raw heap | 269,696 B | 269,584 B | 269,696 B | 0 B |

测试固件相对正式产品增加：ELF 4,900 B、text 2,640 B、data 116 B，
`nuttx.bin` 2,752 B；其 2,048 B task stack 是命令运行时开销，不计入静态 bss。

测试固件 SHA256：

```text
final_nuttx  ed68630741d0266726cf7688bd5197578dc346a729fa3cba64095dc9ee850c1b
nuttx.bin    c6358ac23954eefb26b26325fb1ce3f06bc4968c7401be170e0d3000151aa3af
whole image  0fdc71969f38864f5c142c25f3643a30ee9a793a589538b30d7dabf3c649e8a4
```

烧录并实测的正式产品 SHA256：

```text
final_nuttx  80db2f8d4a0bb09b22872e7a1078e83677b68c75980593a00fc32695be50996c
nuttx.bin    d4a15d9c4d038919558468d1d4d02036e5953fe2466580a62a5c73ba79abae9d
whole image  305b6427e66ddfc64f375f908eeb765469a68b793f86c78eab8902a4124cddc7
```

正式产品和 UBSAN 关闭态尺寸完全相同，是因为没有插桩引用时 runtime object 被
archive 链接规则裁掉。这不代表 `MM_UBSAN_ALL` 没有开销；全镜像方案已实际导致
67,920 B RAM 溢出，不能用于当前 BL616CL 配置。

## 限制与判定边界

- 本次只验证 `signed-integer-overflow` 和 `shift`，不能外推为 GCC 默认
  `undefined` 全类别已支持。
- 当前 runtime 缺少 VLA bound、nonnull return、missing return 和 float cast
  overflow handler；向其他目标添加 sanitizer 类别前必须先做未定义符号闭包检查。
- Recover handler 返回后仍可能继续执行原本非法的操作。产品只应对可接受该语义的
  目标局部启用，并为故障后的数据状态另设判定，不能把“返回 NSH”当作通用安全性。
- trap 模式只进入 RISC-V breakpoint panic，不提供本文报告；abort 模式缺少
  `*_abort` handler；dummy 模式没有诊断内容。
- 本次未覆盖 alignment、bounds、除零、pointer overflow、C++、启动 early C、
  flash、固定地址或外设寄存器。特别是除零 handler 当前日志格式不完整，不纳入
  首轮负测。
- 全镜像 UBSAN 与当前全镜像 KASAN 叠加会显著增加代码、RAM 和运行时检查路径。
  当前板只能使用 runtime 加目标局部插桩，`MM_UBSAN_ALL` 必须保持关闭。
- 测试 app 含故意未定义行为，只能在受控验收固件中启用，不得加入启动脚本或保留
  在正式产品中。
