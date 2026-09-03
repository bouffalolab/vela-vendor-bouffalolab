# BouffaloLab OpenVela 文档

- [BL616CL OpenVela 能力矩阵](bl616cl-openvela-capability-matrix.md)：查询
  BL616CL 当前覆盖、可直接开启、需要适配、延后和不支持的 OS/架构/外设能力。
- [BL616CL 堆分配归属与序号观测](bl616cl-mm-record.md)：配置和验证
  `MM_RECORD_PID`、`MM_RECORD_SEQNO`、`MM_RECORD_STACK`，并覆盖 realloc 失败
  保留调用栈记录、PID/sequence 和释放清除。
- [BL616CL 编译器栈保护与受控负测](bl616cl-stack-canary.md)：配置和验证
  `STACK_CANARIES`、编译器插桩、受控 canary 失败、恢复和外设回归。
- [BL616CL TRNG 随机设备适配与验证](bl616cl-trng.md)：配置和验证
  `/dev/random`、可选硬件 `/dev/urandom`、任意长度读取、基本数据检查、裁剪和
  外设回归。
- [BL616CL RISC-V Lazy FPU 配置与验证](bl616cl-lazy-fpu.md)：配置和验证
  `ARCH_LAZYFPU`、双任务与异步 signal FPU 状态隔离、裁剪、开销和外设回归。
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
- [BL616CL PWM](bl616cl-pwm.md)：说明 PWM controller 与 OpenVela upper-half 的
  最大能力交集、CH3+/GPIO22 owner、频率和极性合同、裁剪及波形验收边界。
- [BL616CL Cache](bl616cl-cache.md)：说明 OpenVela cache 公共 ABI、地址域、partial
  ownership、运行时开关、裁剪和已确认的 cache geometry。
- [BL616CL Wi-Fi STA 移植调研](bl616cl-wifi-sta-porting-research.md)：冻结 wl80211、
  macsw、OpenVela netdev/IOB、shared RAM/cache、ABI 边界和分阶段验收路线。
- [BL616CL DMA0](bl616cl-dma.md)：说明 OpenVela generic DMA adapter、八通道
  mem2mem、共享 IRQ、cache ownership、裁剪、实板验证和后续 consumer 边界。
- [BL616CL 普通 Timer 与 TIMER1 验证](bl616cl-timer.md)：说明 TIMER0/TIMER1
  最大能力交集、oneshot owner 互斥、tick/poll/multi-fd/raw callback 测试、
  五配置裁剪和 USB2 实测边界。
