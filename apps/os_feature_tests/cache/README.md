# BL616CL Cache 测试

## 背景

BL616CL 启动代码原本只在启动和镜像加载阶段执行整 cache 操作，OpenVela 的
`nuttx/cache.h` 公共接口没有完整 lower-half 实现。DMA、XIP 和 RAM 中的可执行代码
分别需要 D-cache clean、D-cache invalidate、I-cache invalidate 以及 coherent 顺序。

测试代码只位于 `apps/os_feature_tests/cache/`，通过 OpenVela 公共 cache ABI 和实际硬件
行为验证接口，不在 chip adapter 中增加 test Kconfig、私有 header、hook 或 bypass。

## 命令

在已经启动到 NSH 的测试固件中执行：

```text
cache_test 001
cache_test 002
cache_test 003
cache_test 004
cache_test 005
cache_test all
```

`cache_test all` 依次执行 001..005，并返回：`0` 表示全部 PASS，`1` 表示有 FAIL。

## 测试流程与判据

### CACHE-001：公共 ABI 几何

读取 I/D cache line size 和 size getter。判据为 line size 32 字节、I-cache 32 KiB、
D-cache 16 KiB。

### CACHE-002：RAM 代码 coherent

在 RAM 写入返回 1 的指令并执行 `up_coherent_dcache()`，确认可执行；再把指令改为返回
2，重复 coherent 并确认新指令生效。该行为验证 D-cache clean 与 I-cache invalidate
组成的公共 coherent 路径。

### CACHE-003：运行时 I/D 开关

在临界区内重复 enable/disable，通过测试程序本地读取 MHCR，确认 I/D 控制位互不影响；
同时在 cached RAM 写入 dirty byte，确认 D-cache disable 后 non-cacheable alias 可见，
重新 enable 后数据保持。所有退出路径恢复 I/D cache enabled。

### CACHE-004：range 与 partial RX ownership

通过 cached/non-cacheable alias 检查 D-cache clean 和 aligned invalidate 的可见性；对
`[33,63)` partial range 执行 invalidate 后由 non-cacheable alias 写入，确认 line 两侧
sentinel 不被破坏；再对 `[1,127)` 执行三行 partial invalidate，验证真实的
`first CI -> middle invalidate -> last CI` ownership 合同。

### CACHE-005：整 D-cache 操作

检查 flush-range、clean-all、flush-all 的 producer 数据可见性，并调用 I-cache range
和 all invalidate；随后切换到
`.nocache_noinit_ram` 专用栈执行 invalidate-all，确认 producer 数据与执行上下文正常。

## 静态审查边界

公共 API 返回 `void`，非法地址和超大 range 无法从 app 观察内部 no-op 或分块顺序。
地址域、整数溢出、固定分块上限和 `first CI -> middle invalidate -> last CI` 的分派由
源码审查和构建产物承担；测试程序不通过 chip 私有 hook 自证这些内部路径。
