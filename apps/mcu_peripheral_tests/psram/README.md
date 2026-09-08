# BL616CL PSRAM

PSRAM 作为 NuttX default heap 的第二个 region 使用，普通 `malloc()` 可分配。
初始化复用 Bouffalo SDK Winbond x8 控制器驱动，并在 `nx_start()` 前完成。
有出厂 trim 时校验 parity 和窗口；无 trim 时扫描 16 档 DQS，通过 uncached
地址验证 4 MiB 后选取最长连续有效窗口中点。初始化失败时报告错误，继续使用内部 SRAM。

## 构建与运行

```sh
vendor/bouffalolab/vela build ai-m64l-32s-kit/nsh-peripherals -j14
vendor/bouffalolab/vela flash ai-m64l-32s-kit/nsh-peripherals --port /dev/ttyUSB2
```

串口为 2000000 baud；进入 NSH 后执行 `mcu_psram_test`，最终应输出 `PSRAM PASS`。
NSH 可能先返回提示符，再输出异步 app 的结果，验收应等待最终 PASS/FAIL。

`nsh-peripherals` 是含手动测试 app 的配置；基础 `nsh` 默认关闭 PSRAM。
产品配置通过 `vela menuconfig` 设置 `MM_REGIONS >= 2`、`BL616CL_PSRAM=y`，
保留 `BL_MCU_PERIPHERAL_TESTS_PSRAM=n`。配置依赖 flat build、非 `MM_SMALL`、
启用 PMP 初始化和 default heap；cache API 随 PSRAM 自动启用。

## 测试含义

- 从 heap 分配“实际容量与最大空闲块的较小值减 64 KiB”，确认返回地址落在 PSRAM。
  该方式兼容统一配置的 KASAN shadow 和 heap 元数据开销。
- 四轮地址相关模式写入后 clean，通过 uncached alias 核对整个分配区。
- 预热 1 KiB cache 后从 uncached alias 改写；确认 invalidate 前仍读到旧值，
  invalidate 后读到新值，避免仅因 cache 命中而误判。
- 释放内存后可以重复执行，测试不访问未分配区域，不自动在启动脚本中运行。

## 范围与限制

驱动按 ID 识别 4/8/16 MiB，32 MiB 超出当前 PMP 窗口，明确拒绝。
当前实机验证为 Ai-M64L-32S-Kit 的无出厂 trim、4 MiB 模组；其他容量只有代码支持。
启动校准在 heap 注册前改写 PSRAM 前 4 MiB，不支持保留 PSRAM 内容的启动方式。
本次不支持 PSRAM 静态代码/数据段、低功耗保存恢复，也不声称完成温压角落或长时稳定性认证。
控制器源时钟为 WIFIPLL 320 MHz，控制器内部分频不能忽略，不将其称为外部总线频率。
