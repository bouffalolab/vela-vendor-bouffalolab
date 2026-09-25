# BL616CL Wi-Fi 热函数布局与回归

`ld.script` 把 Wi-Fi 热函数按调用顺序排在 XIP `.text` 的开头，让它们集中在
少数 I-cache set 里。wl80211、macsw 和 glue 每次更新都可能改名、内联或新增
热函数，列表就会悄悄失效。本文说明布局的构成、什么时候需要回归，以及用
`tools/bl616cl/perf/` 的工具重新确认收益的步骤。收益数据见
[Wi-Fi STA 移植方案](bl616cl-wifi-sta-porting-solution.md) 12.9 节。

## 布局构成

`boards/bl616cl/ai-m64l-32s-kit/scripts/ld.script` 的 `.text` 段：

```text
.text ALIGN(LOADADDR(.data) + SIZEOF(.data), 32K) :
{
  _stext = ABSOLUTE(.);
  __bl616cl_wifi_hot_start = ABSOLUTE(.);
  /* Wi-Fi task loop */            *(.text.wifi_main .text.wifi_main.*) ...
  /* Wi-Fi TX */                   ...
  /* Wi-Fi TX confirmation */      ...
  /* Wi-Fi RX */                   ...
  __bl616cl_wifi_hot_end = ABSOLUTE(.);
  *(.text .text.*)
  ...
  __bl616cl_text_end = ABSOLUTE(.);
}
```

- `.text` 起点对齐到 32 KiB（I-cache 大小）。RAM 段的 flash 镜像变大或变小
  时，代码所在的 cache set 不跟着移动。
- 列表按名字匹配输入段。LTO 会给局部函数加 `.lto_priv`、`.isra`、
  `.constprop` 后缀，所以每行同时写 `.text.fn` 和 `.text.fn.*`。
- 列表直接写在 `ld.script` 里，不用 `INCLUDE`：NuttX CMake 预处理链接脚本
  时不跟踪被包含的文件，改了也不会重新链接。
- 起点来源是 macsw 的 `macsw_cache_affinity.ld.in`，只保留本构建 LTO 后仍是
  独立函数的名字，再加上 tick 采样在同一路径上看到的任务循环、host port 和
  glue 函数。当前 98 行，约 35 KiB。
- 两个标记符号供工具使用：`layout_check.py` 用它们确认列表位置，
  `layout_ab.sh` 用它们删掉列表或在其后插入填充。

## 构建时检查

打开 `BL_COMPONENT_WL80211` 的配置在链接后运行 `layout_check.py`，目前只有
`wifi`；其他配置没有 Wi-Fi 代码，列表匹配不到任何函数，不做检查。输出一行：

```text
bl616cl layout: 98/98 Wi-Fi hot entries placed at 0x80008000, 34.9 KiB (I-cache 32 KiB)
```

列表中有名字在镜像里找不到时，再输出一行 WARNING 并列出这些名字。构建不会
因此失败；需要在 CI 中拦截时，直接运行脚本并加 `--strict`，有缺失时退出码
为 1。

## 什么时候回归

- wl80211、macsw 或 `chips/bl616cl` 的 Wi-Fi glue 更新之后，
  尤其是 `macsw_cache_affinity.ld.in` 有变化时；
- 构建输出出现 `bl616cl layout: WARNING`；
- 工具链、LTO 或优化选项变化之后；
- `ld.script` 中 `.text` 之前的段或 `.text` 本身的结构有改动之后；
- Wi-Fi 吞吐无故下降，需要确认布局是否仍然有效时。

## 回归步骤

以下命令在仓库根目录执行。AP 使用 WPA2/WPA3 时先
`export WIFI_TEST_PSK=...`；不设置或为空时按 open AP 连接。控制台默认
`/dev/ttyUSB3`，可用 `WIFI_TEST_PORT` 修改。

### 1. 构建并看检查结果

```sh
vendor/bouffalolab/vela build ai-m64l-32s-kit/wifi
```

记下 `bl616cl layout:` 行。有缺失项时，到新版源码中找它们的去向：改名的换成
新名字；被内联的删掉该行（调用者通常已在列表中）；删除的函数直接去掉该行。

### 2. 采样

`wifi` 默认不带 perfmon，先用 `perf_build.sh` 生成带 perfmon 的临时镜像：

```sh
vendor/bouffalolab/tools/bl616cl/perf/perf_build.sh <out>/img
vendor/bouffalolab/vela flash --config <out>/img/flash_prog_cfg.ini \
  --port /dev/ttyUSB3 --baudrate 1000000
python3 vendor/bouffalolab/tools/bl616cl/perf/wifi_bench.py <out>/prof \
  --reps 1 --time 20 --udp-rx 60 --perfmon prof
```

每个场景得到一个 `r0-<case>.perfmon`，里面有 `perfmon prof -r` 的原始
直方图和同一窗口的计数。

### 3. 找候选

```sh
python3 vendor/bouffalolab/tools/bl616cl/perf/perfmon_report.py \
  --map <out>/img/nuttx.map --elf <out>/img/final_nuttx --hot wifi \
  --ld-script vendor/bouffalolab/boards/bl616cl/ai-m64l-32s-kit/scripts/ld.script \
  <out>/prof/*.perfmon
```

输出的最后一部分列出 Wi-Fi 模块中样本不少于忙碌样本 0.1% 的函数，标明
`listed` 或 `NEW`，并给出累计大小。判读要点：

- 一个 32 字节桶会算给它覆盖到的每个函数，所以热函数旁边的小函数也会上榜，
  例如 `dma_push` 旁边的 `rxl_go_to_last_rbd`。决定加入前看函数本身是否在
  热路径上。
- `NEW` 函数按调用路径放进任务循环、TX、TX 确认、RX 四组中的一组，不要
  堆在列表末尾。
- "listed, no samples" 里的中断函数（如 `interrupt0_handler`、
  `txl_transmit_trigger`）不能删：tick 采样打不进中断，
  这些函数的开销体现在 `perfmon stat` 的 IRQ 86 周期上。
- 只删除确认已不存在或不再被调用的函数；样本少不是删除的理由。
- 列表越长，冷代码越多地被挤出 cache。当前约 35 KiB，新增时关注累计
  大小，不要超出太多。

2026-09-25 的采样中，当前列表已包含所有达到阈值的 Wi-Fi 函数，只有
`mm_timer_*`、`rc_*` 等低于 0.1% 的函数没有列入。

### 4. A/B 确认收益

```sh
vendor/bouffalolab/tools/bl616cl/perf/layout_ab.sh -b <out>/ab
```

脚本从 `configs/wifi` 复制一个临时配置，分别构建带列表（`hot`）和删掉列表
（`base`）的镜像，每种再在列表之后插入 0、0x1e0、0x9a0 字节的填充，把其余
代码整体挪位，模拟无关改动。六个镜像逐个烧录，各跑 3×20 秒四个方向，最后
打印每个变体各场景的最小、最大和中位数。脚本运行期间会改写并最终恢复
`ld.script`，不要同时构建其他配置。整个过程约 1 小时。

验收标准：

- 三种填充下，`hot` 的 UDP TX 和 UDP RX 中位数都高于 `base`；
- TCP TX、TCP RX 不低于 `base`（差值在 ±2% 内视为持平）；
- 需要看 I-cache 缺失率时，加 `-p` 让六个镜像都带 perfmon，再在 `hot-0`
  和 `base-0` 上各跑一次 `wifi_bench.py --perfmon stat`，确认 `hot` 的缺失率
  和 IRQ 86 每次的周期数不高于 `base`。带 `-p` 的镜像只和带 `-p` 的比较。

达不到标准时，先检查第 1 步的缺失项和第 3 步的 `NEW` 函数，再重复第 4 步。

### 5. 记录

把 A/B 表和 `layout_check` 行更新到移植方案 12.9 节，数据目录写入证据索引。

## 注意事项

- UDP 吞吐对代码位置很敏感：只去掉一个 NSH 命令就能让 UDP 收发变化 ±25%。
  比较两个镜像时，要么 `.text` 大小完全相同，要么用 `layout_ab.sh` 的填充法
  看多个位置，不能只比一次。
- 以前用开关 NSH `mw` 命令挪动代码，现在不再有效：位移会被 256 字节对齐的
  `up_saveusercontext` 吸收。
- 改了链接脚本用到的 CONFIG（如 `BL616CL_WRAM_SIZE`）后必须
  `vela clean`，因为 `ld.script.tmp` 只依赖脚本源文件。改列表本身不需要。
- I-cache 为 32 KiB（见 [bl616cl-cache.md](bl616cl-cache.md)），相联度没有
  确认。对齐按 cache 总大小取，与相联度无关。

## 工具

| 文件 | 作用 |
| --- | --- |
| `tools/bl616cl/perf/layout_check.py` | 链接后检查列表与 map，构建自动运行 |
| `tools/bl616cl/perf/perf_build.sh` | 生成带 perfmon 的临时 `wifi` 镜像 |
| `tools/bl616cl/perf/wifi_bench.py` | 复位、连接、四方向 iperf，可同时运行 perfmon |
| `tools/bl616cl/perf/perfmon_report.py` | 汇总计数、IRQ 占比、模块/函数样本，`--hot` 列候选 |
| `tools/bl616cl/perf/layout_ab.sh` | `hot`/`base` × 填充的构建和测试 |

板上的 `perfmon` 命令见 [bl616cl-perfmon.md](bl616cl-perfmon.md)。

## 后续

按模块排列 net、memcpy/libc 和调度代码，放在 R2 RX 零拷贝完成并稳定验收
之后；届时同时复查 `chips/bl616cl` 中 `bl616cl_wifi`、`bl616cl_wlan` 的
热函数。
