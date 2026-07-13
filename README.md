# Bouffalo Lab Vendor (vela)

Bouffalo Lab 芯片原厂维护的、基于 **openvela** 的适配层：芯片移植 + 板级 +
驱动 + 中间件 + 示例。对外镜像为 `github/bouffalolab/vendor_bouffalolab`，
通过 BL Vela SDK 的 repo manifest（`vendor/bouffalolab`，remote `bouffalo`，
默认 revision `trunk`）接入整树。

当前脚手架：chip = `bl616cl`，board = `bl616cldg`。

## 目录布局

| 目录 | 作用 | 接入构建的方式 |
|---|---|---|
| `chips/` | 芯片移植（custom chip） | kernel/arch 侧按 defconfig 的 `CONFIG_ARCH_CHIP_CUSTOM_DIR` 纳入 |
| `boards/` | 板级（custom board） | kernel/arch 侧按 defconfig 的 `CONFIG_ARCH_BOARD_CUSTOM_DIR` 纳入 |
| `drivers/` | Bouffalo drivers release 独立仓，驱动各自生成 `.a` | 父仓 OpenVela wrapper 显式选择支持的源码；当前为 `bl616cl_lhal.cmake` |
| `components/` | 中间件/可复用组件，**各自独立 `.a`** | 顶层 `nuttx_add_subdirectory()` 自动发现 |
| `examples/` | 示例 app（`nuttx_add_application`） | 顶层 `nuttx_add_subdirectory()` 自动发现 |
| `tools/` | 宿主侧脚本（烧录/镜像/签名），**不编入固件** | 故意无 `CMakeLists.txt` → 不纳入构建 |

各子目录的 `README.md` 给了"如何新增一项"的可抄骨架。

## 接入 openvela 的三条路径

1. **chips/ + boards/**：不被顶层 glob，而是由 kernel/arch 侧根据 defconfig 里的
   `CONFIG_ARCH_CHIP_CUSTOM_DIR` / `CONFIG_ARCH_BOARD_CUSTOM_DIR` 显式
   `add_subdirectory` 进来——只拉点名的那一个目录。

2. **examples/ + components/**：由本仓顶层 `CMakeLists.txt` 的
   `nuttx_add_subdirectory()` 发现。它只 glob **一层** `*/CMakeLists.txt`、非递归、
   逐层 opt-in，并生成对应 Kconfig 菜单。

3. **drivers/**：release repo 的 CMake 文件面向 Bouffalo SDK，OpenVela 不执行这些
   文件。父仓通过 `cmake/bl616cl_lhal.cmake` 显式选择已适配源码，并用
   `nuttx_add_kernel_library()` 生成 `bl_lhal`；IRQ、security mutex 等 OS 相关接口由
   chip 适配层提供。

## 构建

构建走 **cmake + Ninja**（不再用 make，本仓不提供 `Make.defs`/`Makefile`）。

```bash
python3 vendor/bouffalolab/bl_build.py \
  vendor/bouffalolab/boards/bl616cl/bl616cldg/configs/nsh --clean
python3 vendor/bouffalolab/bl_build.py \
  vendor/bouffalolab/boards/bl616cl/bl616cldg/configs/nsh -j14
```

target 使用到 `configs/<name>` 的完整路径。`bl_build.py` 补齐 OpenVela
预置工具链和 Python 依赖环境，只走 CMake/Ninja。

配置菜单（任选其一）：

```bash
python3 vendor/bouffalolab/bl_build.py \
  vendor/bouffalolab/boards/bl616cl/bl616cldg/configs/nsh --menuconfig
```

菜单中可见 `Bouffalo Lab`（→ Examples / Components）。

BL616CLDG 的默认构建还会运行仓内官方 `bflb_fw_post_proc`，产出：

- `cmake_out/bl616cldg_nsh/final_nuttx`：静态分析用 ELF；
- `cmake_out/bl616cldg_nsh/nuttx.raw.bin`：处理前备份；
- `cmake_out/bl616cldg_nsh/nuttx.bin`：boot2 可加载的应用镜像。
- `cmake_out/bl616cldg_nsh/nuttx.whole.bin`：包含 boot2、双 partition、
  app 的 4 MiB whole image，可从 flash `0x0` 写入；MFG 分区保持擦除态。

显式烧录 whole image：

```bash
vendor/bouffalolab/tools/bl616cldg/flash_bl616cl.sh \
  --image cmake_out/bl616cldg_nsh/nuttx.whole.bin \
  --port /dev/ttyACM0
```

构建不会隐式执行烧录。该入口默认 baudrate 为 2000000，可通过
`--baudrate` 覆盖。

## 现状

`bl616cl/bl616cldg` 已具备 RISC-V/E907 reset、cache/RAM section、UART0、
MTimer、NuttX IRQ adapter、基础 board late bring-up 和 boot2 应用镜像构建
链。CMake postbuild 已生成并静态验证 4 MiB whole image；当前验证范围是
clean CMake/Ninja 构建、ELF 和镜像布局，尚未烧录验证。WiFi/RF、PSRAM、
PM 和 flash 高频切换仍属后续阶段。

## License

Apache License 2.0，见 [`LICENSE`](./LICENSE)。
