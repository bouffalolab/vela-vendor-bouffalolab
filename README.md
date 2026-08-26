# Bouffalo Lab Vendor (vela)

Bouffalo Lab 芯片原厂维护的、基于 **openvela** 的适配层：芯片移植 + 板级 +
驱动 + 中间件 + 示例。对外镜像为 `github/bouffalolab/vendor_bouffalolab`，
通过 BL Vela SDK 的 repo manifest（`vendor/bouffalolab`，remote `bouffalo`，
默认 revision `trunk`）接入整树。

当前脚手架：chip = `bl616cl`，board = `ai-m64l-32s-kit`。

## 目录布局

| 目录 | 作用 | 接入构建的方式 |
|---|---|---|
| `chips/` | 芯片移植（custom chip） | kernel/arch 侧按 defconfig 的 `CONFIG_ARCH_CHIP_CUSTOM_DIR` 纳入 |
| `boards/` | 板级（custom board） | kernel/arch 侧按 defconfig 的 `CONFIG_ARCH_BOARD_CUSTOM_DIR` 纳入 |
| `drivers/` | Bouffalo drivers release 独立仓，驱动各自生成 `.a` | 父仓 OpenVela wrapper 显式选择支持的源码；当前为 `bl616cl_lhal.cmake` |
| `components/` | 中间件/可复用组件，**各自独立 `.a`** | 顶层 `nuttx_add_subdirectory()` 自动发现 |
| `examples/` | 示例 app（`nuttx_add_application`） | 顶层 `nuttx_add_subdirectory()` 自动发现 |
| `tools/` | 宿主侧工具（镜像/签名/FlashCube），**不编入固件** | 故意无 `CMakeLists.txt` → 不纳入构建 |

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
./bl_build.py clean bl616cl/ai-m64l-32s-kit/configs/nsh
./bl_build.py build bl616cl/ai-m64l-32s-kit/configs/nsh -j8
```

SDK 根目录的 `bl_build.py` 是 repo manifest 的 linkfile 软链接（指向本仓
`vendor/bouffalolab/bl_build.py`），在根目录直接执行即可；也可以显式运行
`python3 vendor/bouffalolab/bl_build.py`。

target 指向到 `configs/<name>` 的 board 目录；`vendor/bouffalolab/boards/` 前缀可省略，
无歧义时 chip、`configs/` 层也可省略（`ai-m64l-32s-kit/nsh`、`nsh` 均可用）。
`bl_build.py` 补齐 OpenVela 预置工具链和 Python 依赖环境，只走 CMake/Ninja。
默认并行度 = 物理核数一半，可用 `-j N` / `--jobs N` 覆盖。

配置菜单（任选其一）：

```bash
./bl_build.py menuconfig bl616cl/ai-m64l-32s-kit/configs/nsh
```

菜单中可见 `Bouffalo Lab`（→ Examples / Components）。

Ai-M64L-32S-Kit 的默认构建还会运行仓内官方 `bflb_fw_post_proc`，产出：

- `cmake_out/ai-m64l-32s-kit_nsh/final_nuttx`：静态分析用 ELF；
- `cmake_out/ai-m64l-32s-kit_nsh/nuttx.raw.bin`：处理前备份；
- `cmake_out/ai-m64l-32s-kit_nsh/nuttx.bin`：boot2 可加载的应用镜像。
- `cmake_out/ai-m64l-32s-kit_nsh/nuttx.whole.bin`：包含 boot2、双 partition、
  app 的 4 MiB whole image，可从 flash `0x0` 写入；MFG 分区保持擦除态。
- `cmake_out/ai-m64l-32s-kit_nsh/partition.bin`：分区表（编译时由 `bflb_fw_post_proc`
  生成一次并落盘，供按分区烧录）。
- `cmake_out/ai-m64l-32s-kit_nsh/flash_prog_cfg.ini`：按分区烧录的 FlashCube 配置
  （boot2 / partition / app 三段，均指向绝对路径）。

烧录 firmware（纯烧录操作，不执行 CMake configure/build）：

```bash
# 指定 board：按构建时生成的 flash_prog_cfg.ini 分区烧录（推荐）
./bl_build.py flash bl616cl/ai-m64l-32s-kit/configs/nsh --port /dev/ttyUSB0
./bl_build.py flash nsh --port /dev/ttyUSB0       # board 可简写
./bl_build.py flash --port /dev/ttyUSB0           # 唯一输出目录时自动定位

# 显式指定 FlashCube 配置 ini（与 board/--image 互斥）
./bl_build.py flash --config <ini> --port /dev/ttyUSB0

# 直接烧单个 bin 到指定地址（--addr 仅与 --image 搭配，默认 0x0）
./bl_build.py flash --image <boot2>.bin --addr 0x0 --port /dev/ttyUSB0
./bl_build.py flash --image cmake_out/ai-m64l-32s-kit_nsh/nuttx.bin \
  --addr 0x10000 --port /dev/ttyUSB0
```

`--image` 文件为 4 MiB 时视为 whole image，仍执行魔数/MFG 布局校验。
默认 baudrate 为 2000000，可通过 `--baudrate` 覆盖。烧录成功后 FlashCube
使用 `--reset`，通过板载 DTR/RTS 自动下载电路复位到正常启动状态。

shell 补全（bash/zsh/fish）：`./bl_build.py completion <shell>` 输出补全脚本，
安装方式见脚本内的注释。

## 现状

`bl616cl/ai-m64l-32s-kit` 已具备 RISC-V/E907 reset、cache/RAM section、UART0、
MTimer、NuttX IRQ adapter、watchdog、GPIO、timer、oneshot、基础 board late
bring-up 和 boot2 应用镜像构建链。CMake postbuild 已生成并静态验证 4 MiB
whole image；clean CMake/Ninja 构建、FlashCube 烧录、2 Mbps NSH 启动、MCU
外设测试和 ostest 已在 Ai-M64L-32S-Kit 实板通过。WiFi/RF、PSRAM、PM 和
flash 高频切换仍属后续阶段。

## License

Apache License 2.0，见 [`LICENSE`](./LICENSE)。
