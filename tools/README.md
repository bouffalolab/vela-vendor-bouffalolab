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
  镜像链接后检查热函数列表 `bl616cl_wifi_hot.ld`；`perf_build.sh` 生成带 `perfmon` 的
  临时镜像；`wifi_bench.py` 跑四方向 iperf 并可同时采集板上 `perfmon`；`perfmon_report.py` 汇总计数和热点；
  `layout_ab.sh` 做布局 A/B。用法见 `docs/bl616cl-hot-code-layout.md`。
- `vendor/bouffalolab/apps/os_feature_tests/ubsan/ubsan_validate.py`：在单一
  USB-UART fd 中完成 BL616CL UBSAN
  recover 报告核对、正式产品裁剪和现役外设回归。

编译和 UART 烧录统一通过仓库根目录的 `vela` 执行；它调用的固件后处理和
FlashCube 运行资产来自 `bflb_flash_tools/`。

在 SDK 根目录启动 Neovim/clangd 时，可合并现有构建的编译数据库：

```bash
python3 vendor/bouffalolab/tools/merge_compile_commands.py \
  --prefer ai-m64p-32s-kit_wifi
```

脚本扫描 `cmake_out/*/compile_commands.json`，按源文件真实路径去重，生成根目录的
`compile_commands.json`；保留原记录的 `directory` 和编译参数。共享源码优先采用
`--prefer` 指定的构建，其余构建按目录名排序补充缺少的文件；不指定时全部按目录名排序。
输入读取失败时保留旧文件，成功后原子替换；构建更新或清理后需重新运行脚本。
同一源文件的不同宏配置不能同时生效，切换主要开发目标时应更换 `--prefer`。
