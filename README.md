# Bouffalo Lab Vendor（openvela）

Bouffalo Lab 维护的 openvela 适配层，是 BL Vela SDK 中的 `vendor/bouffalolab`：
芯片移植、板级、驱动 wrapper、无线组件、测试应用和宿主工具。本仓不能单独构建，
需要用 repo manifest 拉取整个 SDK（openvela 基座 + 本仓 + 驱动等仓）。

当前支持芯片 BL616CL（RISC-V E907），开发板为安信可 Ai-M64L-32S-Kit。

## 获取代码

需要 Linux x86-64 主机，并预先安装 `repo` 和 `git-lfs`（安装后执行一次
`git lfs install`）。

```bash
mkdir bl_vela_sdk && cd bl_vela_sdk
repo init -u https://github.com/bouffalolab/vela-manifest.git \
          -b main -m manifests/bl-vela-sdk-release.xml
repo sync -j8
git -C vendor/bouffalolab lfs pull bouffalo
```

`repo sync` 不会下载 LFS 对象。不执行最后一步时，固件后处理和烧录工具仍是 LFS
指针文件，构建会在后处理阶段失败。对外清单跟踪各仓的开发分支，暂未提供固定版本。

有内部源码权限的开发者使用 `manifests/bl-vela-sdk.xml`，Wi-Fi core 从源码构建；
对外清单使用本仓中的预编译包，见下文“Wi-Fi 与 BLE”。

## 构建与烧录

以下命令都在 SDK 根目录执行。根目录的 `vela` 是 manifest 创建的软链接，指向本仓
`vela`，它补齐 openvela 预置的工具链、CMake、Ninja 和 Python 依赖，只走
CMake + Ninja（本仓不提供 `Make.defs`/`Makefile`）。

```bash
./vela build ai-m64l-32s-kit/wifi              # configure + 编译
./vela flash ai-m64l-32s-kit/wifi --port /dev/ttyUSB0
./vela menuconfig ai-m64l-32s-kit/wifi         # 配置有变化时回写板级 defconfig
./vela clean ai-m64l-32s-kit/wifi              # 删除 cmake_out/ai-m64l-32s-kit_wifi
./vela build --list                            # 列出可用的板级配置
```

目标写到 `configs/<name>` 一层；`vendor/bouffalolab/boards/` 前缀可省略，无歧义时
chip 和 `configs/` 层也可省略（`bl616cl/ai-m64l-32s-kit/configs/nsh`、
`ai-m64l-32s-kit/nsh`、`nsh` 等价）。默认并行度为逻辑核数的一半，可用 `-j N` 覆盖。

Ai-M64L-32S-Kit 的配置：

| 配置 | 内容 |
|---|---|
| `nsh` | 基础控制台 |
| `nsh-peripherals` | 外设测试、perfmon、KASAN、stack canary |
| `ostest` | NuttX OS 测试 |
| `wifi` | Wi-Fi STA |
| `ble` | BLE（controller 库 + zblue host）和 `mible` 测试命令，不开 Wi-Fi |

构建产物在 `cmake_out/<board>_<config>/`：

- `final_nuttx`：最终 ELF，GDB、coredump 和符号分析用它；
- `nuttx.bin`：boot2 可加载的应用镜像，`nuttx.raw.bin` 是后处理前的备份；
- `nuttx.whole.bin`：4 MiB whole image（boot2、双 partition、app），可从 flash
  `0x0` 写入，MFG 等数据分区保持擦除态；
- `partition.bin`：分区表；
- `flash_prog_cfg.ini`：按分区烧录的 FlashCube 配置。

烧录只调用 FlashCube，不编译：

```bash
./vela flash ai-m64l-32s-kit/nsh --port /dev/ttyUSB0           # 按 flash_prog_cfg.ini 分区烧录
./vela flash --config <ini> --port /dev/ttyUSB0                 # 指定 FlashCube 配置
./vela flash --image <bin> --addr 0x0 --port /dev/ttyUSB0       # 单个 bin 写到指定地址
```

默认波特率 2000000，可用 `--baudrate` 覆盖；烧录后通过板载 DTR/RTS 自动复位运行。
`--image` 按地址原样写入，不检查镜像内容。

控制台为 UART0，2000000 bps。Ai-M64L-32S-Kit 的 DTR/RTS 接在 boot/chipen 上，打开
串口会让模组重启一次；picocom 要加 `--lower-rts`，否则模组保持在复位状态。

shell 补全：`./vela completion install`，或 `./vela completion <bash|zsh|fish>` 打印脚本。

## 目录布局

| 目录 | 作用 | 接入构建的方式 |
|---|---|---|
| `chips/` | 芯片移植（custom chip，当前 `bl616cl`） | 按 defconfig 的 `CONFIG_ARCH_CHIP_CUSTOM_DIR` 纳入 |
| `boards/` | 板级（`bl616cl/ai-m64l-32s-kit`，`bl616cl/common` 为共用启动代码） | 按 defconfig 的 `CONFIG_ARCH_BOARD_CUSTOM_DIR` 纳入 |
| `drivers/` | Bouffalo SDK `drivers/` 的只读镜像（lhal、soc、rfparam、预编译 phyrf），独立仓 | `cmake/bl616cl_lhal.cmake`、`bl616cl_std.cmake` 显式选择源码 |
| `components/` | 中间件，当前为 `wireless/`（`wifi`、`ble`、`rfparam`） | 自动发现 |
| `apps/` | 测试与示例 app：`mcu_peripheral_tests`、`os_feature_tests`、`perf_tools`、`wireless_tests` | 自动发现 |
| `cmake/` | 构建辅助（驱动 wrapper、组件 helper、Wi-Fi 源码/预编译选择） | 顶层 `CMakeLists.txt` include |
| `docs/` | BL616CL 功能文档 | — |
| `tools/` | 宿主侧工具（固件后处理、FlashCube、性能工具），不编入固件 | 无 `CMakeLists.txt` |

## 接入 openvela 的三条路径

1. **chips/ + boards/**：不被顶层 glob，由 kernel/arch 侧根据 defconfig 里的
   `CONFIG_ARCH_CHIP_CUSTOM_DIR` / `CONFIG_ARCH_BOARD_CUSTOM_DIR` 显式
   `add_subdirectory`，只拉点名的那一个目录。

2. **apps/ + components/**：由本仓顶层 `CMakeLists.txt` 的
   `nuttx_add_subdirectory()` 发现，两者再各自 `nuttx_add_subdirectory()`。每层只 glob
   直接子目录的 `*/CMakeLists.txt`、非递归、逐层 opt-in，并生成 Kconfig 菜单
   `Bouffalo Lab` → `Bouffalo Apps` / `Bouffalo Components`。新增组件的骨架见
   [`components/README.md`](components/README.md)。

3. **drivers/**：drivers 仓的 CMake 面向 Bouffalo SDK，openvela 不执行这些文件。
   本仓通过 `cmake/*.cmake` 显式选择已适配源码，并用 `nuttx_add_kernel_library()`
   生成 `bl_lhal`、`bl_std` 等库；IRQ、security mutex 等 OS 相关接口由 chip 适配层
   提供。

## Wi-Fi 与 BLE

- **Wi-Fi STA**（`wifi` 配置）：wl80211 + macsw + `bl_wpa_supplicant`，通过 NuttX
  netdev 和 `wapi`/`ifconfig` 使用。对外清单不含 macsw、wl80211 源码，这两个 core 使用
  `components/wireless/wifi/{macsw,wl80211}/prebuilt/` 中的预编译包（openvela
  GCC 13.4 编译的 fat LTO 库，带 LTO 早期调试信息，`final_nuttx` 中有 core 的行号）。
  见 [Wi-Fi STA 移植方案](docs/bl616cl-wifi-sta-porting-solution.md) 4.5 节。
- **BLE**（`ble` 配置）：controller 为 Bouffalo SDK 的预编译库，host 为 zblue。见
  [BLE 文档](docs/bl616cl-ble.md)。

## 现状

已在 Ai-M64L-32S-Kit 实板验证：启动与 boot2 镜像、UART、GPIO、Timer/oneshot、WDT、
RTC、TRNG、DMA0、cache、Wi-Fi STA、BLE，以及 coredump、KASAN、UBSAN、stack canary、
Note RAM trace、perfmon 等调试能力。I2C、SPI、PWM 已通过软件验证，实物验证待补；
PSRAM 第二 heap、PM/低功耗、SPI flash MTD、ADC、加解密引擎尚未适配。

完整列表见 [BL616CL OpenVela 能力矩阵](docs/bl616cl-openvela-capability-matrix.md)，
全部文档见 [`docs/README.md`](docs/README.md)。

## License

Apache License 2.0，见 [`LICENSE`](./LICENSE)。
