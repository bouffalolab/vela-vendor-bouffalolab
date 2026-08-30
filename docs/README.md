# BouffaloLab OpenVela 文档

- [BL616CL OpenVela 能力矩阵](bl616cl-openvela-capability-matrix.md)：查询
  BL616CL 当前覆盖、可直接开启、需要适配、延后和不支持的 OS/架构/外设能力。
- [BL616CL 堆分配归属与序号观测](bl616cl-mm-record.md)：配置和验证
  `MM_RECORD_PID`、`MM_RECORD_SEQNO`，并按 PID 和分配窗口定位未释放块。
- [BL616CL 编译器栈保护与受控负测](bl616cl-stack-canary.md)：配置和验证
  `STACK_CANARIES`、编译器插桩、受控 canary 失败、恢复和外设回归。
- [BL616CL TRNG 随机设备适配与验证](bl616cl-trng.md)：配置和验证
  `/dev/random`、可选硬件 `/dev/urandom`、任意长度读取、基本数据检查、裁剪和
  外设回归。
- [BL616CL Syslog Coredump 配置与离线解码](bl616cl-syslog-coredump.md)：配置和
  验证单线程 coredump、受控 kernel panic、完整串口抓取、ELF core 和 GDB 回溯。
- [BL616CL Note RAM Trace](bl616cl-noteram-trace.md)：配置和验证 task/IRQ trace、
  过滤、overflow、裁剪和外设回归。
- [BL616CL Generic Heap KASAN](bl616cl-kasan.md)：配置和验证 heap 左右越界、
  use-after-free、启动边界、裁剪和外设回归。
- [BL616CL UBSAN](bl616cl-ubsan.md)：配置和验证局部未定义行为插桩、runtime
  闭包、裁剪和外设回归。
- [BL616CL SPI0/SPI1 Master](bl616cl-spi.md)：说明 polling master 最大能力交集、
  双实例裁剪、GPIO CS、软件合同、构建证据和实物环回验收边界。
