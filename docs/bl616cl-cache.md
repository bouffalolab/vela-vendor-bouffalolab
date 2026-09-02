# BL616CL Cache 适配与验证

> `nsh` 是当前正式入口；历史三态仅作为验证记录，不再对应独立 board 配置目录。

## 背景

BL616CL E907 启动路径已经启用 L1 I-cache 和 D-cache，但此前只提供启动阶段的
整 cache clean/invalidate，未实现 OpenVela `nuttx/cache.h` 的公共 range、all、toggle
和 coherent ABI。这个缺口会影响 XIP 指令取指、RAM 中动态代码、DMA buffer 的
producer/consumer ownership，以及调试和文件系统路径对 cache 的统一调用。

本适配以现役 BL616CL 代码、LHAL、linker 地址域和实板行为为准。经芯片规格重新确认，
I-cache 为 32 KiB，D-cache 为 16 KiB；BL616CL 1.0 手册中的 16 KiB/8 KiB 已过期。
本轮直接采用确认后的规格，不再用 working-set 或 HPM 推测容量。

## 最大能力交集

| 能力 | 结论 | 依据与限制 |
|---|---|---|
| 32 字节 cache line | 纳入 | L1C range 原语和寄存器操作均按 32 字节对齐 |
| I/D enable、disable | 纳入 | LHAL 提供独立 MHCR IE/DE 控制 |
| I/D invalidate-all | 纳入 | 启动代码已有对应指令序列 |
| D clean、invalidate、flush range | 纳入 | LHAL 提供 range clean/invalidate/clean-invalidate |
| D clean、invalidate、flush all | 纳入 | LHAL 提供整 cache 原语 |
| coherent D-clean -> I-invalidate | 纳入 | RAM 中自修改代码需要固定顺序 |
| RAM/XIP 地址域检查 | 纳入 | linker 导出 cacheable RAM/XIP 边界；D-cache 只接受 RAM |
| partial D invalidate | 纳入 | 首尾 partial line 使用 flush，中间完整 line 使用 invalidate |
| 超大 range 分块 | 纳入 | 对齐后按 `INT32_MAX` 内最大块调用 uint32 LHAL |
| runtime toggle 幂等性 | 纳入 | 读取 MHCR 后只在状态变化时调用 enable/disable |
| cache 容量 getter | 纳入 | 重新确认的芯片规格为 I=32 KiB、D=16 KiB |
| DMA cache policy | 部分纳入 | DMA0 公共 mem2mem adapter 已接入；公共 API 提供 clean/invalidate，具体外设 consumer 仍由调用方定义 ownership |

## 调用链与归属

```text
bl616cl_start.c
  -> bl616cl_cache_early_init()
  -> bl616cl_cache_after_load()

OpenVela cache.h caller
  -> up_*cache()/up_coherent_dcache()
  -> chips/bl616cl/bl616cl_cache.c
  -> bflb_l1c_*() 或 E907 cache 指令
```

- `chips/bl616cl/bl616cl_cache.c` 属于 chip adapter，编入 `libarch.a`，实现公共 ABI、
  地址域、对齐、partial ownership、溢出保护和 LHAL 调用。
- `chips/bl616cl/Kconfig` 只提供生产能力开关 `BL616CL_CACHE`，chip adapter 不承载
  cache test 配置或接口。
- linker script 导出 XIP 和 RAM 边界符号，供 chip adapter 做范围判断。
- `apps/os_feature_tests/cache/` 只在测试配置下编入 `cache_test`，不进入产品 archive。
- `drivers/` 是 CI 自动同步的只读仓库；本适配不修改其中源码或构建文件。

## Kconfig 裁剪

| 选项 | 默认 | 作用 | 关闭结果 |
|---|---|---|---|
| `BL616CL_CACHE` | `y` | 打开 OpenVela cache 公共 ABI，并选择 `ARCH_ICACHE`/`ARCH_DCACHE` | 仅裁剪公共 ABI；启动期硬件 cache 仍按既有路径启用 |
| `BL_OS_FEATURE_TESTS_CACHE` | `n` | 仅通过公共 ABI 验证硬件行为的 `cache_test` | 无测试应用和相关字符串 |

`BL616CL_CACHE` 使用 `default y`，因此标准 `nsh` 产品配置默认获得公共 cache ABI；
显式关闭仍可用于裁剪验证。测试 app 依赖 `BL616CL_CACHE`，不会在 cache ABI 关闭时
绕过依赖强行编译。

## API 行为合同

1. range 参数采用 `[start,end)`；空区间和反向区间是 no-op。
2. 起点向下、终点向上按 32 字节对齐；对齐前先检查整数溢出。
3. I-cache range 接受 RAM 或 XIP；D-cache range 只接受 cacheable RAM，拒绝
   non-cacheable alias、MMIO 和跨域范围。
4. D-cache partial invalidate 的首尾 partial line 必须 clean+invalidate，避免保留
   字节被丢弃；完整 line 只 invalidate。
5. coherent 操作只接受 RAM，固定先 D clean 再 I invalidate。
6. 超过 LHAL 单次长度限制的 range 分块执行，块地址连续且不重叠。
7. enable/disable 读取 MHCR IE/DE 后幂等执行，I/D 状态互不影响。
8. 测试不得向 chip adapter 注入 hook、bypass 或测试专用接口；内部 no-op、拒绝和分块
   路径由源码审查，公开行为由 app 侧实板测试验证。

## 构建与静态验证

在 SDK 根目录执行：

```bash
python3 vendor/bouffalolab/bl_build.py clean \
  bl616cl/ai-m64l-32s-kit/configs/nsh
python3 vendor/bouffalolab/bl_build.py build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14
```

标准产品构建应有完整公共 ABI 且无 test app。需要验证 app 时，通过 menuconfig 启用
`BL_OS_FEATURE_TESTS_CACHE` 后另建测试固件；测试态只额外包含 `cache_test_main` 和 app
侧 trampoline。chip 目录在所有配置下均不得出现 cache test 配置、header、hook 或符号。
defconfig 由 menuconfig/savedefconfig 生成，不手工维护 `.config`。

## 固件运行与实测数据

修正前的测试固件曾启动到 `NuttShell (NSH) NuttX-3.6.1`。修正后的测试将通过
公共 ABI 验证几何信息、RAM 代码 coherent、runtime toggle、range/partial ownership
和 non-cacheable stack 上的整 D-cache 操作。地址域、溢出与超大 range 分块属于静态
源码审查边界，不再通过 chip 私有 hook 和 hardware bypass 自证。

2026-09-02 对原实现复审时发现测试 hook 污染 chip adapter，原三态裁剪只能证明产品
制品未带入 hook，不能证明源码分层正确。后续修正删除了 chip 下全部 cache test 内容，
实测结果以对应修正提交和 Jira `VELABL616-174` 的最新验收记录为准。

详细逐 case 流程和运行时关键输出见
`apps/os_feature_tests/cache/README.md`。

## 限制与恢复入口

- cache 容量使用重新确认的芯片规格。后续芯片 revision 或 cache 配置变化时，应先
  更新权威规格和 getter，再重新审查依赖该容量的调用方。
- DMA0 公共 mem2mem adapter 已接入；外设 consumer 尚未接入。具体 consumer 的
  buffer policy、descriptor cache 属性、request owner 和并发取消仍需独立适配与验证。
- pure D-cache invalidate-all 前必须先处理当前执行栈的 dirty ownership；测试 trampoline
  位于 XIP，切换到 `.nocache_noinit_ram` 中的专用栈后再调用公共接口。
- range API 不宣称对 MMIO、non-cacheable alias 或跨 RAM/XIP 区间生效；调用方必须先
  选择正确地址域。
