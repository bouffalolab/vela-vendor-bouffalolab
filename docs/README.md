# BouffaloLab OpenVela 文档

- [BL616CL OpenVela 能力矩阵](bl616cl-openvela-capability-matrix.md)：查询
  BL616CL 当前覆盖、可直接开启、需要适配、延后和不支持的 OS/架构/外设能力。
- [BL616CL 堆分配归属与序号观测](bl616cl-mm-record.md)：配置和验证
  `MM_RECORD_PID`、`MM_RECORD_SEQNO`，并按 PID 和分配窗口定位未释放块。
- [BL616CL 编译器栈保护与受控负测](bl616cl-stack-canary.md)：配置和验证
  `STACK_CANARIES`、编译器插桩、受控 canary 失败、恢复和外设回归。
