# Bouffalo Lab Tools

宿主侧工具 / 脚本（烧录、镜像生成、签名、量产等），**不编入固件**。

本目录**故意不放 `CMakeLists.txt`** —— openvela 的 `nuttx_add_subdirectory()` 只发现带
`CMakeLists.txt` 的子目录，所以 tools/ 不会被纳入构建，仅作脚本存放 / 手动调用。

- `bflb_fw_post_proc/`：从当前 Bouffalo SDK 同步的 Linux、macOS、Windows
  官方固件后处理工具及来源校验信息。
- `bouffalo_flash_cube/`：从 Bouffalo SDK commit `5c976f45d2632043cc160c8659775e021652b79f`
  中同步 FlashCube 工具树；对应 FlashCube commit
  `11d409a9ff451ba3d8658d4ed4e6f3b9da587193`，包含全部平台工具、芯片配置、
  GUI、文档和调试工具，不跟踪上游 `*.bin`、`*.ini` 烧录产物。
- `ai-m64l-32s-kit/postprocess_bl616cl.sh`：Ai-M64L-32S-Kit 的 CMake postbuild
  wrapper；在临时目录生成处理后的 app 和 4 MiB whole image。
- `ai-m64l-32s-kit/kasan_validate.py`：在单一 USB-UART fd 中完成 BL616CL KASAN
  报告核对、warm reset 和现役外设回归。

编译和 UART 烧录统一通过仓库根目录的 `bl_build.py` 执行；本目录只保留它调用的
固件后处理和 FlashCube 运行资产。
