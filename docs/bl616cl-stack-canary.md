# BL616CL 编译器栈保护与受控负测

本文说明 BL616CL 如何启用 OpenVela 通用 stack canary，验证编译器插桩和
`__stack_chk_fail` 路径，并给出可重复的正常、受控破坏和外设回归流程。烧录流程
由 SDK 通用工具文档统一维护，本文从配置和构建开始，运行流程从固件已经启动到
NSH 提示符开始。

## 背景

函数返回前如果局部数组越界覆盖了返回地址或保存寄存器，系统可能在错误位置继续
执行，原始破坏点很难定位。`CONFIG_STACK_CANARIES` 让 RISC-V CMake 工具链对
所有 C 函数加入 `-fstack-protector-all`：函数入口保存 guard，函数返回前比较
guard，发现变化后调用 `__stack_chk_fail()`。

BL616CL 不需要私有 arch hook。通用 libc 提供弱符号 guard 和 fail 实现，RISC-V
工具链提供插桩，现有 assert、stack dump 和 backtrace 路径负责输出故障现场。

该能力与 `STACK_COLORATION` 的用途不同：

| 能力 | 检查对象 | 检查时机 |
|---|---|---|
| stack canary | 函数栈帧内 guard 是否被覆盖 | 受保护函数返回前 |
| stack coloration | 任务栈历史最大使用量 | 查询高水位时 |
| stack margin | 当前剩余栈是否低于门限 | 上下文切换等检查点 |

## 方案与裁剪边界

正式 `nsh` 配置只启用产品能力：

```text
CONFIG_STACK_CANARIES=y
```

RISC-V 构建据此为所有 C 编译单元加入：

```text
-fstack-protector-all
```

关闭该选项后，编译参数、guard 引用和 fail 路径均可被裁掉。正式配置不启用负测
命令：

```text
# CONFIG_BL_OS_FEATURE_TESTS_STACK_CANARY is not set
```

需要复现受控破坏时，临时启用：

```text
CONFIG_BL_OS_FEATURE_TESTS_STACK_CANARY=y
```

测试 app 默认关闭，且依赖 `BUILD_FLAT`、`BUILTIN` 和 `STACK_CANARIES`。它只提供
手工命令，不加入 `rcS`，因此不会在正常启动时自动破坏栈。关闭后独立 archive、
命令注册和测试源码均不进入最终固件。

## 实现路径

1. `nuttx/arch/risc-v/src/cmake/Toolchain.cmake` 根据
   `CONFIG_STACK_CANARIES` 添加 `-fstack-protector-all`。
2. `nuttx/libs/libc/assert/lib_stackchk.c` 提供弱
   `__stack_chk_guard` 和 `__stack_chk_fail()`。
3. fail 实现调用 `PANIC()`，进入现有 RISC-V assert、寄存器、栈和 backtrace
   输出路径。
4. `apps/os_feature_tests/stack_canary/` 提供默认关闭的 `safe` 和 `corrupt`
   两条测试路径。

当前 guard 是弱 `const` 对象，值为 `&__stack_chk_guard`，链接在 flash `.text`
中。它在 `.data` 搬运前即可读取，因此 `__bl616cl_start`、CPU early init、memory
early init 和 section load 等早期 C 函数也能使用 stack protector。

这一实现能检测非定向栈覆盖，但 guard 地址固定且可预测，不能作为高熵、抗定向
攻击的安全 canary。若以后改为随机 RAM guard，必须先解决 guard 在 section load
前的初始化顺序，不能直接替换当前对象。

## 配置与构建

### 正式产品构建

```sh
python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

完成判据：

- 构建退出码为 0，生成 `final_nuttx`、`nuttx.bin` 和
  `flash_prog_cfg.ini`；
- `.config` 含 `CONFIG_STACK_CANARIES=y`；
- 全部 C 编译命令含 `-fstack-protector-all`；
- ELF 含 `__stack_chk_guard` 和 `__stack_chk_fail`；
- ELF 不含 `stack_canary_test_main`，测试 archive 不存在。

核对命令：

```sh
grep -o -- '-fstack-protector-all' \
  cmake_out/ai-m64l-32s-kit_nsh/compile_commands.json | wc -l
grep -c '"command"' \
  cmake_out/ai-m64l-32s-kit_nsh/compile_commands.json
prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf-nm \
  -S cmake_out/ai-m64l-32s-kit_nsh/final_nuttx | \
  grep -E '__stack_chk_(guard|fail)|stack_canary_test'
```

本次正式构建两项编译命令计数均为 1,161；ELF 只包含 guard 和 fail，不包含测试
命令符号。

### 临时负测构建

先完成一次正式 configure/build，再临时打开测试 app，并通过 `savedefconfig` 让
clean configure 使用相同测试配置：

```sh
prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak \
  --file cmake_out/ai-m64l-32s-kit_nsh/.config \
  --enable BL_OS_FEATURE_TESTS_STACK_CANARY
cmake --build cmake_out/ai-m64l-32s-kit_nsh -t savedefconfig
python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

负测完成后必须恢复正式配置：

```sh
prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak \
  --file cmake_out/ai-m64l-32s-kit_nsh/.config \
  --disable BL_OS_FEATURE_TESTS_STACK_CANARY
cmake --build cmake_out/ai-m64l-32s-kit_nsh -t savedefconfig
python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

完成判据：最终 defconfig 只保留 `CONFIG_STACK_CANARIES=y`，不保留测试 app
选项；最终 clean build 再次通过。

## 反汇编门禁

`corrupt` 路径故意执行一字节越界，C 语言层面属于未定义行为，因此每次更换编译器
或优化参数后，必须先核对最终 ELF 的布局，不能只按源码推断。

本次 GCC 13.4.0、`-Os`、frame pointer 和 stack protector 组合下，最终
`run_canary_test` 反汇编为：

```text
addi  sp,sp,-64
addi  s0,sp,64
lw    a5,92(s2)       # __stack_chk_guard
sw    a5,-20(s0)      # frame canary
addi  a0,s0,-52       # buffer[0]
...
sb    a5,-36(s1)      # s0 + offset - 52
...
lw    a4,-20(s0)
lw    a5,92(s2)       # __stack_chk_guard
xor   a5,a5,a4
beqz  a5,return
jal   __stack_chk_fail
```

布局和写入位置：

| 对象或模式 | 地址范围或写入位置 | 结论 |
|---|---|---|
| `buffer[0..31]` | `s0-52` 至 `s0-21` | 32 B 局部数组 |
| frame canary | `s0-20` | 紧邻数组尾部 |
| `safe` offset 31 | `s0-21` | 合法写最后一个数组字节 |
| `corrupt` offset 32 | `s0-20` | 只改 canary 首字节 |

只有 offset 32 精确命中 canary，且 epilogue 仍包含 guard 比较和 fail 调用时，才
允许在实板执行 `corrupt`。

## 固件运行测试

### 1. 建立单一串口会话

```sh
picocom --baud 2000000 --lower-rts /dev/ttyUSB2
```

串口打开会使 Ai-M64L-32S-Kit 重启一次。保持同一会话直到负测和负测后的基本
检查完成，看到 `NuttShell (NSH)` 和 `nsh>` 后继续。

### 2. 验证正常路径

```text
stack_canary_test safe
```

实测关键输出：

```text
STACK_CANARY_TEST SAFE begin
STACK_CANARY_TEST write offset=31 size=32
STACK_CANARY_TEST SAFE returned
```

完成判据：命令返回 NSH，不出现 assert 或 panic。

### 3. 执行受控 canary 破坏

```text
stack_canary_test corrupt
```

实测关键输出：

```text
STACK_CANARY_TEST CORRUPT begin
STACK_CANARY_TEST write offset=32 size=32
Assertion failed panic: at file: /libs/libc/assert/lib_stackchk.c:57 task: stack_canary_test
sched_dumpstack: [ 5] [<0x8003acd6>] run_canary_test+0x70/0x84
sched_dumpstack: [ 5] [<0x8003ad7c>] stack_canary_test_main+0x92/0xca
```

完成判据：

- 出现 `lib_stackchk.c:57` 的 `panic`；
- backtrace 含 `run_canary_test` 和 `stack_canary_test_main`；
- 不出现 `STACK_CANARY_TEST CORRUPT returned`；
- 故障仅来自 offset 32 的受控一字节覆盖。

### 4. 验证普通任务终止后的系统状态

```text
echo alive
```

实测输出：

```text
alive
```

当前为 flat builtin 普通应用任务，`BOARD_RESET_ON_ASSERT=0`。canary fail 输出现场
后由 `abort()` 终止当前测试任务，NSH 继续运行。该结论不能外推到内核线程或 IRQ：
内核线程/IRQ 中的 canary fail 会进入系统 panic；当前 reset 策略下可能停在循环中，
而不是回到 NSH。

### 5. 受控复位

负测完成后使用仓库 `bl-module-reset` 工具执行固定 DTR/RTS 时序，参数为：

```text
--port /dev/ttyUSB2
--baudrate 2000000
--expect "NuttShell (NSH)"
--expect "nsh>"
```

实测 `status=ok`，两个启动标志均匹配。完成判据：冷启动无 early assert/panic，
重新进入 NSH。

## 冷启动外设回归

在受控复位后的同一固件依次执行：

```text
mcu_gpio_test -c edge --out /dev/gpio12 -n 3 -v
mcu_timer_test -c 001 -t 10000 -n 5 -e 5 -v
mcu_timer_test -c 002 -t 500000 -a 39 -b 79 -v
mcu_timer_test -c 005
oneshot -d 100000 /dev/oneshot
mcu_wdt_test -c 002 -t 1000 -p 3000 -i 500 -v
mcu_wdt_test -c 003 -t 1000
echo final_alive
```

实测结果：

| 用例 | 关键数据 | 结果 |
|---|---|---|
| GPIO edge | 非法操作被拒绝，3 个恢复周期 | PASS |
| TIMER-001 | 10 ms、5 轮，最大误差 284 us（2.840%），门限 500 us | PASS |
| TIMER-002 | divider 39/79 周期 0.4997/1.0000 s，比例 2.001 | PASS |
| TIMER-005 | 非法请求、运行中更新和重复生命周期均保持状态 | PASS |
| oneshot | 100,000 us，输出 `Finished` 并返回 NSH | PASS |
| WDT-002 | 1,000 ms timeout，每 500 ms 喂狗，3,006 ms 内 6 次 | PASS |
| WDT-003 | 非法/live 修改和重复生命周期被拒绝 | PASS |
| 最终存活 | 输出 `final_alive` | PASS |

## 固件开销

关闭/开启 clean build 的受控对照均不包含测试 app：

| 制品 | canary 关闭 | canary 开启 | 增量 |
|---|---:|---:|---:|
| `final_nuttx` | 692,816 B | 738,048 B | +45,232 B |
| text | 302,174 B | 349,550 B | +47,376 B |
| data | 14,292 B | 14,292 B | 0 B |
| bss | 11,128 B | 11,128 B | 0 B |
| `nuttx.bin` | 322,576 B | 369,568 B | +46,992 B |

`-fstack-protector-all` 会增加所有 C 函数的入口、出口指令和部分栈帧，不仅影响有
局部数组的函数。本次固件 text 增长约 46 KiB，因此该选项保持独立 Kconfig 裁剪
边界；对代码容量或中断延迟敏感的产品需要基于自身固件重新评估。

最终产品 `nuttx.bin` SHA256 为
`3d458b8455e3deb579c8d8deea6ba7c3b0b00a5c995deef40a5970c1ffdfc61c`。
USB2 分区烧录时 boot2、双 partition 和 app 四段主机/设备校验均一致；启动后
`help` 不含测试命令，以上冷启动外设回归和 `final_alive` 均在该固件上再次通过。

## 适用边界

- 当前 guard 固定可预测，定位为非定向内存破坏检测，不宣称抗攻击随机化。
- 检测发生在受保护函数返回前；覆盖后函数不返回、guard 也被同步伪造或破坏未经过
  canary 时，不能保证报告。
- `-fstack-protector-all` 保护早期启动 C 函数；修改 linker 或 guard 存储位置后
  必须重新验证 pre-section-load 可读性。
- 普通 builtin 任务和内核线程/IRQ 的 assert 后果不同，负测只允许在独立普通应用
  任务中执行。
- 测试 app 包含故意越界代码，默认关闭且不得加入启动脚本。每次工具链或优化参数
  变化后，必须重新通过最终 ELF 反汇编门禁。
