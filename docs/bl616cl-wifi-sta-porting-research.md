# BL616CL Wi-Fi STA 移植调研

> 状态：待 review。本文只冻结调研结论和后续实现门禁，不代表 Wi-Fi 已接入、
> 已构建或已通过实板验证。跟踪任务：Jira `VELABL616-179`。

## 1. 目标与边界

首版目标是让 BL616CL 以 STA 模式接入 OpenVela 网络栈，形成以下最小闭环：

```text
Open/WPA2-PSK 关联
  -> IPv4 DHCP
  -> gateway/peer ping
  -> TCP/UDP 双向基本收发
```

本轮调研覆盖：

- OpenVela `net`、netdev lower-half、wireless ioctl、`mm/iob`、WAPI、netinit、
  DHCP 和 DNS；
- Bouffalo `wl80211`、新 `macsw`、`bl_wpa_supplicant`、BL616CL PHYRF、
  LHAL/SOC；
- 启动、IRQ/task、STA 控制面、TX/RX 数据面、buffer ownership、cache、
  shared RAM、manifest/build 和预编译库 ABI；
- 可执行的分阶段路线和首版测试合同。

以下能力不进入首版：AP、P2P、WPS、WPA3、Wi-Fi 低功耗、Wi-Fi/BLE coex、IPv6、
漫游和吞吐优化。它们必须在 STA 基线稳定后独立设计和验收。

## 2. 结论摘要

1. 首选路线是 `wl80211 + 新 macsw + bl_wpa_supplicant + BL616CL PHYRF`。
   旧 `wifi6/FHOST + bl6_macsw` 只作为 OpenVela glue 的行为参考，不能和新栈混编。
2. OpenVela 侧应新增 BL616CL Wi-Fi lower-half，使用
   `netdev_lowerhalf_s + netdev_ops_s + wireless_ops_s`，并以
   `NET_LL_IEEE80211` 注册。不得复制旧 BL616 私有 upper-half 或重复实现 WEXT 分派。
3. Bouffalo 现有 NuttX port 不是可直接编入的完整 adapter：没有注册 OpenVela netdev，
   RX callback 未接通，事件、MAC 读取、WRAM allocator 和 RTOS API 仍有缺口。
4. 当前源码更符合“硬件可见对象放入 uncached Wi-Fi RAM”的设计，但 OpenVela linker
   尚未映射 `SHAREDRAM`/`.wifi_ram*`。cache/shared RAM 与 IOB ownership 是实现前
   必须冻结的 P0 决策，不能依赖 cache 测试 hook 或隐式一致性。
5. 预编译库的 ISA/ABI 标签表面匹配，但 GCC 10.2 与 13.4、LTO、
   `-fshort-enums`、结构体布局和头文件版本仍需通过真实链接和实板运行验证。

## 3. 调研基线

### 3.1 当前 BL Vela SDK

| 项目 | revision |
|---|---|
| `nuttx` | `8db268e69cb84b2fa1005a82145863db15df2176` |
| `apps` | `a65c438bdbb0c74b311235f8940a5ceba2cedc68` |
| `vendor/bouffalolab` | `b5f9c7f43342f11af874bbb97e76c41f86d87132` |
| `vendor/bouffalolab/drivers` | `c7050c9962569db0ee62a5e7f605b07b5b9a6070` |

当前 manifest 只有 OpenVela 基座、vendor 和 drivers，没有独立的 wl80211、macsw、
`bl_wireless` 或 `bl_phyrf` 项目。当前 `nsh` defconfig 也没有 NET、IOB、wireless、
WAPI、DHCP 或 DNS 配置。

### 3.2 Bouffalo SDK 参考源

参考根目录：`/home/miot/Work/bds/code/bouffalo_sdk_release/bouffalo_sdk`。

| 组件 | 冻结 revision / 产物 |
|---|---|
| wl80211 public | `32e4c4d8d7d88eab758f79d3c59e2a6eb55a95c1` |
| wl80211 private | `36f24be5d24b99986555fbc5560ad1981b021cf7` |
| macsw | `444baa94b9698726de84c57dc715e2b9d008eb97` |
| wifi6，仅参考 | `6b8dd7e9f0ca69ca64c5b0192bb46c554702f335` |
| BL616CL PHYRF | `a0b68bbcafdda0936cd5ae0971631d7f66fa1838` |
| BL616CL std | `a9630593c6c771f83b18bdafa288a2112395595c` |
| lhal | `f8aebf60ffdff6ade4c7bd1ac1a11ea2a892b634` |
| sys | `10cb9943ef8f8b5fb7af12c69ea2f6fc743c570a` |
| wl80211 BL616CL archive | SHA256 `9826064a9ff7ab83e5c7e29bcaf2b0f8e9981f65f986fb008b1db30937843cb4` |

`bl_wpa_supplicant` 当前属于 Bouffalo SDK 父项目目录，不是一个独立 manifest project。
实现前必须先确定各组件在 BL Vela SDK 中的正式仓库边界、revision 和许可证交付方式。

## 4. 模块归属

| 模块 | 首版职责 | 不应承担的职责 |
|---|---|---|
| OpenVela netdev upper-half | socket 到驱动轮询、IOB/netpkt、quota、协议分派、carrier | Bouffalo 私有关联状态机 |
| BL616CL Wi-Fi lower-half | netdev 注册、TX/RX 队列、wireless ops、carrier、buffer/cache 合同 | 重写 OpenVela upper-half/WEXT |
| wl80211 | STA API、连接控制、MACSW bridge、数据帧和 supplicant callback | OpenVela socket/IP/DHCP |
| macsw | Wi-Fi MAC 状态机、descriptor、IRQ 内部分派、TX confirm、RX upload | OpenVela netdev lifecycle |
| `bl_wpa_supplicant` | WPA2 四次握手、EAPOL、PTK/GTK 和 controlled port | DHCP、DNS、IP 数据面 |
| BL616CL PHYRF/RF param | RF 初始化、校准参数和射频运行前置 | netdev 和 WPA 状态 |
| board/chip adapter | 时钟、Wi-Fi IRQ、shared RAM/linker、启动顺序 | test hook 或测试专用 ABI |
| WAPI/netinit | WEXT 配置、ifup/关联/DHCP 编排 | WPA2 握手实现 |

## 5. OpenVela 标准接入模型

`nuttx/include/nuttx/net/netdev_lowerhalf.h` 已定义本次需要的公共 ABI：

- `netpkt_t` 是 `struct iob_s`；
- `netdev_lowerhalf_s` 持有 ops、可选 wireless ops、TX/RX quota、RX 执行模式和
  `net_driver_s`；
- `netdev_ops_s` 要求非阻塞的 `transmit()` 和 `receive()`；
- `wireless_ops_s` 已覆盖 connect/disconnect、SSID、BSSID、密码、认证、频率、
  功率、country 和 scan；
- `netdev_lower_register()` 负责连接通用 upper-half。

建议注册路径：

```text
board late bringup
  -> bl616cl_wlan_initialize()
  -> RF / MACSW / wl80211 初始化
  -> 初始化 lower-half state、ops、iw_ops、quota、RX queue
  -> netdev_lower_register(lower, NET_LL_IEEE80211)
  -> wlan0
```

`NET_LL_IEEE80211` 在当前 OpenVela 中使用 Ethernet II header 和 `wlan%d` 命名；
RX 会由通用 upper-half 进入 `eth_input()`，再分派 IPv4、ARP 等协议。参考实现可看
`nuttx/drivers/net/wifi_sim.c`，但其模拟认证状态不能作为真实硬件实现。

初始化应设置 `CONFIG_NETDEV_LATEINIT=y`，由 board late bringup 显式启动。BL616CL Wi-Fi
依赖 RF、IRQ、任务和 shared RAM，不应进入 RISC-V 通用 early net initialize 路径。

## 6. 启动、任务与 IRQ

Bouffalo 参考启动链为：

```text
system clock / Wi-Fi PLL / MAC-PHY clock
  -> rfparam_init()
  -> wifi_task_create()
  -> macsw_platform_init()
  -> macswl_init()
  -> macsw_event_loop()
  -> wl80211_init()
       -> timeout
       -> MAC bridge
       -> network adapter
       -> supplicant callback
  -> wifi_mgmr_init()
```

关键约束：

- MACSW 需要独立、长期运行的事件循环线程。现有 `macsw_plat.c` 是 FreeRTOS task
  adapter，OpenVela 需要明确的 kthread/pthread、优先级、栈、notification 和退出合同；
- BL616CL `WIFI_IRQn = IRQ_NUM_BASE + 54`，当前 OpenVela BL616CL IRQ 头没有 Wi-Fi
  符号，虽然 `NR_IRQS` 范围已覆盖；
- 外部 `interrupt0_handler()` 扫描内部 Wi-Fi INTC，分派 MAC RX/TX、MODEM、RC、
  LLI DMA 等来源，并唤醒 MACSW task；
- ISR 只做硬件确认、入队和唤醒，不能在 IRQ 中调用 OpenVela 协议栈；
- 现有 NuttX event worker 在 `sched_lock()` 状态调用上层 handler，若 handler 触发
  DHCP 或其他阻塞操作会有调度风险，必须改为显式 event queue/work/thread 边界。

建议 board 扩展点为 `boards/bl616cl/common/src/bl616cl_bringup.c` 中的
`bl616cl_board_initialize()`，具体初始化顺序仍需在 G2 固定后再编码。

## 7. STA 控制面

Bouffalo 控制链已经完整存在：

```text
WAPI / application
  -> wireless_ops_s
  -> wifi_mgmr_sta_connect()
  -> wl80211_sta_connect()
  -> wl80211_cntrl(WL80211_CTRL_STA_CONNECT)
  -> create STA VIF
  -> wl80211_mac_do_connect()
  -> directed join scan
  -> select BSS / parse RSN and PMF
  -> SM_CONNECT_REQ
  -> SM_CONNECT_IND
     -> Open: open controlled port
     -> WPA2: start four-way handshake
  -> key install / controlled port confirm
  -> STA connected event
  -> netdev_lower_carrier_on()
```

WPA2 的 EAPOL RX 会从普通 IP 数据面截获并送入 `bl_wpa_supplicant`；PTK/GTK 由
supplicant 计算后写入 MAC key RAM；握手成功后才打开 controlled port。因此：

- `ifup()` 仅表示管理状态 `IFF_UP`；
- Open 网络应等待 controlled port confirm 后 carrier on；
- WPA2 网络应等待四次握手和 controlled port confirm 后 carrier on；
- disconnect、认证失败或 link loss 必须 carrier off，并清理后续 IP 状态。

OpenVela WAPI 只是配置库/命令，不是 WPA supplicant。通用 wireless ioctl 已负责把
ESSID、key、auth、scan 等请求分派到 `wireless_ops_s`，lower-half 不应重复 switch
整套 `SIOCSIW*`。

首轮建议启用 `NETINIT_NETLOCAL=y`，手工验证：

```text
ifup wlan0 -> scan -> connect -> dhcpc renew -> ping/TCP/UDP
```

待时序和错误恢复稳定后，再验证自动路径：

```text
netlib_ifup(wlan0)
  -> netinit_associate(wlan0)
  -> netlib_obtain_ipv4addr(wlan0)
  -> address/netmask/router/DNS
```

## 8. TX/RX 数据面与 ownership

### 8.1 TX

```text
socket/TCP/UDP
  -> devif_poll()
  -> lower->ops->transmit(netpkt)
  -> IOB chain -> iovec / copy
  -> wl80211_mac_tx()
  -> MACSW software queue
  -> hardware TX
  -> TX confirm
  -> netpkt_free(lower, pkt, NETPKT_TX)
  -> netdev_lower_txdone(lower)
```

`transmit()` 成功后 packet ownership 归驱动，失败则仍由 upper-half 回收。TX confirm
不能直接 `iob_free_chain()`，否则 lower-half quota 不会恢复。若 payload 留在 cached RAM，
必须在 descriptor/payload ownership 交给 MAC 前 clean；若复制到 uncached Wi-Fi RAM，
必须证明 copy 的生命周期和容量上界。

### 8.2 RX

```text
MAC IRQ
  -> MACSW RX task / private RX pool
  -> wl80211_tcpip_input()
  -> lower-half RX queue
  -> netdev_lower_rxready()
  -> lower->ops->receive()
  -> netpkt_put()
  -> eth_input()
  -> IPv4 / ARP
  -> socket
```

RX callback 只负责完成 ownership transfer、排队和通知。若 buffer 由 MAC 写入 cached
地址，必须在 MAC 交还 ownership 后、CPU 读取前 invalidate。首版必须在以下两种模式中
选定一种，不能同时保留模糊 ownership：

1. 从 vendor RX pool 复制到 OpenVela IOB，随后立即归还 vendor buffer；
2. 以严格的 external-buffer/zero-copy 合同把 vendor buffer 交给 netpkt，并在协议栈释放
   时唯一地归还 RX pool。

首版优先选择可证明的 copy 路径；zero-copy 只有在回收、对齐、cache、quota 和异常释放
全部可验证后再考虑。

## 9. IOB、shared RAM 与 cache

### 9.1 当前冲突

OpenVela 默认值：

| 配置 | 默认值 | 影响 |
|---|---:|---|
| `IOB_NBUFFERS` | 8 | 不足以直接承接高并发 WLAN TX/RX |
| `IOB_BUFSIZE` | 196 | 1514-byte Ethernet frame 会形成 IOB chain |
| `IOB_ALIGNMENT` | 4 | 不自动满足 Wi-Fi descriptor/cache-line 要求 |
| `NET_ETH_PKTSIZE` | 1514 | 首版 Ethernet II frame 上界 |

当前 wl80211 NuttX port 的 WRAM allocator 把单个 IOB 当作 WRAM object，因此对象不能
超过 `CONFIG_IOB_BUFSIZE`；但 OpenVela 网络帧本来允许使用 IOB chain。实现前必须量化：

- MACSW 能接受的最大 scatter-gather segment 数；
- `NET_AL_TX_HEADROOM` 和 descriptor 私有头实际字节数；
- TX 采用 IOB chain SG、flatten/copy，还是把 `IOB_BUFSIZE` 增至完整帧；
- TX/RX quota、协议栈占用和 IOB 总池的预算；
- RX pool 是否独立于通用 IOB，以及压力下的 backpressure/drop 行为。

### 9.2 shared RAM/cache 决策

Bouffalo MACSW descriptors 和 wl80211 RX pool 显式进入 `SHAREDRAM`；Bouffalo linker
把 `SHAREDRAMIPC`、`SHAREDRAM` 和 Wi-Fi 公共对象放入 `ram_wifi`。源码中未找到这些
ownership transfer 对应的 dcache clean/invalidate。

当前 OpenVela linker 虽声明 64 KiB `ram_wifi`，却没有映射 `SHAREDRAM`、
`SHAREDRAMIPC`、`.wifi_ram*` 或 `.wifibss`，只处理 `.nocache_ram`、
`.nocache_noinit_ram` 和 `.noncacheable`。

G2 必须在以下方案中冻结一个：

| 方案 | 优点 | 风险/验证要求 |
|---|---|---|
| A. descriptor、RX pool、MAC-visible payload 放 uncached Wi-Fi RAM | 符合现有源码假设，ownership 简单 | 64 KiB 容量、link map、IOB/copy 分配、对齐和溢出必须量化 |
| B. 数据保留 cached RAM，在每次 ownership transfer 做 range cache | 节省专用 RAM，可支持通用 IOB | 所有 TX/RX/descriptor 路径必须完整 clean/invalidate，partial line owner 必须唯一 |

调研建议采用 A 作为 descriptor/RX pool 基线，普通 OpenVela IOB 留在系统 RAM；TX 是否
复制到 uncached payload pool，取决于 SG 能力和 64 KiB 预算。不得把整个 IOB pool 放入
Wi-Fi RAM，除非 link map 和其他网络 consumer 的影响已经证明。

## 10. 构建、manifest 与 ABI

wl80211/macsw 参考构建使用：

```text
-march=rv32imafc_xtheade
-mabi=ilp32f
-mtune=e907
-fshort-enums
LTO / fat-LTO
```

PHYRF archive 路径标识 GCC 10.2，当前 OpenVela 工具链为 GCC 13.4。`readelf -A` 只能
证明 wl80211 和 PHYRF archive 的 ISA 字符串与 16-byte stack alignment 表面一致，
不能证明最终 ABI 兼容。

G0/G1 必须完成：

- manifest project、仓库 owner、公开/私有源码、许可证和可交付 archive 的边界；
- 组件 revision 与 header/config profile 一一对应；
- 使用 OpenVela 最终编译选项真实链接，检查 unresolved/duplicate symbol；
- 检查 enum 大小、公共结构体布局、callback prototype、LTO plugin 和 archive member；
- 从 ELF/map 核对 Wi-Fi code/data/bss/shared RAM、IRQ vector、task entry 和 RF 数据 owner；
- 禁止同时链接旧 wifi6/FHOST 与新 wl80211/macsw 的同名 glue 或控制面。

## 11. 当前已确认缺口

### P0：进入实现前必须解决

| 缺口 | 影响 |
|---|---|
| manifest 没有无线组件及冻结 revision | 无法形成可复现依赖闭包 |
| 当前 wl80211 NuttX port 不注册 OpenVela netdev | 没有 `wlan0` 和 socket 数据面 |
| `g_rx_cb` 只有定义/调用，没有现有注册路径 | 普通 IP/ARP RX 可能空指针调用 |
| linker 未映射 Wi-Fi shared sections | descriptor/RX pool 的硬件可见性不成立 |
| 缺 Wi-Fi IRQ symbol、attach/enable 和 task adapter | MACSW 无可靠事件驱动 |
| IOB/SG/headroom/quota 未冻结 | 1500-byte frame 的 TX/RX lifecycle 不成立 |
| cache ownership 未冻结 | 可能出现静默数据损坏 |
| archive/toolchain ABI 未真实验证 | 可能链接失败或运行期结构体错位 |

### P1：最小 STA 闭环所需

| 缺口 | 影响 |
|---|---|
| `wifi_mgmr_init()` 的 NuttX 分支基本为空 | 关联事件和 carrier 转换缺失 |
| event worker 在 `sched_lock()` 下调上层 handler | 阻塞/调度风险 |
| 调用未实现的 `bl616_wifi_event_handler()` | NuttX event adapter 不闭合 |
| MAC 读取仍调用 BL616 命名 efuse API | BL616CL MAC owner 不清晰 |
| `rtos_ms2tick` 声明与 `rtos_al_ms2tick` 实现不一致 | API/link 风险 |
| 缺 unlimited WRAM allocator 实现 | 某些对象分配路径不完整 |
| entropy 直接依赖 Bouffalo TRNG API | 与现有 BL616CL TRNG/OpenVela mbedTLS owner 未收敛 |
| Bouffalo CMake 与 NuttX Makefile 选择不同 adapter 源 | OpenVela CMake source set 未冻结 |
| 当前 defconfig 无 NET/IOB/WAPI/DHCP/DNS | 无法构建首版测试配置 |

## 12. 首版配置基线

以下仅是 G2 配置候选，不应直接手改 autogenerated defconfig：

```text
CONFIG_NET=y
CONFIG_NET_IPv4=y
CONFIG_NET_ETHERNET=y
CONFIG_NET_TCP=y
CONFIG_NET_UDP=y
CONFIG_NET_ARP=y
CONFIG_NET_ICMP=y
CONFIG_MM_IOB=y
CONFIG_NETDEVICES=y
CONFIG_DRIVERS_WIRELESS=y
CONFIG_DRIVERS_IEEE80211=y
CONFIG_NETDEV_WIRELESS_IOCTL=y
CONFIG_NETDEV_WIRELESS_HANDLER=y
CONFIG_NETDEV_LATEINIT=y
CONFIG_WIRELESS_WAPI=y
CONFIG_WIRELESS_WAPI_CMDTOOL=y
CONFIG_NETUTILS_NETINIT=y
CONFIG_NETINIT_NETLOCAL=y
CONFIG_NETUTILS_DHCPC=y
CONFIG_NETDB_DNSCLIENT=y
```

`IOB_BUFSIZE`、`IOB_NBUFFERS`、quota、worker priority/stack 和 Wi-Fi 专用 pool 大小不能
沿用默认值，也不能照抄旧 BL616 配置，必须由 G1 link map 与 G2 压力模型确定。

## 13. 分阶段实施路线

| 阶段 | 目标 | 退出条件 |
|---|---|---|
| G0 交付闭包 | 冻结仓库、revision、许可证、源码/archive owner | manifest 方案可复现，无来源不明二进制 |
| G1 最小链接 | 只解决 component source set、ABI、unresolved symbols 和 link map | 最终 ELF 可链接，section/符号/ABI 证据完整；不宣称可运行 |
| G2 方案冻结 | 冻结 netdev、IOB/cache、IRQ/task、event、MAC/RF/WPA2、Kconfig 和测试合同 | 本文 review 决策项有唯一答案，blocking unknown 清零 |
| G3 adapter 实现 | 实现 OpenVela lower-half 和 BL616CL glue | diff 可追溯，无重复 upper-half，无 test 内容进入 chip driver |
| G4 构建/静态验证 | 产品态/测试态/关闭态构建与 ELF/map 检查 | 配置裁剪、符号、section、archive 和镜像门禁通过 |
| G5 实板 STA | Open/WPA2、DHCP、IPv4 和 TCP/UDP | 下节测试矩阵通过并保留串口/网络原始证据 |

WPA3、PS/coex、AP/P2P 等从 G5 基线另开任务，不扩大首版验收。

## 14. 首版实板测试矩阵

| ID | 场景 | 核心断言 |
|---|---|---|
| STA-01 | 冷启动，无 AP | NSH 存活；Wi-Fi 初始化失败可观测；无 panic/死循环 |
| STA-02 | scan | 能发现指定 2.4 GHz AP；结果字段和重复扫描稳定 |
| STA-03 | Open AP connect/disconnect | controlled port、carrier on/off 顺序正确 |
| STA-04 | WPA2-PSK 正确密码 | 四次握手、key install、carrier on 成功 |
| STA-05 | WPA2-PSK 错误密码 | 明确认证失败；不 carrier on；可重试恢复 |
| STA-06 | DHCP/DNS | 获取 address/netmask/router/DNS；lease renew 正常 |
| STA-07 | IPv4 ping | AP/gateway/peer 双向可达，统计无异常 |
| STA-08 | TCP/UDP 基本收发 | 上下行完整性、断连恢复和 socket 错误正确 |
| STA-09 | AP 丢失/重启 | carrier off、旧 IP 清理、重新连接可控 |
| STA-10 | IOB/内存压力 | quota 可恢复，无 pool 泄漏、越界、死锁或长期 TX stall |
| STA-11 | cache/shared RAM 压力 | 多长度、非对齐、持续双向流量无静默损坏 |
| STA-12 | 裁剪和回归 | Wi-Fi 关闭态无无线 archive/symbol；UART/NSH/既有外设正常 |

运行证据至少包括：固件和配置 identity、ELF/map、串口启动/关联日志、AP 配置、DHCP
结果、ping/TCP/UDP 原始输出、断连恢复和压力前后 IOB/heap/task 状态。构建成功、出现
`wlan0` 或单次 ping 不能替代完整验收。

## 15. Review 决策清单

进入实现前需确认以下问题：

1. 是否接受 `wl80211 + 新 macsw + bl_wpa_supplicant + BL616CL PHYRF` 为唯一主线，
   旧 wifi6/FHOST 仅作参考？
2. 无线组件放入 manifest 的仓库边界、可交付源码/archive 和 revision 如何冻结？
3. descriptor/RX pool 是否固定放 uncached `ram_wifi`？TX payload 采用 SG 还是 copy？
4. 首版 RX 是否先采用 copy-to-IOB，暂不做 zero-copy？
5. IOB size/count、TX/RX quota、Wi-Fi pool、task stack 的定量预算是什么？
6. Wi-Fi IRQ、MACSW task、event worker 和 board late init 的唯一 owner 是谁？
7. MAC 地址、RF param/calibration、TRNG/entropy 的 BL616CL 正式接口是什么？
8. 是否接受首轮 `NETINIT_NETLOCAL=y` 手工编排，稳定后再打开自动关联/DHCP？
9. GCC 10.2 PHYRF archive 是否允许直接进入 GCC 13.4 最终链接；若不允许，谁负责重编？
10. G5 的 AP、信道、密码、安全模式、流量 peer 和长时压力时长由谁固定？

## 16. 主要源码证据

### OpenVela

- `nuttx/include/nuttx/net/netdev_lowerhalf.h:71-231,283-354,582-613`
- `nuttx/drivers/net/netdev_upperhalf.c:245-365,503-752,1393-1539,1606-1708,1824-1874`
- `nuttx/net/netdev/netdev_register.c:326-332`
- `nuttx/drivers/net/wifi_sim.c:335-351,1781-1797`
- `nuttx/mm/iob/Kconfig:8-42`
- `nuttx/net/netdev/Kconfig:53-59`
- `nuttx/drivers/net/Kconfig:25-30`
- `nuttx/drivers/wireless/Kconfig:6-12,89-95`
- `apps/wireless/wapi/src/driver_wext.c:246-330`
- `apps/netutils/netinit/netinit.c:634-667`
- `apps/netutils/netinit/netinit_associate.c:46-74`
- `apps/netutils/netlib/netlib_obtainipv4addr.c:80-154`

### 当前 BL616CL adapter

- `vendor/bouffalolab/chips/bl616cl/include/irq.h:43-60`
- `vendor/bouffalolab/chips/bl616cl/CMakeLists.txt:21-82`
- `vendor/bouffalolab/boards/bl616cl/common/src/bl616cl_bringup.c:70-115`
- `vendor/bouffalolab/boards/bl616cl/ai-m64l-32s-kit/scripts/ld.script:26-34,174-189`
- `vendor/bouffalolab/boards/bl616cl/ai-m64l-32s-kit/configs/nsh/defconfig`

### Bouffalo SDK 参考源

- `bsp/board/bl616cldk/board.c:71-91,411-417`
- `bsp/board/bl616cldk/bl616cl_dv.ld:20-41,340-364`
- `components/wireless/wl80211/src/wl80211.c:19-29`
- `components/wireless/wl80211/wifi_mgmr.c:327-471,889-945`
- `components/wireless/wl80211/src/cntrl.c:17-47,96-146`
- `components/wireless/wl80211/src/macsw/connect.c:284-617`
- `components/wireless/wl80211/src/macsw/macsw.c:197-355`
- `components/wireless/wl80211/src/macsw/tx.c:119-255`
- `components/wireless/wl80211/src/macsw/rx.c:390-673`
- `components/wireless/wl80211/nuttx.c:102-225,459-569,708-784`
- `components/wireless/wl80211/rtos_al_nuttx.c:76-93,476-600`
- `components/wireless/wl80211/supplicant.c:114-350`
- `components/wireless/macsw/modules/macsw_config/src/macsw_config.c:207-238`
- `components/wireless/macsw/plf/refip/src/driver/intc/risc-v/intc.c:94-295`
- `components/wireless/bl_wpa_supplicant/src/bl_supplicant/bl_wpa_main.c:78-106,232-345`

## 17. 当前判定

信息已足够进入方案 review，但还不足以进入编码。最关键的 blocking unknown 是无线组件
交付闭包、shared RAM/cache ownership、IOB/SG/quota 预算和 GCC 10.2/13.4 ABI。
这些问题一旦在 G0-G2 获得唯一答案，后续实现可以按 lower-half、chip/board glue、
构建配置和实板验收四个独立变更面推进。
