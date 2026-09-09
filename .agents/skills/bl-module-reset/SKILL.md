---
name: bl-module-reset
description: 通过 USB-UART 的 DTR/RTS 控制 BouffaloLab 模组复位并从 Flash 正常启动，同时在指定固件波特率验证启动串口日志。用于烧录后启动模组、重新抓取启动日志，或在后续硬件测试前确认模组已正常运行；仅支持 Linux。
---

# BL Module Reset

使用随 skill 提供的脚本执行固定复位时序，不要手工重写 DTR/RTS 控制逻辑。

## 执行

先确定以下必填参数：

- `--port`：目标串口。根据用户输入或 USB 设备枚举确定，不要由脚本自动选择。
- `--baudrate`：固件日志波特率。可以从用户输入、defconfig、构建日志或可靠的当前上下文推断；信息冲突时先询问用户，不要探测多个波特率。

运行：

```bash
python3 SKILL_DIR/scripts/bl_module_reset.py \
  --port /dev/ttyUSB2 \
  --baudrate 2000000 \
  --expect "NuttShell (NSH)" \
  --expect "nsh>"
```

`SKILL_DIR` 是本文件所在目录的绝对路径。`--expect` 可省略或重复：省略时以收到可读启动日志为成功；指定时必须匹配全部标志。

## 规则

- 只执行正常启动复位，不进入 UART 下载模式，也不构建或烧录固件。
- 确认没有串口终端占用目标端口后再运行脚本。
- 对 USB ID `42bf:b210` 使用 CKLink 控制字符串；其他或无法识别的适配器使用标准 DTR/RTS modem-control。
- 固定执行 `DTR up`、等待 50 ms、`RTS up`、等待 50 ms、`RTS down`、等待 100 ms，然后抓取日志。
- 不自动切换或探测波特率。错误波特率下的乱码不能作为其他波特率的成功证据。
- 不把串口输出保存到文件；直接查看终端过程日志和最后一行 `BL_MODULE_RESET_RESULT=<json>`。
- 退出码 `0` 表示成功，`1` 表示参数、设备、串口或控制线失败，`2` 表示没有可验证日志，`3` 表示日志缺少指定的 `--expect` 标志。
- 脚本失败时停止后续串口测试，并向用户报告最后的 JSON 结果。

