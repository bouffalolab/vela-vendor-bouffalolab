# Bouffalo Lab Tools

宿主侧工具 / 脚本（烧录、镜像生成、签名、量产等），**不编入固件**。

本目录**故意不放 `CMakeLists.txt`** —— openvela 的 `nuttx_add_subdirectory()` 只发现带
`CMakeLists.txt` 的子目录，所以 tools/ 不会被纳入构建，仅作脚本存放 / 手动调用。

- `bflb_fw_post_proc/`：从当前 Bouffalo SDK 同步的 Linux、macOS、Windows
  官方固件后处理工具及来源校验信息。
- `bl616cldg/postprocess_bl616cl.py`：BL616CLDG 的 CMake postbuild wrapper；
  保留 raw 镜像，在临时目录运行官方工具，只回写 boot2 应用镜像。
