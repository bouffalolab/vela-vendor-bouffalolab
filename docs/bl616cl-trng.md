# BL616CL TRNG 随机设备适配与验证

本文说明 BL616CL 如何把安全引擎 TRNG 接入 OpenVela 标准 `/dev/random`，并给出
配置裁剪、构建检查、标准 API、USB2 运行和现役外设回归的完整流程。运行环境为
Ai-M64L-32S-Kit、`/dev/ttyUSB2`、2,000,000 baud。

## 背景

BL616CL LHAL 已有 `sec_trng` device table 和 32 B TRNG 读取函数，但当前 LHAL
实现存在三个不适合直接暴露为 VFS read 的边界：

- timeout 直接返回，未清理 `EN/TRIG/DOUT/INT` 临时状态；
- 没有检查硬件 `HT_ERROR`；
- 任意长度、部分成功、并发和标准设备节点语义不属于 LHAL 职责。

OpenVela 不提供通用 RNG lower-half。架构在 `ARCH_HAVE_RNG` 下实现完整的
`devrandom_register()`；`drivers_initialize()` 负责在 OS 服务可用、用户初始化开始前
注册 `/dev/random`。因此本项实现的是 chip-owned 字符设备 adapter，而不是新增
私有 ioctl 或修改 NuttX 上游 random 框架。

## 正式方案

### 配置边界

正式配置：

```text
CONFIG_BL616CL_TRNG=y
CONFIG_DEV_RANDOM=y
# CONFIG_DEV_URANDOM is not set
# CONFIG_BL_MCU_PERIPHERAL_TESTS_TRNG is not set
```

配置职责如下：

| 选项 | 作用 | 关闭效果 |
|---|---|---|
| `BL616CL_TRNG` | 编入 chip adapter，并声明 `ARCH_HAVE_RNG` | 不编译 `bl616cl_rng.c`，不产生标准硬件随机设备 |
| `DEV_RANDOM` | 由 NuttX 启动路径调用 `devrandom_register()` | 不注册 `/dev/random` |
| `DEV_URANDOM` + `DEV_URANDOM_ARCH` | 可选地调用同一 adapter 注册 `/dev/urandom` | 不注册硬件 `/dev/urandom` |
| `BL_MCU_PERIPHERAL_TESTS_TRNG` | 编入独立测试 archive 和 `mcu_trng_test` | 正式产品无测试命令、测试代码和字符串 |

测试 app 默认关闭，单独编译为 `libapps_mcu_trng_test.a`。TRNG adapter 条件加入
chip `arch` target，最终位于 `libarch.a` 对应对象中，不形成独立 driver archive。

### 不修改同步驱动仓

`vendor/bouffalolab/drivers/` 由 Bouffalo SDK CI 同步，本项保持只读。adapter 在
`chips/bl616cl/bl616cl_rng.c` 中复用 device table、时钟宏和寄存器定义，并复制
LHAL 必需的寄存器事务。这样既不修改同步源，也能补齐 OpenVela 需要的清理、健康
错误和 VFS 语义。

没有把 `bflb_sec_trng.c` 额外编入 OpenVela wrapper；也没有调用其中忽略健康错误、
失败后不清理的 `bflb_trng_read()`。

## 硬件事务

初始化只执行一次：

```text
PERIPHERAL_CLOCK_SEC_ENABLE()
  -> bflb_device_get_by_name("sec_trng")
  -> 清理 TRIG/DOUT/EN/INT 临时状态
```

每个 32 B 块的事务为：

```text
设置 EN
  -> 清 INT
  -> 4 个 nop，等待 BUSY 建立窗口
  -> 等待 idle，最长 100 ms
  -> 检查 HT_ERROR
  -> 清 INT 并设置 TRIG
  -> 4 个 nop，等待 BUSY 建立窗口
  -> 等待 idle，最长 100 ms
  -> 再次检查 HT_ERROR
  -> 读取 DOUT0..DOUT7，逐字转换为小端 32 B
  -> 清 TRIG
  -> DOUT_CLR pulse
  -> 清 EN
  -> 清 INT
```

两个 idle 阶段都检查 `HT_ERROR`，避免把初始化阶段或生成阶段的健康错误数据交给
调用者。清理顺序与当前 LHAL 正常路径一致，但正常产品不改变硬件默认的 RCT/APT
参数，也不据此声明健康认证。

## VFS 语义

- 零长度 read 直接返回 0，不触发硬件事务。
- 非零 read 在整个调用期间持有一个 NuttX mutex，以 32 B 分块生成并复制到调用者
  的任意字节地址。
- 首块失败返回原始负 errno：timeout 为 `-ETIMEDOUT`，`HT_ERROR` 为 `-EIO`。
- 已成功复制部分块后失败，返回有效短读；不会用固定值或软件 PRNG 补齐。
- 临时 32 B block 在成功复制后和退出前清零。
- `poll()` 对 `POLLIN` 立即通知；这表示设备接受读取，不表示硬件事务零延迟。
- 未实现私有 ioctl，VFS 对未知命令返回 `ENOTTY`。
- 设备以 `0444` 注册，实测为 `cr--r--r--`。

## 调用链

```text
CONFIG_BL616CL_TRNG
  -> select ARCH_HAVE_RNG
  -> NuttX 默认允许 CONFIG_DEV_RANDOM
  -> CMake 条件编入 bl616cl_rng.c

drivers_initialize()
  -> devrandom_register()
  -> enable SEC clock / resolve sec_trng
  -> register_driver("/dev/random", ..., 0444)

read("/dev/random", buffer, length)
  -> lock complete VFS read
  -> generate 32 B blocks
  -> copy full or tail bytes to arbitrary buffer alignment
  -> unlock

getrandom(..., GRND_RANDOM)
  -> open/read/close /dev/random
```

可选 `DEV_URANDOM_ARCH` 时，`devurandom_register()` 用相同 file operations 注册
`/dev/urandom`；默认 `getrandom(..., 0)` 走该节点。

## 配置与构建流程

以下命令均在 SDK 根目录执行。`defconfig` 是自动生成文件，配置变更通过生成目录
的 `.config` 和 `savedefconfig` 完成。

### 1. 关闭态 clean build

```sh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14

prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak \
  --file cmake_out/ai-m64l-32s-kit_nsh/.config \
  --disable BL_MCU_PERIPHERAL_TESTS_TRNG \
  --disable BL616CL_TRNG
cmake --build cmake_out/ai-m64l-32s-kit_nsh -t savedefconfig

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

完成判据：clean build 成功；生成配置无 `BL616CL_TRNG/DEV_RANDOM`；构建树和
ELF 无 `bl616cl_rng.o`、`devrandom_register`、TRNG file operations 或测试命令。

### 2. `/dev/random` 测试固件

```sh
prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak \
  --file cmake_out/ai-m64l-32s-kit_nsh/.config \
  --enable BL616CL_TRNG \
  --enable DEV_RANDOM \
  --disable DEV_URANDOM \
  --enable BL_MCU_PERIPHERAL_TESTS_TRNG
cmake --build cmake_out/ai-m64l-32s-kit_nsh -t savedefconfig

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

完成判据：生成 `libapps_mcu_trng_test.a`；ELF 同时包含
`devrandom_register` 和 `mcu_trng_test`；`help` 可见测试命令。

### 3. urandom-only 组合

该组合用于验证两个标准节点的 Kconfig 独立性，不是正式产品配置：

```sh
prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak \
  --file cmake_out/ai-m64l-32s-kit_nsh/.config \
  --disable DEV_RANDOM \
  --enable DEV_URANDOM \
  --enable DEV_URANDOM_ARCH \
  --enable BL_MCU_PERIPHERAL_TESTS_TRNG
cmake --build cmake_out/ai-m64l-32s-kit_nsh -j14
```

完成判据：ELF 有 `devurandom_register`、无 `devrandom_register`；`/dev/random`
不存在而 `/dev/urandom` 存在；`mcu_trng_test all` 通过。

### 4. 恢复正式产品并 clean build

```sh
prebuilts/build-tools/linux-x86_64/bin/kconfig-tweak \
  --file cmake_out/ai-m64l-32s-kit_nsh/.config \
  --enable BL616CL_TRNG \
  --enable DEV_RANDOM \
  --disable DEV_URANDOM \
  --disable BL_MCU_PERIPHERAL_TESTS_TRNG
cmake --build cmake_out/ai-m64l-32s-kit_nsh -t savedefconfig

python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

完成判据：生成配置与本文正式配置一致；测试 archive、命令和字符串消失；
`/dev/random` 的 driver 对象和注册符号保留。

## 静态制品核查

```sh
OUT=cmake_out/ai-m64l-32s-kit_nsh
TOOL=prebuilts/gcc/linux-x86_64/riscv-none-elf/bin/riscv-none-elf

grep -E 'CONFIG_(BL616CL_TRNG|DEV_RANDOM|DEV_URANDOM|BL_MCU_PERIPHERAL_TESTS_TRNG)' \
  "$OUT/.config"
"$TOOL-nm" -S "$OUT/final_nuttx" | \
  grep -E 'dev(random|urandom)_register|bl616cl_rng'
"$TOOL-size" "$OUT/final_nuttx"
sha256sum "$OUT/nuttx.bin"
find "$OUT" -type f -name 'libapps_mcu_trng_test.a'
```

正式产品还需执行：

```sh
test ! -e "$OUT/apps/vendor/bouffalolab/apps/mcu_peripheral_tests/trng/libapps_mcu_trng_test.a"
test -z "$(strings "$OUT/final_nuttx" | grep mcu_trng_test)"
```

## 固件运行流程

本节从已经进入 NSH 的固件开始，不包含烧录动作。

### 测试固件

1. 执行 `help`，确认 `mcu_trng_test` 存在。
2. 执行 `ls -l /dev/random`，确认只读字符设备；urandom-only 配置改查
   `/dev/urandom`，并确认 `/dev/random` 不存在。
3. 执行 `mcu_trng_test lengths`，核对十种长度、`align=1`、canary 和完整返回。
4. 执行 `mcu_trng_test api`，核对 `poll=0001`、未知 ioctl 和 `getrandom()`。
5. 连续执行三次 `mcu_trng_test stats`，记录 bit 计数、固定块、重复块和耗时。
6. 连续执行五次 `mcu_trng_test concurrent`，每轮核对四线程、32 次和 13215 B。
7. 执行 `mcu_trng_test all`，确认跨 case 资源清理后仍全部通过。
8. 执行现役 GPIO、timer、oneshot、WDT 回归，最后输出存活标志。

### 正式产品

1. 执行 `help`，确认没有 `mcu_trng_test`。
2. 执行 `ls -l /dev/random`，确认设备权限为 `cr--r--r--`。
3. 执行 `dd if=/dev/random of=/dev/null bs=32 count=4`，必须返回 `nsh>`。
4. 执行现役外设回归并输出最终存活标志。

测试 case 的逐步实现和完整运行输出见同目录测试说明
`apps/mcu_peripheral_tests/trng/README.md`；该 README 直接保存关键结果，不依赖
外部任务日志。

## 构建与尺寸实测

| 配置 | 构建 | `final_nuttx` | text | data | bss | `nuttx.bin` |
|---|---:|---:|---:|---:|---:|---:|
| TRNG 关闭、测试关闭 | 1219/1219 | 833792 B | 440116 B | 15040 B | 20364 B | 460800 B |
| TRNG 开启、random 测试开启 | 1222/1222 | - | - | - | - | 468976 B |
| TRNG 开启、urandom-only 测试开启 | 1222/1222 | 844524 B | - | - | - | 468944 B |
| TRNG 开启、测试关闭 | 1220/1220 | 838356 B | 441676 B | 15120 B | 20364 B | 462448 B |

正式 TRNG 相对关闭态：ELF `+4564 B`、text `+1560 B`、data `+80 B`、bss
`+0 B`、`nuttx.bin +1648 B`。最终正式 `nuttx.bin` SHA256：

```text
9c59b054cdc0547d4f64795ce81874cc07f38a625023893e3304b9a43f64925a
```

烧录校验中 host/device SHA 均为该值，读取校验长度为 462448 B。

urandom-only 测试态的 `libapps_mcu_trng_test.a` 为 17368 B，ELF 只包含
`devurandom_register()`，不包含 `devrandom_register()`；`nuttx.bin` SHA256 为：

```text
3e31409a2a94294a95c1feece221c107a8eda3fc6714c8ae8e44744484d82785
```

该镜像烧录时 host/device SHA 一致，读取校验长度为 468944 B。

## `/dev/random` USB2 实测

长度 case 的十种请求全部 PASS，实际 buffer 均为 `align=1`。API 输出：

```text
TRNG_TEST RESULT case=api PASS poll=0001 checksum=1d8cf234
```

4096 B 统计四轮：

| 轮次 | 1 bit 数 | 千分比 | 固定块 | 重复块 | 耗时 |
|---|---:|---:|---:|---:|---:|
| 1 | 16272 | 496 | 0 | 0 | 9851 us |
| 2 | 16312 | 497 | 0 | 0 | 9822 us |
| 3 | 16328 | 498 | 0 | 0 | 9819 us |
| all 内 | 16422 | 501 | 0 | 0 | 9634 us |

并发 case 共六轮全部 PASS，每轮 4 线程、每线程 32 次、合计 13215 B。`all` 最终
输出：

```text
TRNG_TEST RESULT case=lengths PASS count=10
TRNG_TEST RESULT case=api PASS poll=0001 checksum=08058ea8
TRNG_TEST RESULT case=stats PASS bytes=4096 ones=16422 \
ones_permille=501 fixed=0 repeated=0 elapsed_us=9634 checksum=8efb8e00
TRNG_TEST RESULT case=concurrent PASS threads=4 iterations=32 bytes=13215
TRNG_TEST RESULT case=all PASS
nsh>
```

## `/dev/urandom` USB2 组合实测

urandom-only 固件中 `/dev/urandom` 权限为 `cr--r--r--`，`/dev/random` 不存在。
测试 runner 明确选择 `/dev/urandom`，十种长度、API、三轮独立统计、五轮独立并发
和一轮 `all` 全部通过：

```text
TRNG_TEST RESULT case=lengths PASS count=10
TRNG_TEST RESULT case=api PASS poll=0001 checksum=0cb6f26d
TRNG_TEST RESULT case=stats PASS bytes=4096 ones=16182 \
ones_permille=493 fixed=0 repeated=0 elapsed_us=9820 checksum=9b6c8eab
TRNG_TEST RESULT case=concurrent PASS threads=4 iterations=32 bytes=13215
TRNG_TEST RESULT case=all PASS
nsh>
```

四次 4096 B 统计的 1 bit 千分比为 495、507、501、493，固定块和重复块均为
0；六轮并发均为 4 线程、每线程 32 次、合计 13215 B。runner 汇总为
`bytes=8899 stats=4 concurrent=6 failures=[]`。现役 GPIO、timer、oneshot、WDT 和
最终 NSH 存活回归同样通过。

## 正式产品回归

```text
nsh> ls -l /dev/random
 cr--r--r--           0 /dev/random
nsh> dd if=/dev/random of=/dev/null bs=32 count=4
nsh> mcu_gpio_test -c edge --out /dev/gpio12 -n 3 -v
[GPIO-edge] PASS rejected invalid operations and recovered for 3 cycles
nsh> mcu_timer_test -c 001 -t 100000 -n 5 -e 5 -v
RESULT max_err=862.0us (0.862%) tol=5000.0us (5.00%)
[TIMER-001] PASS accuracy within tolerance
nsh> mcu_timer_test -c 002 -t 500000 -a 39 -b 79 -v
ratio period_b/period_a=2.000 (expected 2.000, tol +/-5%)
[TIMER-002] PASS prescaler takes effect
nsh> mcu_timer_test -c 005
[TIMER-005] PASS rejected requests preserved state; live update fired; lifecycle recovered
nsh> oneshot -d 100000 /dev/oneshot
Finished
nsh> mcu_wdt_test -c 002 -t 1000 -p 3000 -i 500 -v
PASS: fed 6 times over 3026ms, no reset; watchdog stopped
nsh> mcu_wdt_test -c 003 -t 1000
PASS: invalid/live changes rejected; duplicate lifecycle preserved state
nsh> echo ST015_FINAL_ALIVE
ST015_FINAL_ALIVE
nsh>
```

全程没有意外 assert、panic、KASAN/UBSAN 报告或 watchdog reset。

## 限制与残留风险

- 正常读数未出现 timeout 或 `HT_ERROR`；没有硬件故障注入，因此失败后的 cleanup
  恢复未做实板验证。
- 多线程 case 证明调用均完成，但在单核同优先级 FIFO 下不确定性证明 mutex 实际
  发生竞争；不能把该结果描述为锁竞争时序验证。
- 4096 B bit 计数和重复块检查只用于数据链路冒烟，不证明熵质量或认证等级。
- poll 立即可读不代表读操作无延迟；实测 4096 B 单次 read 约 9.6~9.9 ms。
- 本项没有配置硬件 RCT/APT 参数，也没有验证温度、电压、长时或重启间统计。
