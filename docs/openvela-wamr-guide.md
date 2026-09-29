# openvela WebAssembly（WAMR）使用说明与教程

> 适用对象：第一次接触 openvela Wasm 的新人，以及需要做 Wasm 集成、原生扩展与调试的开发者。
> 适用仓库：`bl_vela_sdk`（openvela 5.5 基线的开发期 SDK）。
> 实测基线：NuttX 13.1.0-RC0 模拟器（`sim:wamr`）、WAMR 1.1.2（`sim:wamr` 钉的提交 `9f0c4b63`）、预置工具链 `prebuilts/clang-wasm/linux-x86_64`（clang 16 + wamrc + wasm-opt）。
> 阅读路径：新人按第 2 章跑通；开发者读第 3、4 章；排查问题看第 5 章；BL616CL 见第 6 章。

**验证标记**：本文中标 ✅ 的命令/输出是在上述基线实机（模拟器）跑过的；标 📖 的内容来自仓库源码、NuttX 文档或上游 openvela 仓库，未在本仓运行验证。

---

## 目录

1. [概述](#1-概述)
2. [快速上手：在模拟器上跑通第一个 wasm 程序](#2-快速上手在模拟器上跑通第一个-wasm-程序)
3. [使用说明](#3-使用说明)
4. [参考](#4-参考)
5. [限制、已知问题与排错](#5-限制已知问题与排错)
6. [BL616CL：实验性评估](#6-bl616cl实验性评估)

---

## 1. 概述

### 1.1 技术栈的三层结构

openvela 的 WebAssembly 支持不是单个组件，而是三层：

| 层 | 位置 | 作用 |
|---|---|---|
| 运行时 | `apps/interpreters/wamr` | WAMR VMcore + `iwasm` 命令行；负责加载/执行 `.wasm`、`.aot`、`.xip` 模块 |
| 模块构建框架 | `apps/tools/Wasm/`、`apps/cmake/nuttx_wasm_interface.cmake`、`apps/tools/Wasm.mk` | 用预置 WASI clang 把 `apps/` 里的应用编成 `wasm32-wasi` 模块，可选 `wasm-opt` 优化、`wamrc` 生成 AOT/XIP |
| 原生模块注册 | `apps/interpreters/wamr/registry/`、`wamr_custom_init.c` | 让平台 C 代码以 host function（native symbol）形式给 wasm 应用提供能力 |

配套工具链在 `prebuilts/clang-wasm/<host>-<arch>/`（本机为 `linux-x86_64`），包含 clang-16、wasm-ld、llvm-ar、`wamrc`（AOT 编译器）和 `wasm-opt`（二进制优化）。`source build/envsetup.sh` 会导出：

```bash
export WASI_SDK_PATH=$T/prebuilts/clang-wasm/${SYSTEM}-${SYS_ARCH}
export WASM_TOOLCHAIN_PATH=$T/prebuilts/clang-wasm/${SYSTEM}-${SYS_ARCH}   # wamrc 所在目录
```

**WAMR 源码不在仓库里**：构建时自动下载到 `apps/interpreters/wamr/wamr`（该目录在 `.gitignore` 中）。版本由 `CONFIG_INTERPRETERS_WAMR_VERSION` 决定，Kconfig 默认 `"WAMR-2.1.0"`；`sim:wamr` 配置钉的是提交 `9f0c4b63ac21c975aee7aae30236e306c8e49d13`（对应 WAMR 1.1.2）。

### 1.2 三个可选的 wasm 运行时

`apps/interpreters/` 下同时存在三个运行时，只有 WAMR 是 openvela 主推并配套了模块构建框架与原生模块注册机制：

| 运行时 | Kconfig | 特点 | 本仓配套 |
|---|---|---|---|
| **WAMR** | `INTERPRETERS_WAMR` | 功能最全：经典/快速解释器、AOT、WASI、线程、调试器、内存/性能 profiling | CMake + Make 构建、wasm 模块构建框架、原生模块注册、`sim:wamr` 配置 |
| wasm3 | `INTERPRETERS_WASM3` | 极小解释器（`CODEPAGE`、`FIXEDHEAP` 可调），无 AOT | 仅解释执行，无模块构建框架 |
| toywasm | `INTERPRETERS_TOYWASM` | 小而完整的解释器，Kconfig 注释说明 wasm/wasi threads 依赖 pthread（当前未暴露为选项） | `sim/toywasm`、`esp32s3-devkit/toywasm` 配置，无模块构建框架 |

👉 除非有极端体积要求，选 WAMR。本文后续全部以 WAMR 为例。

### 1.3 执行模式：INT / AOT / XIP 📖

| 模式 | 产物 | 生成方式 | 适用场景 |
|---|---|---|---|
| INT（解释） | `.wasm` | 无需额外步骤 | 开发调试、内存紧张、模块需动态加载；分「经典解释器」和「快速解释器」二选一 |
| AOT（预编译） | `.aot` | `wamrc -o x.aot x.wasm`（构建框架中 `WAMR_MODE=AOT`） | 性能敏感、模块固定；需要能把代码放到可执行内存（NuttX 上依赖 text heap 支持） |
| XIP（就地执行） | `.xip` | `wamrc --enable-indirect-mode --disable-llvm-intrinsics -o x.xip x.wasm`（`WAMR_MODE=XIP`） | 内存受限，不希望把编译产物再拷进 RAM |

解释器二选一由 Kconfig 决定：`INTERPRETERS_WAMR_FAST` / `INTERPRETERS_WAMR_CLASSIC` / `INTERPRETERS_NONE`（三选一，默认 `NONE`——**必须显式选一个**，否则只有加载能力没有执行能力）。

### 1.4 openvela 相对上游 WAMR 的增量

- **预置工具链与模块构建框架**：上游 WAMR 需要自备 wasi-sdk；openvela 把 clang、wamrc、wasm-opt 放进 `prebuilts/`，并提供把 `apps/` 源码同源编成 wasm 的 CMake/Make 流程（§3.2）。
- **原生模块注册表（registry）**：`wamr_custom_init()` 替换 WAMR 的 `wasm_runtime_full_init()`，在运行时初始化后自动调用各模块的 `wamr_module_<name>_register()`（§3.5）。
- **v5.5 发行说明中的 openvela 特性**：netlib / WiFi / xtables 的 WASM 封装；WASM 多实例初始化（安全/ELF TA 场景）。封装源码在 openvela 上游仓 `open-vela/frameworks_runtimes_wasm`，**本 SDK 的 manifest 没有拉取该仓**。
- **上游 Vela Wasm Framework**（`open-vela/frameworks_runtimes_wasm`，dev 分支）：除 README 列出的 `chre`、`libc`、`memory`、`test`、`vela-sysroot`、`vendor` 外，仓库还有 `wapi`、`xtables`、`irq`、`dfx`、`feature_framework`、`dbus` 等目录。其中 `irq/` 提供 IRQ→wasm 的桥（§3.6），`vendor/bouffalo/` 是 Bouffalo 的 wasm SDK 2.0（含 `wasi-sdk.cmake` 和 BLFB native 库的导出符号表）。需要这些能力时，单独 clone 该仓或把它加入 manifest。

### 1.5 术语

| 术语 | 含义 |
|---|---|
| Module / Instance | `.wasm` 文件是模块；加载到内存后的可执行实体是实例（instance），带独立线性内存 |
| ExecEnv | 执行环境（执行栈、aux 栈、异常、线程状态）；每个线程一个 |
| Native symbol / host function | 由原生（C）代码注册、wasm 可以直接 import 调用的函数 |
| WASI | WebAssembly System Interface；openvela 用 `INTERPRETERS_WAMR_LIBC_WASI` 提供（实现不完整，见 §5.1） |
| AOT / XIP / INT | 见 §1.3 |
| registry | openvela 的原生模块注册机制（`.pdat`/`.bdat` + 生成头文件）|

---

## 2. 快速上手：在模拟器上跑通第一个 wasm 程序

主平台选 NuttX 模拟器（`sim:wamr`）：不需要硬件、构建快、便于反复实验。BL616CL 见第 6 章。

### 2.1 前置条件

- 主机 Linux x86_64，`make`、`curl`、`unzip`、Python3 可用；
- 本仓已经 `repo sync`，`prebuilts/clang-wasm/linux-x86_64` 存在；
- **能访问 GitHub**：首次构建会下载 WAMR 源码（约几 MB）。离线环境需要预先把 WAMR 放到 `apps/interpreters/wamr/wamr`（§4.5）。

### 2.2 步骤 1：准备环境变量

```bash
cd <repo-root>
source build/envsetup.sh        # 导出 WASI_SDK_PATH / WASM_TOOLCHAIN_PATH / PYTHONPATH
```

`envsetup.sh` 同时把 openvela 预置的 Python 包（`pyelftools`、`cxxfilt`、`Mako` 等）加入 `PYTHONPATH`——NuttX 构建本身的 `mkallsyms.py` 依赖它们，不设置会在**链接阶段**报出难以定位的 `错误 22`（见 §5.2）。

如果不想 source 整个 envsetup，可等价地手动导出：

```bash
export WASI_SDK_PATH=$PWD/prebuilts/clang-wasm/linux-x86_64
export WASM_TOOLCHAIN_PATH=$PWD/prebuilts/clang-wasm/linux-x86_64
export PYTHONPATH=$PWD/prebuilts/tools/python/dist-packages/pyelftools:\
$PWD/prebuilts/tools/python/dist-packages/cxxfilt:\
$PWD/prebuilts/tools/python/dist-packages/Mako:\
$PWD/prebuilts/tools/python/dist-packages/ply:\
$PWD/prebuilts/tools/python/dist-packages/jsonpath:\
$PWD/prebuilts/tools/python/dist-packages/kconfiglib:\
$PWD/prebuilts/tools/python/dist-packages/construct:$PYTHONPATH
```

### 2.3 步骤 2：配置并构建 sim:wamr ✅

```bash
cd nuttx
tools/configure.sh sim:wamr
make -j$(($(getconf _NPROCESSORS_ONLN)/2))
```

首次构建会先把 WAMR 源码下载解压到 `apps/interpreters/wamr/wamr/`，再编译内核与应用。预期结果：

```console
$ ls -l nuttx
-rwxrwxr-x 1 miot miot 6753936 Sep 29 11:00 nuttx
```

⚠️ **本仓当前有两个已知构建问题**，都会挡住这次构建，修复方法见 §5.2：

1. WAMR 1.1.2 的 NuttX 平台头缺少 `#include <pthread.h>`，启用 `THREAD_MGR`/`LIB_WASI` 时编译报 `incomplete type`；
2. 链接阶段 `mkallsyms.py` 因 `PYTHONPATH` 未设置静默退出 22。

> `sim:wamr` 的 defconfig 还开了 `CONFIG_ALLSYMS=y`、`CONFIG_BOARDCTL_APP_SYMTAB=y`，用于 dumpstack/符号表；它们不是 Wasm 的必需项，但本教程保留默认配置。

### 2.4 步骤 3：编译一个 wasm 模块 ✅

最小示例用 WAMR 自带的 hello-world 样例；后续 §3.2 会讲 openvela 框架内的编译方式。

```bash
cd apps/interpreters/wamr/wamr/product-mini/app-samples/hello-world

$WASI_SDK_PATH/bin/clang -O3 \
    -z stack-size=4096 -Wl,--initial-memory=65536 \
    -o test.wasm main.c \
    -Wl,--export=main -Wl,--export=__main_argc_argv \
    -Wl,--export=__data_end -Wl,--export=__heap_base \
    -Wl,--strip-all,--no-entry -Wl,--allow-undefined \
    -nostdlib
```

预期结果（实测 413 字节）：

```console
$ ls -l test.wasm
-rwxrwxr-x 1 miot miot 413 Sep 29 11:00 test.wasm
```

关键参数含义：

- `-z stack-size=4096`：wasm 侧栈 4 KiB；`--initial-memory=65536`：线性内存初始 1 页（64 KiB，必须是 64 KiB 的倍数）；
- 导出 `main`/`__main_argc_argv` 供 `iwasm` 调用，导出 `__data_end`/`__heap_base` 供运行时定位堆；
- `--allow-undefined`：未实现的符号（如 `printf`）留给运行时解析——`iwasm` 通过 libc builtin/WASI 或注册的原生模块提供；
- `-nostdlib`：不链接 wasi-libc 的完整实现（openvela 框架编译时会用 `--sysroot=$TOPDIR` 指向 NuttX 树，见 §4.3）。

### 2.5 步骤 4：在 NSH 里运行 ✅

模拟器的 `rcS` 会执行 `mount -t hostfs -o fs=. /data`，把 **启动 `./nuttx` 时的工作目录**映射为 `/data`，所以把 wasm 文件放到 `nuttx/` 下即可：

```bash
cp apps/interpreters/wamr/wamr/product-mini/app-samples/hello-world/test.wasm nuttx/

cd nuttx
( sleep 3; printf 'iwasm /data/test.wasm\n'; sleep 5; printf 'poweroff\n' ) | timeout 25 ./nuttx
```

实测输出：

```console
NuttShell (NSH) NuttX-13.1.0-RC0
nsh> iwasm /data/test.wasm
Hello world!
buf ptr: 0x1460
buf: 1234
nsh> poweroff
```

### 2.6 步骤 5：验证与退出

- 看到 `Hello world!`、`buf ptr: 0x1460`、`buf: 1234` 即端到端成功：模块加载、WASI/builtin libc 解析、wasm→宿主打印都通。
- 退出模拟器用 `poweroff`（defconfig 开了 `CONFIG_BOARDCTL_POWEROFF`），实测返回码 0。**不要用 `exit`**：它只退出 NSH 会话，模拟器进程不会结束，脚本会一直挂住。

### 2.7 教程阶段的常见错误

| 症状 | 原因 | 处置 |
|---|---|---|
| 编译 WAMR 报 `field 'wait_lock' has incomplete type` / `unknown type name 'pthread_rwlock_t'` | WAMR 1.1.2 的 `platform_internal.h` 没包含 `<pthread.h>` | §5.2 问题 1 |
| 链接阶段 `make` 报 `错误 22`，日志停在 `mkallsyms.py` 且无报错文本 | `PYTHONPATH` 缺 openvela 预置 Python 包 | §5.2 问题 2 |
| `nsh> iwasm: command not found` | `CONFIG_INTERPRETERS_IWASM_TASK` 被关掉 | menuconfig 打开（默认 y） |
| `iwasm /data/x.wasm` 报打不开文件 | 文件不在启动目录，或路径写错 | 把文件放到 `nuttx/` 下；`/data` 只映射启动时 cwd |
| 运行 AOT 文件报版本/加载错误 | `.aot` 与当前 WAMR 版本或目标架构不匹配 | 用同一套工具链重新 `wamrc` 生成 |

---

## 3. 使用说明

### 3.1 在板级配置里启用与裁剪 Wasm

menuconfig 路径：`Application Configuration → Interpreters → Webassembly Micro Runtime`（`apps/interpreters/wamr/Kconfig`）。最常用的开关：

| 类别 | 选项 | 说明 |
|---|---|---|
| 总开关 | `INTERPRETERS_WAMR` | tristate，默认 n |
| 命令行应用 | `INTERPRETERS_IWASM_TASK` | 默认 y；生成 `iwasm`；`INTERPRETERS_WAMR_PRIORITY`（默认 100）、`INTERPRETERS_WAMR_STACKSIZE`（默认 8192） |
| 版本 | `INTERPRETERS_WAMR_VERSION` | 默认 `WAMR-2.1.0`；可填 tag/分支/commit（用于拼接下载 URL） |
| 解释器 | `INTERPRETERS_WAMR_FAST` / `_CLASSIC` / `INTERPRETERS_NONE` | 三选一，默认 `NONE`；想执行就必选其一 |
| libc | `INTERPRETERS_WAMR_LIBC_BUILTIN` | 内建 libc（`printf` 等）；默认 n |
| libc | `INTERPRETERS_WAMR_LIBC_WASI` | WASI 实现，select `FS_LINKS`；文件系统操作不完整（§5.1） |
| 模块构建 | `INTERPRETERS_WAMR_BUILD_MODULES_FOR_NUTTX` | 实验性，依赖 `LIBC_BUILTIN`；**Make 路径编译 wasm 模块的总开关** |
| AOT | `INTERPRETERS_WAMR_AOT` | 默认 n；`if ARCH_HAVE_TEXT_HEAP` 时 select `ARCH_USE_TEXT_HEAP` |
| AOT 细节 | `_AOT_QUICK_ENTRY`（+~8KB）、`_AOT_STACK_FRAME`、`_AOT_WORD_ALIGN_READ`、`_MEM_DUAL_BUS_MIRROR`、`_DEBUG_AOT` | 按平台能力选择 |
| 线程 | `_THREAD_MGR`、`_LIB_PTHREAD`、`_LIB_PTHREAD_SEMAPHORE`、`_LIB_WASI_THREADS` | 中断/终止、pthread 支持的前提（§3.6） |
| 内存 | `_GLOBAL_HEAP_POOL(_SIZE)`（默认 128 KB）、`_STACK_GUARD_SIZE`（默认 1024） | 栈保护占用宿主任务栈 |
| 调试 | `_LOG`、`_DEBUG_INTERP`（依赖 CLASSIC+THREAD_MGR+NET_TCP+NET_SOLINGER）、`_DUMP_CALL_STACK`、`_LOAD_CUSTOM_SECTIONS`、`_CUSTOM_NAME_SECTIONS` | 排错时开，量产关 |
| 性能 | `_PERF_PROFILING`、`_MEMORY_PROFILING`、`_MEMORY_TRACING` | 有额外开销 |
| 规范特性 | `_MULTI_MODULE`、`_REF_TYPES`、`_GC`、`_TAIL_CALL`、`_SHARED_MEMORY`、`_BULK_MEMORY`、`_MEMORY64`、`_ENABLE_EXCE_HANDLING`、`_MINILOADER`、`_ENABLE_SPEC_TEST` | 按模块需求开启 |
| 其他 | `_CONFIGURABLE_BOUNDS_CHECKS`、`_DISABLE_HW_BOUND_CHECK` | 后者在 NuttX 上是 no-op（Kconfig help 已注明硬件边界检查不可用） |
| 示例 | `EXAMPLES_HELLO_WASM`（`_BUILD_WASM` 默认 y、`_BUILD_NATIVE` 默认 n）、`EXAMPLES_WAMR_MODULE` | 教程与原生模块示例 |
| 工具 | `TOOLS_WASM_BUILD`（`Extra Tools → Wasm Build Options`） | **CMake 路径触发 wasm 子构建的总开关** |

裁剪建议：先把 `LOG`/`PERF_PROFILING`/`MEMORY_*` 关掉；不需要 AOT 就关 `_AOT`（连带 `wamrc` 步骤都不需要）；线程管理与调试器只在需要中断/调试时开；`LIBC_WASI` 只在模块真的要用 POSIX 文件/环境时开。

### 3.2 用 openvela 框架把 `apps/` 源码编成 wasm 模块

openvela 的价值在于**同一份源码、同一套配置**既可编成原生 app，也可编成 wasm 模块。现有两条路径：

#### CMake 路径（openvela 主线，本 SDK 的 `vela` 构建即 CMake）

应用在 `CMakeLists.txt` 里声明：

```cmake
wasm_add_application(
  NAME hello_wasm            # 产物 <NAME>.wasm
  SRCS hello_main.c
  STACK_SIZE 4096            # 可选，默认 2048
  INITIAL_MEMORY_SIZE 65536  # 可选，默认 65536（1 页，须为 64 KiB 倍数）
  WAMR_MODE AOT             # 可选，INT（默认）/AOT/XIP
  INSTALL_NAME hello_wasm.aot  # 可选，给出则把产物打进 ROMFS
  WCFLAGS ... WLDFLAGS ... WRCFLAGS ... WINCLUDES ...  # 可选
)
```

内部机制（便于排错时定位）：

1. 原生侧 `apps/cmake/nuttx_wasm_interface.cmake` 的 `wasm_add_application()` 不编译任何东西，只把当前目录登记到 `wasm_interface` 目标的 `WASM_DIR` 属性；给了 `INSTALL_NAME` 时再用 `add_dynamic_rcraws()` 把 `<build>/wasm/<INSTALL_NAME>` 挂进 ROMFS。
2. `apps/tools/CMakeLists.txt` 在 `CONFIG_TOOLS_WASM_BUILD=y` **或** `CONFIG_INTERPRETERS_WAMR_BUILD_MODULES_FOR_NUTTX=y` 时，把收集到的目录列表交给 `apps/tools/Wasm/CMakeLists.txt`，用 `WASI-SDK.cmake` 定义的工具链做一次外部子构建。
3. 子构建产物写到 `<build>/wasm/`：先 `wasm-opt -Oz --enable-bulk-memory` 优化出 `<name>.wasm`；`WAMR_MODE=AOT` 且 `INTERPRETERS_WAMR_AOT=y` 时用 `wamrc` 生成 `<name>.aot`；`XIP` 则生成 `<name>.xip`。静态库用 `wasm_add_library(NAME ... SRCS ...)` 声明，链接进 `wasm_interface`。

参考实现：`apps/examples/hello_wasm/CMakeLists.txt`（同一份 `hello_main.c`，`EXAMPLES_HELLO_WASM_BUILD_NATIVE` 与 `_BUILD_WASM` 决定编原生还是 wasm）。

#### Make 路径（legacy，本仓仍支持）

应用 Makefile 声明：

```makefile
WASM_BUILD = y        # y：只编 wasm；both：原生 + wasm；n：不编
WAMR_MODE  = AOT      # INT（默认）/AOT/XIP
WASM_INITIAL_MEMORY ?= 65536
include $(APPDIR)/Application.mk
```

`apps/Application.mk` + `apps/tools/Wasm.mk` 用 `WCC`（wasi clang）把 `MAINSRC`/`CSRCS` 编成 `*.wo`，顶层 `apps/Makefile` 的 `LINK_WASM` 负责链接、`wasm-opt` 优化和 `wamrc` 转换；产物在 `apps/bin/wasm/<PROGNAME>.{wasm,aot,xip}`（`BINDIR = $(APPDIR)/bin`）。这条路径的**总开关是 `CONFIG_INTERPRETERS_WAMR_BUILD_MODULES_FOR_NUTTX=y`**，且需要 `WASI_SDK_PATH` 指向预置工具链。

#### 框架实测 ✅

在 `sim:wamr` 上开启 `CONFIG_EXAMPLES_HELLO_WASM=y` + `CONFIG_INTERPRETERS_WAMR_BUILD_MODULES_FOR_NUTTX=y`（`make olddefconfig` 后 `make`），Make 路径确实产出了模块：

- 中间产物：`apps/wasm/hello_wasm%65536%2048%%AOT%<obj>.wo`、`apps/wasm/hello_wasm.ldflags`、`apps/wasm/libwasm.a`（文件名里的 `65536%2048%%AOT` 即 `INITIAL_MEMORY%STACKSIZE%PRIORITY%WAMR_MODE`）；
- 最终产物：`apps/bin/wasm/hello_wasm.wasm`（212 B）、`coremark.wasm`（13 KB）。

但这条路径在本仓有三个实测出来的注意点：

1. **首次 `make` 可能只出 `.wo` 不出 `.wasm`**：`LINK_WASM` 挂在顶层 `apps/Makefile` 的 `$(BIN)` 规则里，按 `$(wildcard apps/wasm/*.wo)` 展开；实测首次构建后 `apps/bin/wasm/` 为空。触发 `$(BIN)` 重建（`rm apps/libapps.a`，或删除各 app 的 `*.built` 后重跑 `make`）即可产出最终 `.wasm`。
2. **64 位 sim 上框架产出的是 wasm64 模块，WAMR 1.1.2 加载不了**：`CONFIG_ARCH_64BIT=y` 时 `WASI-SDK.defs` 会加 `--target=wasm64` / `--enable-memory64`，生成的 import 里指针是 i64（实测 `(import "env" "printf" (func (param i64 i64) (result i32)))`）；而 1.1.2 的 VMcore 没有 MEMORY64 支持（`INTERPRETERS_WAMR_MEMORY64` 在 1.1.2 上无对应编译开关），`iwasm` 报 `WASM module load failed: integer too large`。**32 位目标板（如 BL616CL）不受影响**；在 64 位 sim 上验证运行时请用 §2 的手工 wasm32 模块。
3. **sim 上 `WAMR_MODE=AOT` 会静默失败**：Make 路径的 `Toolchain.defs` 在 sim 上生成的 `wamrc` 参数是 `--target=x86_64 --cpu= --target-abi=`（`LLVM_CPUTYPE`/`LLVM_ABITYPE` 为空），wamrc 打印 usage 就退出，而错误被重定向丢弃，最后只留 `.wasm`。实测补上 `--cpu=x86-64 --target-abi=gnu` 可产出 `hello_wasm.aot`（728 B）；因此在 sim 上先把 `WAMR_MODE` 设为 `INT`，或显式提供 `WCPU`/`WABITYPE`。

CMake 路径与 Make 路径共用同一套工具链定义（`WASI-SDK.cmake`/`WASI-SDK.defs`），第 2、3 两点同样适用；CMake 子构建在 sim 上的差异本文未单独实测 📖。

### 3.3 独立构建 wasm 模块

不依赖 NuttX 构建系统时，直接用预置 clang：

```bash
export WASI_SDK_PATH=$PWD/prebuilts/clang-wasm/linux-x86_64

$WASI_SDK_PATH/bin/clang -O3 --target=wasm32 -nostdlib \
    -z stack-size=4096 -Wl,--initial-memory=65536 \
    -Wl,--export=main -Wl,--export=__main_argc_argv \
    -Wl,--export=__data_end -Wl,--export=__heap_base \
    -Wl,--strip-all,--no-entry -Wl,--allow-undefined \
    -o app.wasm app.c
```

要点：

- 目标 ABI 是 `wasm32`（`CONFIG_ARCH_64BIT` 的构建会切换 `wasm64`，见 `WASI-SDK.defs`）；
- `--allow-undefined` 是嵌入式 wasm 的常态：未实现符号留给 `iwasm` 的 libc builtin/WASI 或原生模块；但**解析不到的符号会在运行时加载时失败**，错误能在 `iwasm` 日志里看到；
- 想复用 NuttX 头文件/声明时，加 `--sysroot=$TOPDIR`（openvela 框架就是这么做的）；只想编纯 POSIX 代码则用 wasi-sdk 自带的 sysroot；
- 优化：`wasm-opt -Oz --enable-bulk-memory -o app.wasm app.wasm`；
- AOT：`wamrc --target=<arch> --cpu=<cpu> --target-abi=<abi> -o app.aot app.wasm`；XIP 额外加 `--enable-indirect-mode --disable-llvm-intrinsics`；
- 约束：模块里**不能用 NuttX 专有 API**，只能用 POSIX 兼容子集或你自己注册的 host function。

### 3.4 用 iwasm 运行与调试

`iwasm` 的宿主任务由 `INTERPRETERS_IWASM_TASK` 控制，用法与上游一致。实测帮助（WAMR 1.1.2，sim）：

```console
Usage: iwasm [-options] wasm_file [args...]
options:
  -f|--function name       指定入口函数（默认 main）
  -v=n                     日志等级 0~5（默认 2，越大越详细）
  --interp                 强制解释执行（即使模块是 AOT）
  --stack-size=n           最大执行栈，默认 64 KB
  --heap-size=n            最大堆，默认 16 KB
  --repl                   进入简易 REPL："FUNC ARG..."
  --env=<k=v>              WASI 环境变量，可重复
  --dir=<dir>              授予 WASI 访问的宿主目录
  --addr-pool=<cidr,...>   WASI 允许的网络地址
  --allow-resolve=<domain> WASI 允许解析的域名（支持通配）
  --native-lib=<lib>       注册 .so 原生库（嵌入式一般不用）
  --max-threads=n          每个 cluster 最大线程数，默认 4
  --version                显示版本
```

常见用法：

```bash
nsh> iwasm /data/app.wasm arg1 arg2      # 解释执行 / AOT / XIP 都是这个命令，按文件内容自动识别
nsh> iwasm -f func /data/app.aot         # 指定入口函数
nsh> iwasm -v=5 /data/app.wasm           # 打开详细日志定位加载/符号解析问题
nsh> iwasm --stack-size=131072 --heap-size=65536 /data/app.wasm
```

调试建议：加载失败、符号缺失、内存越界优先用 `-v=5`；需要断点/远程调试则开 `INTERPRETERS_WAMR_DEBUG_INTERP`（要求经典解释器 + 线程管理 + TCP）和 `_DUMP_CALL_STACK`；性能定位用 `_PERF_PROFILING`/`_MEMORY_PROFILING`（会增大体积、降低速度）。

### 3.5 注册原生模块（host function）

机制：wasm 只能调用显式注册给它的原生符号。openvela 用「注册表」把这件事标准化——模块导出固定命名的注册函数，运行时初始化时统一调用：

```
wasm_runtime_full_init(...)        # WAMR 原始初始化
wamr_custom_init(...)              # openvela 包装后的初始化
  ├── wasm_runtime_full_init(...)
  └── for each module: wamr_module_<name>_register()   # 来自 registry 生成的列表
```

参考实现 `apps/examples/wamr_module/module_hello.c`：

```c
static NativeSymbol g_hello_symbols[] =
{
  EXPORT_WASM_API_WITH_SIG2(hello, "()")   /* wasm 侧 import "hello" 的 "hello" */
};

static void hello_wrapper(wasm_exec_env_t env)
{
  printf("Hello World from WAMR module!\n");
}

bool wamr_module_hello_register(void)       /* 命名固定：wamr_module_<name>_register */
{
  return wasm_runtime_register_natives("hello", g_hello_symbols,
                                       nitems(g_hello_symbols));
}
```

**Make 路径**：应用 Makefile 写

```makefile
CSRCS = module_hello.c
WAMR_MODULE_NAME = hello          # 全局唯一
include $(APPDIR)/interpreters/wamr/Module.mk
include $(APPDIR)/Application.mk
```

`Module.mk` 会生成 `apps/interpreters/wamr/registry/hello.{pdat,bdat}` 和拼接出 `wamr_external_module_{proto,list}.h`，供 `wamr_custom_init.c` 编译期包含。

**CMake 路径**：应用 `CMakeLists.txt` 写

```cmake
target_sources(apps PRIVATE module_hello.c)
nuttx_add_wamrmod(MODS hello)      # cmake/nuttx_add_wamrmod.cmake
```

生成物在 `<build>/wamrmod/`。注意：CMake 实现用 `>>` 向生成头文件追加内容，是按 target 的 custom command 触发的；**改动模块列表后建议 clean 对应 app 的构建目录或全量重编**，避免出现重复/漏项 📖。

命名与约束：模块名全局唯一（决定 `wamr_module_<name>_register`）；符号签名必须与 wasm 侧 import 声明一致（`EXPORT_WASM_API_WITH_SIG*` 家族宏）；注册在运行时初始化阶段完成，**不能**在运行到一半动态卸载；任一模块注册失败，`wamr_custom_init` 直接失败，整个 `iwasm` 起不来——排错时优先看 `-v=5` 日志和注册函数的返回值。

### 3.6 中断与异步终止

**结论**：WAMR 支持**协作式**中断/终止（让运行中的 wasm 停下来/挂起），没有指令级抢占式中断；CPU 硬件 IRQ 也不会直接进入 wasm。

**VM 执行中断（1.1.2 实测代码路径）**：

- 每个执行环境有 `suspend_flags.flags` 位标志（`core/iwasm/common/wasm_exec_env.h`）：`0x01`=terminate、`0x02`=suspend、`0x08`=线程退出标记。写入点：`core/iwasm/libraries/thread-mgr/thread_manager.c`（`set_thread_cancel_flags` 置 0x01；`wasm_cluster_suspend_thread` 置 0x02；`wasm_cluster_exit_thread` 置 0x08）。
- 检查点在**控制流边界**：经典/快速解释器的 `CHECK_SUSPEND_FLAGS()`（`wasm_interp_classic.c`、`wasm_interp_fast.c`）在循环回边、分支、函数调用处检查；AOT 由 `core/iwasm/compilation/aot_emit_control.c` 的 `check_suspend_flags()` 在编译进模块的同类位置插入。密集循环每圈都能响应，纯直线长指令流要等到下一个边界。
- 语义：`0x01` 置位后当前 wasm 函数立即返回（宿主负责回收/收尾）；`0x02` 置位后线程阻塞在 `wait_cond`，由 `wasm_cluster_resume_thread()` 清标志唤醒。
- **前提是 `INTERPRETERS_WAMR_THREAD_MGR=y`**（依赖 pthread）：不开线程管理器，这些检查根本不编译进去。
- API 差异：WAMR 1.1.2（本仓 `sim:wamr` 钉的版本）里 terminate/suspend 属于线程管理器内部接口（`wasm_cluster_cancel_thread`、`wasm_cluster_terminate_all`、`wasm_cluster_suspend_thread`、`wasm_cluster_resume_thread`），公开面只有 `wasm_runtime_spawn_thread`/`wasm_runtime_join_thread`；Kconfig 默认版本 `WAMR-2.1.0` 上游才把 terminate 暴露为公开 API `wasm_runtime_terminate()` 📖。开 `INTERPRETERS_WAMR_DEBUG_INTERP` 时，终止走真实 OS signal（`WAMR_SIG_TERM`）异步打断线程，供调试器使用。

**硬件 IRQ**：IRQ 由 NuttX 正常抢占处理，iwasm 只是普通任务，只要不长时间关中断就不会丢中断。wasm 要响应外设中断，必须由原生模块把 IRQ 事件转成 wasm 能调用的回调——上游 Vela Wasm Framework 提供了这样的模块：`irq/`（Kconfig `WASM_IRQ`，默认 n；`WASM_IRQ_THREAD_PRIORITY` 默认 100；`wasm_irq_manager.h` 定义 `wasm_irq_s`：IRQ 号、引用计数、回调 index/参数、专属 IRQ 线程）📖，其实现是「IRQ 线程收事件 → 调 wasm 回调」。**该仓不在本 SDK 的 manifest 里**，需要时单独引入。

**注意**：NuttX 移植上基于 SIGSEGV/altstack 的硬件边界检查不可用（Kconfig help 已注明），不要依赖它做内存安全兜底。

### 3.7 把模块放进固件、开机自启

- CMake 路径给 `wasm_add_application` 传 `INSTALL_NAME`，产物会以该名字打包进 board 的 ROMFS（`add_dynamic_rcraws`），在设备上表现为 `/etc/<INSTALL_NAME>`（openvela 官方文档里的示例是 `/etc/ofonod.aot`）📖。
- 开机自启在 board 的 `etc/init.d/rcS` 里加一行，例如 openvela 电话文档的写法：

  ```sh
  iwasm --disable-bounds-checks --max-threads=1 /etc/ofonod.aot &
  ```

  `--disable-bounds-checks` 需要 `INTERPRETERS_WAMR_CONFIGURABLE_BOUNDS_CHECKS=y`。
- 更常见的做法是把模块放到可写的 FAT/ramdisk 分区（如 `/data`、`/mnt`），跑 `iwasm /path/app.wasm`；这便于替换模块，但会失去 ROMFS 的只读保护。

### 3.8 性能与体积调优

- **执行模式优先**：固定模块优先 AOT；内存吃紧用 XIP；可动态替换的模块用 INT。
- **解释器选择**：`FAST` 比 `CLASSIC` 快、代码略大；只求最小体积选 `CLASSIC`。
- **裁剪**：关 `LOG`/`*_PROFILING`/`*_TRACING`；`AOT_QUICK_ENTRY` 会增加约 8 KB（Kconfig help），按需开。
- **内存**：`GLOBAL_HEAP_POOL(_SIZE)` 使用固定池（默认 128 KB）替代 malloc/free 组合，碎片敏感场景更稳；`STACK_GUARD_SIZE` 默认 1024，会算进宿主任务栈，栈紧张时可下调但要保留余量。
- **模块侧**：`wasm-opt -Oz`、`--strip-all`、按需缩小 `INITIAL_MEMORY_SIZE`；模块内部算法优化对 AOT/INT 都有收益。
- **参考数据**：上游 openvela WAMR 仓 README 给出了各运行模式的大致体积（如 fast interpreter 模式约 58.9 KB）📖，可作为裁剪前的基线预期。

---

## 4. 参考

### 4.1 Kconfig 选项速查

完整列表见 `apps/interpreters/wamr/Kconfig`；分类说明见 §3.1。补充几个容易混淆的点：

- `INTERPRETERS_WAMR_VERSION` 是字符串，直接拼进下载 URL：tag（`WAMR-2.1.0`）、分支（`main`）或 commit hash 都可以；
- `INTERPRETERS_WAMR_AOT` 在支持 text heap 的架构上自动 `select ARCH_USE_TEXT_HEAP`，ARM64 类配置默认 `AOT_WORD_ALIGN_READ=y`；
- 外设/平台相关：`WAMR_MEM_DUAL_BUS_MIRROR` 仅当 `ARCH_HAVE_TEXT_HEAP_SEPARATE_DATA_ADDRESS` 时默认 y；
- `INTERPRETERS_WAMR_UNIT_TEST` 依赖 `LIB_GOOGLETEST`，会额外生成 `wamr_unit_test` 应用。

### 4.2 `wasm_add_application` / `wasm_add_library` 参数

| 参数 | 必填 | 默认 | 说明 |
|---|---|---|---|
| `NAME` | 是 | — | 产物名 `<NAME>.wasm` |
| `SRCS` | 是 | — | 源文件列表 |
| `STACK_SIZE` | 否 | 2048 | 传给链接器的 `-z stack-size` |
| `INITIAL_MEMORY_SIZE` | 否 | 65536 | 线性内存初始值，必须是 65536 的倍数 |
| `WAMR_MODE` | 否 | `INT` | `INT`/`AOT`/`XIP`（AOT/XIP 需 `INTERPRETERS_WAMR_AOT=y`） |
| `INSTALL_NAME` | 否 | `<NAME>.wasm/.aot/.xip` | 给出则把产物以该名字打进 ROMFS |
| `WCFLAGS` / `WLDFLAGS` / `WRCFLAGS` / `WINCLUDES` | 否 | — | 编译/链接/wamrc/include 扩展 |

`wasm_add_library(NAME <n> SRCS <files> [WCFLAGS] [WINCLUDES])` 生成静态库并挂到 `wasm_interface`。

### 4.3 Make 变量速查（`Wasm.mk` / `WASI-SDK.defs`）

| 变量 | 默认 | 说明 |
|---|---|---|
| `WASM_BUILD` | `n`（未设置时由 `WCC` 是否存在决定） | `y`/`both`/`n` |
| `WAMR_MODE` | `INT` | `INT`/`AOT`/`XIP` |
| `WASM_INITIAL_MEMORY` | 65536 | 同 CMake |
| `STACKSIZE` | `CONFIG_DEFAULT_TASK_STACKSIZE` | 同 CMake |
| `WCC` / `WAR` | `$WASI_SDK_PATH/bin/clang` / `llvm-ar rcs` | 编译/打包 |
| `WSYSROOT` | `$(TOPDIR)` | NuttX 源码树作为头文件根 |
| `WOPTFLAGS` | 由 `CONFIG_DEBUG_FULLOPT`/`CONFIG_ARCH_64BIT` 等推导 | `wasm-opt` 参数 |
| `WASM_TOOLCHAIN_PATH` | — | `wamrc` 所在目录（envsetup 导出） |

### 4.4 目录、产物与环境变量

| 路径/变量 | 内容 |
|---|---|
| `apps/interpreters/wamr/` | 运行时集成；生成物 `wamr/`（源码）、`registry/*.{pdat,bdat}`、`wamr_external_module_{proto,list}.h`（gitignore） |
| `apps/tools/Wasm/` | CMake 构建入口、`WASI-SDK.cmake`；子构建目录 `<build>/Wasm` |
| `apps/tools/Wasm.mk`、`WASI-SDK.defs` | Make 路径的构建宏与工具链标志 |
| `apps/cmake/nuttx_wasm_interface.cmake`、`nuttx_add_wamrmod.cmake` | CMake 侧接口 |
| `<build>/wasm/*.wasm|.aot|.xip` | CMake 路径模块产物（CMake 的 build 目录通常是 `cmake_out/<board>_<config>/`） |
| `apps/bin/wasm/*` | Make 路径模块产物 |
| `<build>/wamrmod/` | CMake 路径原生模块注册生成物 |
| `prebuilts/clang-wasm/linux-x86_64/` | `bin/clang`、`bin/wasm-ld`、`bin/llvm-ar`、`wamrc`、`wasm-opt` |
| `WASI_SDK_PATH` / `WASM_TOOLCHAIN_PATH` | 同指预置工具链目录；`envsetup.sh` 导出 |
| `PYTHONPATH`（预置包） | NuttX 构建需要；`pyelftools`/`cxxfilt` 缺失会导致链接期 `错误 22` |

### 4.5 WAMR 源码的获取与离线构建

- Make 路径：`apps/interpreters/wamr/Makefile` 用 `curl` 下载 `https://github.com/bytecodealliance/wasm-micro-runtime/archive/<VERSION>.zip` 并解压为 `wamr/`；
- CMake 路径：`apps/interpreters/wamr/CMakeLists.txt` 用 `FetchContent` 下载同一个 URL 的 zip 到源码目录（`SOURCE_DIR` 就是 `apps/interpreters/wamr/wamr`）；
- **离线/内网**：
  - Make 路径用 `$(wildcard wamr/.git)` 判断是否需要下载：要么在 `apps/interpreters/wamr/` 下放好与 `INTERPRETERS_WAMR_VERSION` 同名的 `<VERSION>.zip`（make 见文件存在就跳过下载，再解压），要么把 WAMR 仓库 clone 到 `wamr/` 并切到目标版本（保留 `.git`）；
  - CMake 路径只判断目录是否存在，预置目录即可跳过 `FetchContent`；
- 该目录被 gitignore，`vela clean`/`make distclean` 会清理；重新拉取会丢失本地手改（包括 §5.2 的 pthread 修补），这也是为什么修补应尽量做成构建参数而不是改源码。

### 4.6 相关仓库与资料

| 资料 | 链接 |
|---|---|
| openvela WAMR 仓（本仓 `apps/interpreters/wamr` 的上游） | <https://github.com/open-vela/apps_interpreters_wamr> |
| openvela Vela Wasm Framework（libc/memory/vela-sysroot/irq/wapi/xtables/vendor） | <https://github.com/open-vela/frameworks_runtimes_wasm> |
| WAMR 官方文档 | <https://wamr.gitbook.io/document/> |
| NuttX wamr 文档（Make 时代，部分内容过时） | <https://nuttx.apache.org/docs/latest/applications/interpreters/wamr/index.html> |
| Bouffalo wasm SDK 2.0（上游 Vela Wasm Framework 内） | <https://github.com/open-vela/frameworks_runtimes_wasm/blob/dev/vendor/bouffalo/README.md> |

---

## 5. 限制、已知问题与排错

### 5.1 能力与设计限制

- **模块 ABI**：只接受 `wasm32`/`wasm64`（由 `CONFIG_ARCH_64BIT` 决定）的 WebAssembly 模块；模块内**不能用 NuttX 专有 API**，只能用 POSIX 兼容子集或自己注册的 host function。
- **WASI 不完整**：`INTERPRETERS_WAMR_LIBC_WASI` 的 Kconfig help 明确写了「大部分文件系统操作尚未实现（主要因为 NuttX 缺 openat 系列）」，且只承诺在 WAMR `main` 分支可用。文件相关需求要靠自定义原生模块，或先验证再依赖。
- **AOT 依赖可执行内存**：`INTERPRETERS_WAMR_AOT` 只有在平台提供 text heap（`ARCH_HAVE_TEXT_HEAP`）时才自动可用；NuttX 上基于 SIGSEGV 的 HW bounds check 也不可用，不要把它当安全边界。
- **静态扩展**：原生模块是编译期注册的，运行时不能动态装载（`--native-lib` 的 `.so` 机制面向桌面场景）。
- **进程模型**：`iwasm` 一次只跑一个模块，模块之间不共享内存；多模块（`MULTI_MODULE`）、线程/共享内存属于实验性能力。
- **版本差异**：Kconfig 默认 `WAMR-2.1.0`，但本仓 `sim:wamr` 钉的是 1.1.2；公开 API（如 `wasm_runtime_terminate`）和选项集合需要按实际版本确认。
- **生态位置**：openvela 的 netlib/WiFi/xtables WASM 封装、Vela Wasm Framework（libc/memory/vela-sysroot/irq/wapi/xtables/vendor）都不在本 SDK manifest 内，需要时单独引入。

### 5.2 已知构建问题（本仓实测）

#### 问题 1：WAMR 1.1.2 启用线程/WASI 后编译报 pthread 类型不完整 ✅

**症状**：

```
wamr/core/iwasm/aot/../common/wasm_exec_env.h:116:16: error: field 'wait_lock' has incomplete type
wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src/locking.h:87:5:
    error: unknown type name 'pthread_rwlock_t'
```

**根因**：WAMR 1.1.2 的 NuttX 平台头 `core/shared/platform/nuttx/platform_internal.h` 直接 `typedef pthread_t/pthread_mutex_t/pthread_cond_t`，却没有 `#include <pthread.h>`；libc-wasi 的 `locking.h` 同样需要完整的 pthread 类型。

**处置**（优先选不需要改第三方源码的方案）：

1. 给 WAMR 源码的编译加 `-include pthread.h`：CMake 路径给 `wamr` target 追加编译选项，Make 路径在 `apps/interpreters/wamr/Makefile` 的 `CFLAGS` 里追加；
2. 临时改构建缓存：在 `apps/interpreters/wamr/wamr/core/shared/platform/nuttx/platform_internal.h` 的 `#include <semaphore.h>` 后加一行 `#include <pthread.h>`（本文实测做法；**重新下载 WAMR 源码会丢失**）；
3. 不要把 `INTERPRETERS_WAMR_THREAD_MGR` 一关了之——它是中断/终止能力的前提（§3.6）。

#### 问题 2：链接阶段 `make` 报 `错误 22`，日志停在 `mkallsyms.py` ✅

**症状**：

```
LD:  nuttx
.../nuttx/tools/mkallsyms.py .../nuttx/nuttx allsyms.tmp --noconst --orderbyname
make[1]: *** [Makefile:441：nuttx] 错误 22
```

且没有任何报错文本。

**根因**：`mkallsyms.py` 依赖 `pyelftools`/`cxxfilt`；缺失时脚本走 `except ModuleNotFoundError` 分支，静默 `os._exit(errno.EINVAL)`（22）。这些包在 openvela 预置的 `prebuilts/tools/python/dist-packages/` 里，需要 `PYTHONPATH` 才会被找到。

**处置**：`source build/envsetup.sh`，或按 §2.2 手动导出 `PYTHONPATH`。自检：`python3 -c 'import elftools, cxxfilt; print("ok")'`。

#### 其他注意事项

- 上游 NuttX 的 wamr 文档仍是 Make 时代写法，且写明「CMake scripts don't work for this configuration」——在本仓以本文实测的 Make 流程为准；CMake 路径是 openvela 主线，sim 上是否可直接走 CMake 未在本文验证 📖。
- WAMR 源码解压在源码树内（gitignore 已覆盖），会额外占磁盘；清理与离线预置见 §4.5。
- CMake 的原生模块注册文件用追加方式生成（§3.5），改模块列表后建议 clean 对应构建。

### 5.3 排错速查

| 症状 | 可能原因 | 处置 |
|---|---|---|
| WAMR 编译报 pthread 相关错误 | 见问题 1 | 加 `-include pthread.h` |
| 链接 `错误 22` / `mkallsyms.py` | 见问题 2 | 设置 `PYTHONPATH` |
| `iwasm: command not found` | `INTERPRETERS_IWASM_TASK` 未开 | menuconfig 打开 |
| `iwasm` 报打不开模块 | 路径不对；`/data` 只映射启动 cwd | 把文件放到 `nuttx/` 下或用绝对路径 |
| 加载时报 unresolved symbol | 符号未注册，或模块名/签名与 wasm import 不一致 | `iwasm -v=5`；检查 `wasm_runtime_register_natives` 返回值 |
| AOT 报 invalid/版本错误 | `.aot` 与运行时版本或目标架构不匹配 | 用同一套工具链重新 `wamrc` |
| 框架模块加载报 `integer too large` / `WASM module load failed` | 64 位主机上模块被编成 wasm64，而 WAMR 1.1.2 无 MEMORY64 支持 | 在 32 位目标上重编；sim 上用手工 wasm32 模块（§3.2 注意点 2） |
| 运行中栈溢出/莫名崩溃 | `INTERPRETERS_WAMR_STACKSIZE`、模块 `STACK_SIZE`、`STACK_GUARD_SIZE` 不匹配 | 加大宿主任务栈；核对模块栈；保留 guard 余量 |
| 模块内存不足 | `INITIAL_MEMORY_SIZE`、`--heap-size`、`GLOBAL_HEAP_POOL_SIZE` 太小 | 逐项加大并观察内存余量 |
| menuconfig 里解释器选项点不动 | 互斥 choice，默认 `INTERPRETERS_NONE` | 三选一 |
| `exit` 后模拟器不退出 | NSH `exit` 只结束会话 | 用 `poweroff` |

---

## 6. BL616CL：实验性评估

> 本章是**可行性评估与实验路线**，不是已验证的集成方案：本 SDK 当前没有任何 BL616CL 配置启用 WAMR，也没有实机验证结论。

### 6.1 现状 📖

- `vendor/bouffalolab/boards/bl616cl/ai-m64l-32s-kit/configs/` 下的 `nsh`/`nsh-peripherals`/`ostest`/`wifi` 都没有 Wasm/WAMR 选项；
- `chips/bl616cl`、`boards/bl616cl` 内没有 wasm 相关代码；
- 本 SDK 的 manifest 也没有拉取 openvela 的 Vela Wasm Framework 与 Bouffalo wasm SDK 2.0。

### 6.2 约束分析

| 维度 | 现状 | 影响 |
|---|---|---|
| 执行模式 | `chips/bl616cl/Kconfig` 没有 `ARCH_HAVE_TEXT_HEAP` | `INTERPRETERS_WAMR_AOT` 自动 select 不生效，**AOT 不可用**；第一版只能 INT（XIP 是否可行未验证） |
| RAM | WRAM 128–160 KiB（默认 128，另有 EM 占用 0/16/32 KiB）；OCRAM+WRAM 窗口 384 KiB | WAMR fast 解释器 + 一个 64 KiB 线性内存的模块会很紧张；需要实测内存余量，必要时用 PSRAM（需适配 WAMR 的内存映射，未验证） |
| 中断 | VM 不暴露 IRQ，NuttX 正常抢占 | wasm 想响应硬件中断，需要在 BL 侧写 native 模块（§3.6），或移植上游 `irq` 模块 |
| 工具链 | 预置 clang-wasm 与 BL616CL 的 riscv 工具链互不影响 | 模块编译走 clang-wasm；固件编译走 vela 的 riscv 工具链 |

### 6.3 若要在 BL616CL 上做实验

按仓库约定**不新增 wasm 专用 defconfig**，在现有配置（建议 `nsh-peripherals`，或用本地临时配置）上实验：

```bash
source build/envsetup.sh
./vela menuconfig ai-m64l-32s-kit/nsh-peripherals
# Application Configuration → Interpreters → Webassembly Micro Runtime
#   [*] Webassembly Micro Runtime
#   [*] Webassembly iwasm task
#   (二选一) Enable fast interpreter / Enable classic interpreter
#   [*] Enable built-in libc            （需要 printf 等）
#   [ ] Enable WASI libc               （先不开启，见 §5.1）
#   [*] Enable thread manager          （需要中断/终止时；注意 §5.2 问题 1）
# Extra Tools → Wasm Build Options
#   [*] Enable Wasm build support      （CMake 路径编 wasm 模块）
# Application Configuration → Examples
#   [*] "Hello, WebAssembly!" example  （可选，验证框架）

./vela build ai-m64l-32s-kit/nsh-peripherals -j14
```

注意事项：

- `vela menuconfig` 退出时会**自动回写板级 defconfig**；实验性改动要么不提交、用完恢复，要么单独提交并经过评审。
- 改动 `BL616CL_EM_*` 等链接脚本相关 CONFIG 后必须 `vela clean`（增量构建会静默沿用旧内存布局）。
- 模块产物在 `cmake_out/ai-m64l-32s-kit_nsh-peripherals/wasm/`；设备侧需要可访问这些文件（ROMFS 只读、编译期生成；实验可先走 `/data` 分区或用 `INSTALL_NAME` 打进 romfs）。
- 烧录/串口操作按仓库技能（`vela flash` + `bl-module-reset`），不要手写 DTR/RTS 时序。

### 6.4 第一步的验收清单（建议）

1. `nsh-peripherals` 能完整编译、镜像体积增量可接受；
2. `iwasm` 能启动；加载并运行一个 64 KiB 以内的 INT 模块（如 hello）；
3. 记录空闲内存/堆余量、iwasm 任务栈峰值；
4. 对照未开启 WAMR 的镜像，确认 Wi-Fi/BLE（EM/WRAM）不受影响；
5. 再决定是否推进 AOT/XIP、线程、原生扩展等能力。

---

## 维护提示

本文与以下实现强相关，改动它们时请同步本文：`apps/interpreters/wamr/`（Kconfig、Makefile、CMakeLists、Module.mk）、`apps/tools/Wasm*`、`apps/cmake/nuttx_wasm_interface.cmake`、`nuttx_add_wamrmod.cmake`、`apps/examples/hello_wasm/`、`apps/examples/wamr_module/`。

