# Bouffalo Lab Tools

宿主侧工具 / 脚本（烧录、镜像生成、签名、量产等），**不编入固件**。

本目录**故意不放 `CMakeLists.txt`** —— openvela 的 `nuttx_add_subdirectory()` 只发现带
`CMakeLists.txt` 的子目录，所以 tools/ 不会被纳入构建，仅作脚本存放 / 手动调用。

- `bflb_flash_tools/`：独立 project（GitHub `bouffalolab/bflb_flash_tools`，manifest
  以 `clone-depth="1"` 拉取），是 Bouffalo SDK `tools/bflb_tools/` 的只读镜像，随
  Bouffalo SDK 发版自动同步，不接受手工提交。`bflb_fw_post_proc/` 是固件后处理工具；
  `bouffalo_flash_cube/` 是 FlashCube 工具树，含全部平台工具、芯片配置、GUI、文档和
  调试工具；`bouffalo_mfg_tool/` 本 SDK 不使用。
- `bl616cl/postprocess_bl616cl.sh`：Ai-M64P-32S-Kit 的 CMake postbuild
  wrapper；在临时目录生成处理后的 app 和 4 MiB whole image。
- `bl616cl/perf/`：Wi-Fi 性能与热函数布局工具。`layout_check.py` 在 Wi-Fi
  镜像链接后检查 `ld.script` 热函数列表；`perf_build.sh` 生成带 `perfmon` 的
  临时镜像；`wifi_bench.py` 跑四方向 iperf 并可同时采集板上 `perfmon`；`perfmon_report.py` 汇总计数和热点；
  `layout_ab.sh` 做布局 A/B。用法见 `docs/bl616cl-hot-code-layout.md`。
- `vendor/bouffalolab/apps/os_feature_tests/ubsan/ubsan_validate.py`：在单一
  USB-UART fd 中完成 BL616CL UBSAN
  recover 报告核对、正式产品裁剪和现役外设回归。

编译和 UART 烧录统一通过仓库根目录的 `vela` 执行；它调用的固件后处理和
FlashCube 运行资产来自 `bflb_flash_tools/`。
