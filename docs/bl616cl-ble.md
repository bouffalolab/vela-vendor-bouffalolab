# BL616CL BLE（controller 库 + zblue）

本文说明 BL616CL 如何接入 Bouffalo 预编译的 BLE controller 库，host 使用
openvela 的 zblue，以及 `ble` 配置、`mible` 测试命令和实板对测结果。Wi-Fi 与
BLE 共存、低功耗不在本文范围内。

## 组成

```text
mible 命令 / 应用
  └─ zblue host（apps/external/zblue，线程属于调用 z_sys_init() 的任务）
       └─ h4.c ── /dev/ttyHCI0（uart_bth4）
            └─ chips/bl616cl/bl616cl_ble.c（struct bt_driver_s）
                 └─ on-chip HCI：bt_onchiphci_send() / 接收回调
                      └─ controller 线程 btblecontroller（kthread，优先级 200）
                           └─ BLE/BT/DM 中断
```

| 位置 | 内容 |
|---|---|
| `components/wireless/ble/lib/`、`include/` | controller 库和三个头文件，原样取自 Bouffalo SDK |
| `components/wireless/ble/CMakeLists.txt` | 链接 controller 库与 phyrf 库；补编 zblue 的 `h4.c` |
| `components/wireless/ble/zblue_btsnoop.c` | `h4.c` 需要的 `btsnoop_log_capture()` 空实现（弱符号） |
| `chips/bl616cl/bl616cl_ble.c` | HCI 传输：H4 包与 on-chip HCI 结构体互转，注册 `/dev/ttyHCI0` |
| `chips/bl616cl/bl616cl_ble_port.c` | 库的 OS 适配：线程、队列、延时、内存、打印 |
| `boards/bl616cl/common/src/bl616cl_bringup.c` | 开机时调用 `bl616cl_ble_initialize()` |
| `apps/wireless_tests/mible/` | `mible` 测试命令 |
| `boards/.../configs/ble/defconfig` | 只开 BLE、zblue 和 `mible` 的配置 |

组件开关 `CONFIG_BL_COMPONENT_BLE` 依赖 `UART_BTH4` 和非零 EM，并 select
`BL_COMPONENT_RFPARAM`。

## controller 库

| 项 | 值 |
|---|---|
| 来源 | GitHub bouffalo_sdk v2.3.32 `components/wireless/bluetooth/btblecontroller` |
| 文件 | `libbtblecontroller_bl616cl_m2s1.a`，sha256 `a629cab45a14ce68119e8ca8a3270348a246a5245cb3344f8f526d090ed62936` |
| 版本 | 1.6.199（启动 controller 时打印；v2.3.34 为 1.6.207） |
| 能力 | 仅 BLE，2 条连接，全部角色，EM 16 KiB（`CONFIG_BL616CL_EM_16K`） |
| ABI | rv32imafc_xtheade / ilp32f，与 phyrf 库一致 |

内部 gerrit 的 bouffalo_sdk 只有 controller 源码，没有 BL616CL 预编译库；开发期
直接把公开库提交在 vendor 仓里。

库是任务模式：`btble_controller_init()` 通过 `btblecontroller_task_new()` 创建
线程，线程循环 `queue_recv()` → `hci_send_2_controller()` →
`rwip_schedule()`。OS 相关函数都是弱符号，分在两个成员里：

- `btblecontroller_port_freertos_os.c.obj`：12 个 FreeRTOS 实现（任务、队列、
  延时、中断判断、malloc/free）。`bl616cl_ble_port.c` 全部覆盖，这个成员不会被
  链接；链接 map 中不应出现它。
- `btblecontroller_port.c.obj`：中断注册（经 `bflb_irq_*`）、efuse MAC
  （`mfg_media_read_macaddr_with_lock()`）、mtimer。沿用库内默认实现，只覆盖
  `printf`/`puts`，改走 syslog。

语义按 FreeRTOS：返回 1 表示成功，超时单位为 ms，`0xffffffff` 表示永久等待。
队列由信号量加自旋锁实现，`queue_send_from_isr` 可在中断里调用。

换库时：替换 `lib/` 和 `include/`，用 `nm` 核对弱符号列表是否变化，构建后在
map 中确认 FreeRTOS 成员仍未被链接。

## HCI 传输

`bl616cl_ble.c` 把 zblue 写入 `/dev/ttyHCI0` 的 H4 包拆成 on-chip HCI 结构体：
命令为 `{opcode, params, param_len}`，ACL 为 `{conhdl, pb_bc_flag, len, buffer}`，
与原生 `bl_hci_wrapper.c` 一致；库在 `bt_onchiphci_send()` 返回前复制数据。

controller 的接收回调在 controller 线程中执行，按包类型拼回 H4 事件或 ACL，
经 `bt_netdev_receive()` 放进 uart_bth4 的接收缓冲（4096 B）。缓冲满时广播报告
立即丢弃（每 64 个打印一次告警），避免扫描阻塞 controller；其他包最多等 50 ms。

初始化顺序：open 时先 `btble_controller_init()`，再
`bt_onchiphci_interface_init()`。controller 初始化会清空内核任务表
（`rwip_init → btble_ke_init → btble_ke_task_init` 中的 `memset`），先注册的
on-chip HCI 任务会被清掉，第一个发给 host 的事件就会调度到空任务槽而崩溃。

不开 Wi-Fi 时，`bl616cl_ble_initialize()` 调用 `rfparam_init()` 初始化 RF；开
Wi-Fi 时由 Wi-Fi adapter 完成。

## 线程与优先级

| 线程 | 优先级 | 栈 | 说明 |
|---|---|---|---|
| `btblecontroller` | 200 | 3072 | 内核线程，`CONFIG_BL_COMPONENT_BLE_PRIORITY`/`_STACKSIZE` |
| `sysworkq` | 110 | 4064 | zblue 系统工作队列 |
| `BT Driver /dev/ttyHCI0` | 108 | 3032 | `h4.c` 接收线程 |
| `BT LW WQ` | 10 | 1360 | zblue 长任务工作队列 |
| `mible` | 100 | 4096 | 测试命令，`CONFIG_BL_WIRELESS_TESTS_MIBLE_*` |

controller 线程在每次 BLE 中断后运行链路层调度，阻塞在自己的队列上，所以用独立
线程而不是 `hpwork`：`hpwork` 是共享的中断下半部队列，放一个常驻循环会占住
它。整体顺序为 `hpwork` 224 > `btblecontroller` 200 > `lpwork` 150 >
`wifi_fw` 130 > 应用 100。

## `ble` 配置

```bash
./vela build ai-m64l-32s-kit/ble -j14
```

在 `nsh` 的基础上打开 `BL_COMPONENT_BLE`、`BL616CL_EM_16K`、zblue
（`BT_H4`、`BT_CENTRAL`、`BT_PERIPHERAL`、`BT_GATT_CLIENT`、`BT_MAX_CONN=2`）、
`UART_BTH4`（RX 4096、TX 1024）和 `BL_WIRELESS_TESTS_MIBLE`。另有调试项：
`ARCH_STACKDUMP`、`SCHED_BACKTRACE`、`FRAME_POINTER`、`BOARD_RESET_ON_ASSERT=2`，
zblue 日志只开 error（`BT_DEBUG_LOG_LEVEL=3`）。

zblue 4.0.99 的默认构建只验证过少数配置组合，下面几项是为了能编译、能运行而
设置的，都没有修改 `apps/external/zblue`：

| 配置 | 原因 |
|---|---|
| `BT_HCI_ACL_FLOW_CONTROL` 关 | 打开时 `hci_core.c:337` `bt_hci_host_num_completed_packets` 类型冲突 |
| `BT_SMP`、`BT_PRIVACY` 开 | `id.c:1631` 无条件访问只在 `BT_PRIVACY` 下存在的 `hdev->irk`；`BT_PRIVACY` 位于 `if BT_SMP` 内 |
| `BT_GATT_CACHING` 关 | `gatt.c` 缓存分支（616、793、950、1110 行等）没有随多实例改造更新 |
| `BT_MC_DEVICE_INST` 保持 y | `h4.c` 无条件定义 ttyHCI0、ttyHCI1 两个实例；实例 1 只在 `dev_id` 为 1 时打开 |
| `PTHREAD_MUTEX_TYPES` 开 | `h4.c` 用 `PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP` |
| `BT_SHELL` 关 | 打开时 `CMakeLists.default.txt:808` 在 zblue 目标创建前 `target_link_libraries`，configure 失败 |
| `BL616CL_TRNG`、`MBEDTLS_ENTROPY_HARDWARE_ALT` 开 | SMP 的 ECC 走 mbedtls PSA；没有熵源时 `psa_crypto_init()` 失败，`bt_ecc_init()` 提前返回，ECC work 未初始化，`bt_init()` 中调度到空 handler |
| `BT_GAP_PERIPHERAL_PREF_PARAMS` 关 | 见下节“GATT 句柄对齐” |

`CONFIG_BT_PRIVACY` 使本端作为 central 时用 RPA 发起连接；广播和 peripheral
由 `mible` 指定 `BT_LE_ADV_OPT_USE_IDENTITY`，仍用 public 地址。

tinycrypt 在 configure 时从 GitHub 下载（`apps/crypto/tinycrypt`），首次构建需
联网。

## `mible` 测试命令

zblue 带有 `subsys/bluetooth/host/shell/mible_test.c`（小米 BLE 自动化测试
命令），但默认构建不编它，zblue shell 的根命令表也不含 `mible`。vendor
`apps/wireless_tests/mible/` 把它和一个交互入口一起编成 `mible` 命令：

```text
nsh> mible
mible> init
mible> peripheral on
mible> log_show 5
mible> q
```

- `mible_main.c` 调用一次 `z_sys_init()`，按 `mible_test.c` 的子命令表分派，
  并提供 `BT_SHELL` 关闭时缺少的 `shell_fprintf_*`、`shell_help`、`ctx_shell`。
  zblue 线程属于 `mible` 任务，`q` 退出后需要重启才能再次使用蓝牙。
- `mible_test.c` 仍按 zblue 3.x 的头文件路径书写（`<zephyr.h>`、
  `<bluetooth/...>`）。app 的 CMake 在构建目录生成转发头，指向 4.x 的
  `zephyr/` 路径；API 本身兼容。
- zblue `port/sections/defines.c` 只在 `CONFIG_BT_MIBLE_TEST` 有定义时把 mible
  GATT 服务放进静态服务表，而 zblue 没有定义这个 Kconfig 符号。app 的 Kconfig
  定义了隐藏符号 `BT_MIBLE_TEST`，由 `BL_WIRELESS_TESTS_MIBLE` select；缺了它
  mible 服务不在 GATT 数据库里，`bt_gatt_attr_get_handle()` 返回 0。

### GATT 句柄对齐

`mible_test.c` 用本地 GATT 句柄访问对端：central 按本地句柄写对端的写特征、
订阅对端的 notify。两端 GATT 数据库布局必须相同。miot_test（zblue 2.x）的布局
为 GATT 服务 0x01–0x04、GAP 服务 0x05–0x0b、mible 服务 0x0c–0x11（notify 值
0x0e、CCC 0x0f、写特征值 0x11）。

本端因 `BT_CENTRAL` 与 `BT_PRIVACY` 同开，GAP 服务多出 Central Address
Resolution 特征（2 个属性）。`ble` 配置关闭 `BT_GAP_PERIPHERAL_PREF_PARAMS`
（PPCP 特征，也是 2 个属性），GAP 服务属性数与 zblue 2.x 相同，mible 服务落在
0x0c–0x11。代价是本端作为 peripheral 时不提供首选连接参数，也不自动请求连接
参数更新（zblue 4.x 自动更新只用 PPCP 的值）。对端若是别的 GATT 布局，central
可用 `central_target <addr> <handle>` 指定写句柄，notify 方向仍须布局一致。

### 与 miot_test 的命令对应

对端 miot_test 用 `bts mible <子命令>`，子命令表与本端相同：

| 用例 | 本端 | 对端 |
|---|---|---|
| 本端广播 | `broadcast on` | `bts mible observer on`，`bts mible log_show` |
| 本端扫描 | `observer on`，`log_show` | `bts mible broadcast on` |
| 本端 central | `central on`，`central_throughput 0` | `bts mible peripheral on`，`bts mible peripheral_throughput 0` |
| 本端 peripheral | `peripheral on`，`peripheral_throughput 0` | `bts mible central on`，`bts mible central_throughput 0` |
| 断开重连 | `central_periodic_disconnect 4` 或 `peripheral_periodic_disconnect 4` | 保持 `peripheral on` 或 `central on` |

吞吐由发送端和接收端各自累计 CRC32，`log_show <秒>` 的周期输出带
`[Checksum ...]`；停发后两端字节数和校验和相同，说明数据完整到达。

## 实板结果

2026-10-01，Ai-M64L-32S-Kit（本端，`ble` 配置）对 BL616 miot_test（对端，
zblue 2.x，NuttX-3.6.1），屏蔽箱内，RSSI -11～-18 dBm。脚本经常驻串口集线器
同时操作两端，吞吐每个方向 600 s，每分钟采样一次，重连周期 4 s。

| 用例 | 结果 |
|---|---|
| 本端广播（250 个，20 ms） | 对端 observer found 239 |
| 本端扫描（对端广播 250 个） | found 235 |
| 本端 central → 对端 | 731,519 B，1216 B/s（每分钟 1196～1230），0 断线，CRC 一致 |
| 对端 → 本端 central | 187,549 B，312 B/s（298～320），0 断线，CRC 一致 |
| 本端 peripheral → 对端（notify） | 724,546 B，1204 B/s（1164～1260），0 断线，CRC 一致 |
| 对端 → 本端 peripheral（write） | 205,219 B，341 B/s（334～345），0 断线，CRC 一致 |
| `central_periodic_disconnect 4` | 100 次断开后都重连成功；99 次原因 0x16，1 次 0x08 |
| `peripheral_periodic_disconnect 4` | 100 次断开后都重连成功，原因全部 0x16 |

两个方向速率差 4 倍：对端（zblue 2.x）每包都等发送完成回调才发下一包，本端
zblue 4.x 的回调更早，连接事件里能排多个包。ATT MTU 为 23，每包 19 B。

跑完全部用例后 `btblecontroller` 栈最深 804/3004 B（26.7%）；`q` 退出后堆总量
297,972 B，峰值占用 27,220 B。

### 断开原因 0x08

主动断开的一方偶尔在 4 s（supervision timeout）后才收到断开事件，原因是 0x08
而不是 0x16；被断开的一方都按时报 0x13，随后的重连不受影响。断开方发出的
LL_TERMINATE_IND 已被对方收到，对方随即退出连接，但它回的确认包丢了，断开方
只能等监督超时。

这只在 2M PHY 下出现。zblue 4.x 默认开 `BT_AUTO_PHY_UPDATE`，连接后切到 2M；
原生 SDK 的 host 不切，停在 1M。让对端 central 每 4 s 断开本端一次，统计对端
收到的 0x08：

| 本端固件 | PHY | 0x08 / 断开次数 |
|---|---|---|
| Vela `ble` | 2M | 14 / 100 |
| Vela `ble`，关 `BT_AUTO_PHY_UPDATE` | 1M | 0 / 102 |
| 原生 btble_cli（同一 controller 库，FreeRTOS） | 1M | 0 / 102 |
| 原生 btble_cli，每次连接后 `ble_set_2M_Phy` | 2M | 8 / 101 |
| 同上，再 `ble_set_tx_pwr 00`（本端发射 0 dBm） | 2M | 0 / 102 |

两端 BLE 发射功率都是 13 dBm（rfparam `pwr_ble`），屏蔽箱里相距很近：本端收
对端 -11～-18 dBm，对端收本端 -14～-20 dBm。只把本端降到 0 dBm，0x08 就消失，
说明丢的是“本端 → 对端”这一跳在强信号、2M 下的包。数据包丢了会重传，吞吐和
CRC 看不出来；断开前的最后一个确认没有重传机会，所以表现为 0x08。反方向（本端
central 断开、对端回确认）几轮合计只有 2 / 210。

问题与 zblue 和 Vela 移植无关，原生固件同样出现。两台设备分不清是对端在强信号
下的 2M 接收，还是本端 13 dBm 时的 2M 发射，需要第三台设备或衰减器。

## 开销

`ble` 配置的 `final_nuttx` 为 text 665,954 B、data 12,892 B、bss 39,852 B
（`nsh` 为 178,622/1,780/8,380）。按库统计（text 含只读数据）：

| 部分 | text | bss/data |
|---|---|---|
| controller 库 | 117,079 B | bss 4,931 B，TCM 668 B |
| phyrf、rfparam | 13,179 + 6,004 B | 约 700 B |
| zblue | 83,135 B | bss 13,658 B，data 644 B |
| `h4.c`（两个实例） | 1,711 B | data 8,440 B |
| `mible` | 9,072 B | 约 1,255 B |
| mbedtls（SMP 的 ECC，PSA） | 146,374 B | bss 11,060 B |
| tinycrypt | 4,564 B | — |

mbedtls 最大，来自为绕开 `id.c` 编译问题而打开的 `BT_SMP`。`ALLSYMS`（沿用
`nsh`）另占 117,303 B。EM 16K 从堆中划走。

## 限制

- Wi-Fi 与 BLE 共存、controller 低功耗（`btble_controller_sleep`）未适配。
- ATT MTU 为 23，`mible` 每包 19 B，吞吐低；结果只记录，不作为验收门限。
- 上表 zblue 编译问题属于上游，修复后可去掉对应配置限制。
- 近距离、13 dBm 发射时，2M PHY 下主动断开偶发 0x08（见“断开原因 0x08”），
  原生 SDK 同样存在；本端降到 0 dBm 后不再出现。
