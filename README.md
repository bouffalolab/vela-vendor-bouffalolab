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
| `drivers/` | 驱动，**各自独立 `.a`** | 顶层 `nuttx_add_subdirectory()` 自动发现 |
| `components/` | 中间件/可复用组件，**各自独立 `.a`** | 顶层 `nuttx_add_subdirectory()` 自动发现 |
| `examples/` | 示例 app（`nuttx_add_application`） | 顶层 `nuttx_add_subdirectory()` 自动发现 |
| `tools/` | 宿主侧脚本（烧录/镜像/签名），**不编入固件** | 故意无 `CMakeLists.txt` → 不纳入构建 |

各子目录的 `README.md` 给了"如何新增一项"的可抄骨架。

## 接入 openvela 的两条路径

1. **chips/ + boards/**：不被顶层 glob，而是由 kernel/arch 侧根据 defconfig 里的
   `CONFIG_ARCH_CHIP_CUSTOM_DIR` / `CONFIG_ARCH_BOARD_CUSTOM_DIR` 显式
   `add_subdirectory` 进来——只拉点名的那一个目录。

2. **drivers/ + examples/ + components/**：由本仓顶层 `CMakeLists.txt` 的
   `nuttx_add_subdirectory()` 发现。它只 glob **一层** `*/CMakeLists.txt`、非递归、
   逐层 opt-in，并生成 Kconfig 菜单：

   ```
   vendor → Bouffalo Lab → { Bouffalo Drivers / Examples / Components }
   ```

   > 因为非递归，把独立 git 仓（如复用 bouffalo_sdk 的 `lhal`）以"外层包装 + 内层独立仓"
   > 方式导入时，内仓自带的 CMakeLists 不会被自动读取。详见 `components/README.md`。

## 构建

构建走 **cmake + Ninja**（不再用 make，本仓不提供 `Make.defs`/`Makefile`）。

```bash
# 编译（--cmake 切换到 CMake，并默认启用 -GNinja；--dis-ninja 可关闭）
./build.sh vendor/bouffalolab/boards/bl616cl/bl616cldg/configs/nsh --cmake -j8
```

> 注意：target 用**到 `configs/<name>` 的全路径**（vendor 自定义板），不是 in-tree 短名的
> `board:config` 冒号式；缺省不加 `--cmake` 时 build.sh 会走 Make。

配置菜单（任选其一）：

```bash
./build.sh vendor/bouffalolab/boards/bl616cl/bl616cldg/configs/nsh --cmake menuconfig
# 或直接对 cmake 构建目录操作（目录名 = cmake_out/<board>_<config>）
cmake --build cmake_out/bl616cldg_nsh -t menuconfig
```

菜单中可见 `Bouffalo Lab`（→ Drivers / Examples / Components）。

## 现状

`bl616cl/bl616cldg` 为**初始脚手架**：`nsh/defconfig` 当前是 ARM 占位（来自模板），
真实 BL616（RISC-V / E907）移植，以及 lhal / wireless / phyrf 的接入为后续工作。

## License

Apache License 2.0，见 [`LICENSE`](./LICENSE)。
