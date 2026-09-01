# BL616CL Cache 测试

## 背景

BL616CL 启动代码原本只在启动和镜像加载阶段执行整 cache 操作，OpenVela 的
`nuttx/cache.h` 公共接口没有完整 lower-half 实现。DMA、XIP 和 RAM 中的可执行代码
分别需要 D-cache clean、D-cache invalidate、I-cache invalidate 以及 coherent 顺序。
本测试覆盖接口几何信息、地址域、分块、partial invalidate 的 ownership、coherent
顺序、运行时开关、cacheable/non-cacheable alias 和整 cache 操作。

测试程序是一个 `cache_test` main，通过参数选择 case，不把一个 main 拆成多个应用。
I-cache 32 KiB、D-cache 16 KiB 采用重新确认的芯片规格，本轮不以 working-set 或 HPM
推测容量，只检查公共 getter 是否发布正确规格。

## 命令

在已经启动到 NSH 的 `st036-cache-test` 固件中执行：

```text
cache_test 001
cache_test 002
cache_test 003
cache_test 004
cache_test 005
cache_test 006
cache_test 007
cache_test 008
cache_test all
```

`cache_test all` 依次执行 001..008，并返回：`0` 表示全部 PASS，`1` 表示有 FAIL。
测试只记录固件运行时行为，不包含烧录步骤；烧录由板级验证流程单独完成。

## 测试流程与判据

### CACHE-001：公共 ABI 几何和 all 操作

流程：

1. 读取 `up_get_icache_linesize()`、`up_get_dcache_linesize()`、
   `up_get_icache_size()` 和 `up_get_dcache_size()`。
2. 安装 test-only trace hook 并开启硬件 bypass，使本 case 只检查路由。
3. 依次调用 I enable、I disable、I invalidate-all、D enable、D disable、D clean-all、
   D invalidate-all、D flush-all。
4. 关闭 trace hook，逐项检查 8 个 operation event 的顺序、操作码、地址和长度。

判据：line size 为 32 字节，接口报告 I=32 KiB、D=16 KiB；8 个 all 操作各产生一个
地址和长度均为 0 的 operation event。实测 8 个路由全部通过。

### CACHE-002：地址域和对齐

流程：

1. 取 linker 导出的 cacheable RAM 起点、XIP 起点和 RAM 终点。
2. 对 RAM 起点加 1、长度 63 调用 D clean，检查向下/向上对齐后覆盖 64 字节。
3. 对 XIP 起点加 1、长度 63 调用 I invalidate，检查同样的 64 字节范围。
4. 分别传入 non-cacheable alias、普通 MMIO 地址和跨 RAM 终点的范围。
5. 检查有效范围产生 operation event，非法范围只产生 reject event，不调用 LHAL。

判据：RAM 和 XIP 可接受；non-cacheable alias、MMIO 和跨域范围拒绝。实测结果为
`RAM/XIP accepted; nocache/MMIO/cross rejected`。

### CACHE-003：分块、空区间和溢出

流程：

1. 将 test-only chunk limit 设为 64 字节。
2. 对 RAM 发送 160 字节 flush，检查分块为 64、64、32，且地址连续。
3. 发送 `start == end` 和 `start > end`，检查只产生 no-op event。
4. 发送接近 `UINTPTR_MAX` 的 32 字节区间，检查加法溢出被 reject。
5. 清除 trace hook，确认 bypass 只影响硬件调用，不改变边界判定。

判据：分块顺序固定为 `64,64,32`；空区间和反向区间无操作；溢出不进入 LHAL。实测
上述 6 个事件全部匹配。

### CACHE-004：partial D-cache invalidate ownership

流程：

1. 对单个 cache line 的前部、后部和跨 line 的区间分别调用 D invalidate。
2. 将跨 line 的区间拆成 first partial、middle full、last partial 三种组合。
3. 检查 first/last partial line 使用 clean+invalidate（flush），完整 line 只使用
   invalidate。
4. 检查同一 line 的完整覆盖不会错误地 clean，也不会丢失相邻 sentinel 内容。

判据：单 line partial、跨 line first-CI/middle-I/last-CI 的 9 个 operation event
顺序与 ownership 算法一致。实测全部通过。

### CACHE-005：coherent 顺序、RAM 代码和长度溢出

流程：

1. 对 cacheable RAM 调用 `up_coherent_dcache()`，检查先 D clean、后 I invalidate。
2. 对 XIP 地址调用同一接口，检查 D-cache 域拒绝，而不是误操作 XIP。
3. 发送零长度和 `UINTPTR_MAX` 附近的长度，检查分别为 no-op 和 reject。
4. 在 RAM 中写入 `addi a0, zero, 1; ret`，执行 coherent 后调用函数，期望返回 1。
5. 把第一条指令改为返回 2，再次执行 coherent，期望新指令立即生效。

判据：D/I 事件顺序、边界事件和两次 RAM 代码执行结果均正确。实测输出为
`D-clean/I-invalidate order and RAM update` PASS。

### CACHE-006：运行时 I/D 开关和 MHCR 独立性

流程：

1. 进入临界区，先幂等调用 I/D enable，并读取 MHCR 的 IE、DE 位。
2. 只关闭 I-cache，确认 IE 清零而 DE 保持；重复关闭后重新开启 I-cache。
3. 在 cached RAM 写入 dirty byte，通过 D-cache disable 触发 clean，再读取
   non-cacheable alias。
4. 只关闭 D-cache，确认 DE 清零而 IE 保持；重复关闭后重新开启 D-cache。
5. 读取 alias 和 cached 地址，确认 dirty 数据没有丢失，最后恢复 I/D enable。

判据：I/D 控制位互不影响，重复 enable/disable 不重复执行破坏性操作，dirty byte
在切换后仍为 `0x61`。实测幂等、MHCR 和 dirty persistence 全部通过。

### CACHE-007：cached/non-cacheable 可见性和 partial RX

流程：

1. 在 cached alias 填充 `0x11`，执行 D clean，在 non-cacheable alias 逐字节读取。
2. 对完整的 `[32,96)` 范围做 D invalidate，从 non-cacheable alias 写入 `0x22`，
   检查 cached alias 只在该范围看到新数据。
3. 对 partial 的 `[33,63)` 范围做 D invalidate，写入 `0x44`，检查两侧 sentinel
   仍保持 `0x33`。

判据：clean 后 producer 数据对 alias 可见；invalidate 后 consumer 读取到新数据；
partial RX 不破坏 line 外数据。实测完整范围和 sentinel 检查均通过。

### CACHE-008：all 操作和 non-cacheable stack

流程：

1. 写入 `0x51`，调用 D clean-all，从 non-cacheable alias 检查可见。
2. 写入 `0x52`，调用 D flush-all，再次检查可见。
3. clean-all 后由 non-cacheable alias 写入 `0x53`。
4. 进入临界区，在专用 `.nocache_noinit_ram` 栈上运行 invalidate-all trampoline，
   避免 D-cache invalidate-all 期间访问普通 cached stack。
5. 退出临界区并检查 cached alias 读到 `0x53`，确认 all 操作后的执行上下文安全。

判据：clean-all、flush-all 和 invalidate-all 三条路径均保持 producer 数据，trampoline
执行期间没有异常。实测全部通过。

## 固件运行关键证据

启动后出现 `NuttShell (NSH) NuttX-3.6.1`，`help` 的 Builtin Apps 包含
`cache_test`、`mcu_gpio_test`、`mcu_timer_test` 和 `mcu_wdt_test`。最终固件执行
`cache_test all`，CACHE-001 报告 `line=32 I=32768 D=16384`，CACHE-001..008
全部 PASS，汇总为
`Cache Summary: pass=8 fail=0 -> PASS`。

同一固件完成现役回归：GPIO edge 3 个恢复周期通过；TIMER-001 五轮 100 ms 最大
误差 0.647%；TIMER-002 分频 39/79 的周期比为 2.000；TIMER-005 生命周期通过；
oneshot 100000 us 正常结束；WDT-002 喂狗 9 次无复位；
WDT-003 边界和生命周期通过；`/dev/rtc0` 存在，`date; sleep 2; date` 前后相差
2 秒；最终 alive 标记正常回显。
