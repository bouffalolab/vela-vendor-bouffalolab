# bflb_fw_post_proc

本目录包含 BL616CL 固件构建所需的 Bouffalo SDK 官方后处理工具，支持
Linux、macOS 和 Windows。构建系统根据宿主平台选择对应的原始文件名，
不依赖仓库外部的 Bouffalo SDK。

## 来源

- SDK 仓库：Bouffalo SDK
- SDK 版本：`2.3.28-25-g5c976f45`
- SDK commit：`5c976f45d2632043cc160c8659775e021652b79f`
- 原始路径：`tools/bflb_tools/bflb_fw_post_proc/`
- 同步日期：2026-07-10

SDK 工作区包含无关的子模块修改；本目录中的文件按以下 SHA-256 校验，
与上述 commit 工作树里的官方工具逐字节一致：

```text
5ce0041c9be104f437f5bb1af2a38c09764ca5d6c0a303d28908cbc1af5cfc81  bflb_fw_post_proc-macos
8cfa7d949567f871e2ffc26ce674f774eb02eabbea873265c31e5fb228835b54  bflb_fw_post_proc-ubuntu
19990b25f3699b2095ce61446c5ce0846c28065e15a05e7dbd825eb00f70ce40  bflb_fw_post_proc.exe
e960112c7b5802ccb58231cc21ab221d07cd02e5078429927b129e507be2f213  usage.txt
5c5bbd78ec82cea199d895abfde383ca9744d4e25fcd964b7b20bab5c6add6dd  version_info.txt
```

工具的上游版本信息和完整参数说明分别见 `version_info.txt` 和
`usage.txt`。
