# BL616CLDG firmware assets

本目录的 DTS、partition 和 boot2 来自当前 Bouffalo SDK
`bsp/board/bl616cldk/config/`。

SDK 没有提供 BL616CL 预编译 MFG，因此
`mfg_bl616cl_m0_sdk_5c976f45_autoboot.bin` 从同一 SDK commit
`5c976f45d2632043cc160c8659775e021652b79f` 的
`examples/mfg_test` 构建：

```text
make CHIP=bl616cl BOARD=bl616cldk CPU_ID=m0
```

上游示例使用 Bouffalo 官方 T-Head GCC 10.2.0。构建完成后从 ELF 重新
objcopy 出未经过 postprocessor 的 raw binary，使常规 OpenVela postbuild
可使用当前 board DTS 统一处理 app 和 MFG：

```text
riscv64-unknown-elf-objcopy -O binary \
  mfg_test_bl616cl_m0.elf \
  mfg_bl616cl_m0_sdk_5c976f45_autoboot.bin
```

raw MFG 大小为 186904 字节，SHA-256 为：

```text
2c8d1d3897afcd9c7ddbe3add62fac117bf40d9b0564f3032d45b48b7a3ebf36  mfg_bl616cl_m0_sdk_5c976f45_autoboot.bin
```

该 binary 是独立固件资产，不参与 OpenVela 编译；NuttX 固件仍只使用仓库
当前 GCC 13.4.0。
