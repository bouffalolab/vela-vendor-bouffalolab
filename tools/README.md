# Bouffalo Lab Tools

宿主侧工具 / 脚本（烧录、镜像生成、签名、量产等），**不编入固件**。

本目录**故意不放 `CMakeLists.txt`** —— openvela 的 `nuttx_add_subdirectory()` 只发现带
`CMakeLists.txt` 的子目录，所以 tools/ 不会被纳入构建，仅作脚本存放 / 手动调用。

- `bflb_fw_post_proc/`：从当前 Bouffalo SDK 同步的 Linux、macOS、Windows
  官方固件后处理工具及来源校验信息。
- `bouffalo_flash_cube/`：BL616CL whole image 拼包和烧录所需的三平台 CLI
  最小运行集。
- `bl616cldg/postprocess_bl616cl.sh`：BL616CLDG 的 CMake postbuild
  wrapper；在临时目录生成处理后的 app 和 4 MiB whole image。
- `bl616cldg/flash_bl616cl.sh`：显式把 `nuttx.whole.bin` 从 flash `0x0`
  写入的 UART 入口；普通构建不会调用。
