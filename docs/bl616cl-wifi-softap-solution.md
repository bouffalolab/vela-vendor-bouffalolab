# BL616CL Wi-Fi SoftAP 方案

本文说明 BL616CL 在 OpenVela 上的 SoftAP：结构、控制接口、数据面、配置、用法和已知限制。STA 部分见 [BL616CL Wi-Fi STA 移植方案](bl616cl-wifi-sta-porting-solution.md)，本文沿用其中的组件、预编译包和线程划分。

## 1. 方案结论

- SoftAP 是第二个 netdev `wlan1`，对应 wl80211 的 AP VIF；`wlan0` 仍是 STA。两者用同一个 MAC 地址（与 Bouffalo SDK 默认相同）。
- 能力：open 与 WPA2-PSK（CCMP）；2.4 GHz、20 MHz，信道取当前国家码的信道表（CN 为 1–13）；连接数上限 1–4，由 Kconfig 选定；可在编译期选择隐藏 SSID。
- 只用标准 wapi/WEXT ioctl 控制：`wapi essid wlan1 <SSID> 1` 启动，`wapi essid wlan1 <SSID> 0` 或 `ifdown wlan1` 停止。没有新增私有 ioctl。
- 客户端之间的帧由驱动转发（AP 内部转发），与 Bouffalo SDK lwIP 移植的做法相同。
- 驱动不配 IP 地址。地址和 DHCP 由 `netutils/dhcpd` 提供，`wifi` 配置里 `dhcpd_start wlan1` 即可（网关 192.168.169.1/24）。
- 客户端关联、离开只写 syslog，不提供客户端列表接口。
- 与 STA 同时工作时只支持同信道：不指定信道时 AP 跟随 STA 当前信道。

## 2. 结构

```text
wapi / ifup / ifdown (wlan1)
  -> bl616cl_wlan.c: g_softap_ops
  -> bl616cl_wifi_adapter.c: bl616cl_wifi_softap_*
  -> wifi_mgmr_ap_start() / wifi_mgmr_ap_stop() / wifi_mgmr_conf_max_sta()
  -> wl80211 AP（macsw AP VIF + supplicant 认证器）

wl80211 事件 AP_STOPPED / AP_STA_ADDED / AP_STA_DEL
  -> bl616cl_wifi_event_handler()（LPWORK）
  -> carrier、syslog
```

- macsw `vela_bl616cl` profile 本来就有 2 个 VIF、4 个 STA 表项和 beacon，SoftAP 不改 macsw、wl80211 private 和 supplicant；wl80211 的 AP 代码没有编译开关，STA 镜像里已经编入。
- wl80211 public 只改了 NuttX host port：`wl80211_output()` 多一个 VIF 参数（原来固定发往 STA VIF）。预编译包随之重新导出，见 STA 方案文档 4.5。
- 隐藏 SSID 由共用的 wl80211 实现（与 Bouffalo SDK 相同）：AP 启动参数 `wl80211_ap_settings.flags` 带 `WL80211_AP_FLAGS_HIDDEN_SSID` 时，beacon 里的 SSID 元素长度为 0，不回应通配 probe request，回应带本 AP SSID 的定向 probe request（应答里是真实 SSID），并且 beacon、probe response、association response 都不带 WPS 元素（WSC 2.0 要求隐藏的 AP 不启用 WPS）。适配层只是按 Kconfig 设置 `wifi_mgmr_ap_params_t.hidden_ssid`。
- 板级 bringup 在 `bl616cl_wlan_sta_initialize()` 之后调用 `bl616cl_wlan_softap_initialize()` 注册 `wlan1`；注册后接口是 DOWN，射频不动作。

## 3. 控制面

| 操作 | wlan1 上的行为 |
| --- | --- |
| `ifup wlan1` | 只打开接口，不启动 AP |
| `wapi mode wlan1 3` | 只接受 `IW_MODE_MASTER`，其它模式返回 `-EINVAL` |
| `wapi psk wlan1 <口令> 3` | 保存 WPA2-PSK（CCMP）口令，8–63 字符；以 `WPA_ALG_NONE` 设置（alg 取 0）清除口令，即回到 open；WPA、WPA3 版本和 TKIP、WEP 加密返回 `-ENOTSUP` |
| `wapi freq wlan1 <信道或 MHz> 1` | 按国家码信道表检查后保存；表外的值、非 FIXED 标志返回 `-EINVAL`。AP 已在运行时下一次启动生效；要回到自动选择信道只能 `ifdown` |
| `wapi essid wlan1 <SSID> 1` | 用保存的 SSID、口令、信道启动 AP；AP 已在运行时先停再启。接口未 up 返回 `-ENETDOWN`，`wifi_mgmr_ap_start()` 失败返回 `-EIO` |
| `wapi essid wlan1 <SSID> 0` | 停止 AP，保留 SSID、口令、信道 |
| `ifdown wlan1` | 停止 AP，并清除 SSID、口令、信道 |
| `wapi show wlan1` | 运行中 ESSID 显示 ON、频率为实际信道；AP 地址为 `wlan1` 的 MAC；bitrate、txpower、sensitivity、PTA 优先级读不到 |

- 不指定信道时：STA 已连接则用 STA 的信道，否则用信道 11。
- wlan1 不支持、返回 `-ENOTSUP` 的请求：bitrate、RSSI（`SIOCGIWSENS`）是每个客户端各自的值，AP 没有单一值；省电、PMKSA 只属于 STA；Wi-Fi/BLE 共存（PTA）只针对 STA；发射功率和 DTIM 是 AP 参数，但 wifi_mgmr 启动 AP 时写死（功率 20 dBm、beacon 间隔 100 TU），没有接口可传；扫描用 `wlan0`。国家码和信道范围（`SIOCGIWRANGE`）与 STA 共用同一套实现。
- 连接数上限：每次启动前把 `CONFIG_BL616CL_WIRELESS_WIFI_SOFTAP_MAX_STA` 交给 `wifi_mgmr_conf_max_sta()`。客户端满了以后，新关联请求得到状态码 17（AP 无法容纳更多 STA），已连客户端不受影响。
- 空闲客户端：每次启动前把 `CONFIG_BL616CL_WIRELESS_WIFI_SOFTAP_IDLE_TIMEOUT`（秒，向上取整到 5 的倍数）交给 `wifi_mgmr_ap_set_max_idle_time()`。wl80211 每 5 s 检查一次，超过这个时间没有收到某个客户端的任何帧，就向它发 deauth（reason 4），驱动随后打印离开。用于清掉不发 deauth 就离开的客户端；0 关闭。
- 停止：wl80211 的停止是异步的，驱动发出停止请求后等待 `AP_STOPPED` 事件（最多 3 s）再返回，因此停止后立即启动是安全的。停止时 wl80211 向所有客户端发广播 deauth（reason 3）。
- carrier：AP 运行时 on，停止后 off。dhcpd 等 UDP 发送依赖 `IFF_RUNNING`。
- 事件：`AP_STA_ADDED` 打印 `wlan1: client <MAC> associated`，`AP_STA_DEL` 打印 `wlan1: client <MAC> left`。WPA2 客户端在四次握手完成后才上报 `AP_STA_ADDED`；握手失败（例如口令错误）时只有 `AP_STA_DEL`，驱动不为这类从未关联成功的客户端打印日志。AP 停止时不逐个打印离开。

## 4. 数据面

```text
接收：wl80211 RX 回调（vif_type）
  STA VIF -> 只收 IPv4/IPv6/ARP，进 wlan0
  AP VIF  -> 全部进 wlan1 的接收队列
        -> 网络线程 rxpoll：
           目的为组播/广播 -> 复制一份进 wlan1 发送队列，原帧交协议栈
           目的为 wlan1 自己 -> 交协议栈
           其它单播 -> 直接进 wlan1 发送队列（AP 内部转发）

发送：wlan0/wlan1 -> wl80211_output(vif_type, IOB) -> wl80211_mac_tx()
```

- 转发在网络线程（LPWORK）的 rxpoll 中完成，与协议栈对发送队列的访问在同一线程，不另加锁。转发帧走与本机发送相同的发送路径；RX 零拷贝的槽 headroom 不够时，发送路径照常先复制进池 IOB。
- `wl80211_output()` 的在途上限 24 由 STA 与 AP 共用；TX 完成回调会让每个已 up 的接口重试发送。
- AP 收到的帧都交给 `wlan1`（STA 侧仍按以太类型过滤），因为客户端之间的帧不论类型都要转发。

## 5. 配置

```text
BL616CL_WIRELESS_WIFI
└── BL616CL_WIRELESS_WIFI_SOFTAP            注册 wlan1
    ├── BL616CL_WIRELESS_WIFI_SOFTAP_MAX_STA       1–4，默认 4
    ├── BL616CL_WIRELESS_WIFI_SOFTAP_IDLE_TIMEOUT  0–2145 s，默认 300，0 关闭
    └── BL616CL_WIRELESS_WIFI_SOFTAP_HIDDEN        隐藏 SSID，默认关
```

`ai-m64p-32s-kit/wifi` 配置打开 SoftAP、`NETUTILS_DHCPD` 与 `EXAMPLES_DHCPD`（提供 `dhcpd_start`/`dhcpd_stop` 命令），并打开 NSH 的 `ifup`/`ifdown`。dhcpd 地址：

| 项 | 值 |
| --- | --- |
| `NETUTILS_DHCPD_ROUTERIP` / `DNSIP` | 192.168.169.1 |
| `NETUTILS_DHCPD_STARTIP` | 192.168.169.2 |
| 掩码 | 255.255.255.0（dhcpd 默认） |

`dhcpd_start wlan1` 设置接口地址与掩码、打开接口并启动 dhcpd；dhcpd 是单实例，只服务一个接口。

## 6. 用法

```text
nsh> dhcpd_start wlan1
nsh> wapi psk wlan1 <8–63 字符口令> 3      # 不执行这一步则为 open
nsh> wapi freq wlan1 6 1                   # 可选
nsh> wapi essid wlan1 MY_SOFTAP 1
wlan1: AP "MY_SOFTAP" started on channel 6, WPA2-PSK      # 隐藏时末尾多 ", hidden"
wlan1: client xx:xx:xx:xx:xx:xx associated
nsh> wapi essid wlan1 MY_SOFTAP 0          # 停止，保留设置
nsh> ifdown wlan1                          # 停止并清除设置
```

同时作为 STA 联网时，先让 `wlan0` 连上上游 AP，再启动 `wlan1`，AP 会落在同一信道。

## 7. 与 STA 并存

- STA 和 AP 共用一个射频与 MAC 地址。不指定信道时 AP 自动用 STA 的信道。
- 同信道并存可用：STA 连上游 AP（信道 6）、AP 同信道运行并有客户端时，两侧同时 ping 1 分钟，STA 到网关 298/300、客户端到 AP 300/300；客户端反复关联与 STA 重连同时进行（15 次与 5 次）也全部成功。
- 异信道并存不可用：AP 在信道 1 运行时 STA 去连信道 6 的上游 AP，连接和 DHCP 能完成，macsw 建立两个信道上下文，但稳定后射频基本停在 AP 的信道，AP 侧正常、STA 到网关 0/150。需要同时联网时先连 STA，再不指定信道启动 AP。

## 8. 已知限制

- 不支持 WPA3/PMF、5 GHz、40 MHz；这些需要先在共用的 wl80211 中实现。
- 隐藏 SSID 只能在编译期选择，运行中不能切换；NuttX 没有对应的标准 ioctl。客户端要事先知道 SSID 并按隐藏网络配置（例如 NetworkManager 的 `802-11-wireless.hidden yes`、wpa_supplicant 的 `scan_ssid=1`）；Vela 设备作客户端时 `wapi essid` 直接可连。隐藏不是安全手段：SSID 仍出现在 probe response、客户端的定向 probe 和关联请求里。只验证过 Linux（NetworkManager）与 Vela 客户端，手机和 Windows 未测。
- 没有运行时设置连接数上限、查询客户端列表或踢客户端的接口；NuttX 也没有这类标准 ioctl。
- wl80211 的 AP 接受没有先发认证帧的关联请求。这样的客户端由空闲清理在超时后移除，超时前占用一个名额，所以伪造关联请求仍能在这段时间内占满名额。
- 空闲清理到期直接 deauth，不先向客户端发探测帧确认它是否还在，空闲时一帧都不发的客户端到期会被断开、需要重连。实测 Linux 客户端（树莓派）空闲时持续发 QoS Null；macsw 的 STA 在 55 s 没有发送时发保活帧。超时上限 2145 s 来自 wl80211 比较时间用的 32 位微秒计时器。
- 客户端之间不能隔离：驱动总是转发客户端之间的帧。
- STA 适配层在每次断开后清除口令和 SSID，Vela 设备作为客户端重连时要重新 `wapi psk`（STA 现有行为）。
- 预编译包里的 `nuttx.c` 必须与 `chips/bl616cl/bl616cl_wl80211_port.h` 中带 VIF 参数的 `wl80211_output()` 原型一致；wl80211 public 更新后按 STA 方案文档 4.5 重新导出。

## 9. 验收结果

2026-11-12，ai-m64p-32s-kit 作 AP（`wifi` 配置）；客户端为 bl616cl-ave-v10（Vela STA）与 Linux 主机（树莓派板载网卡）；屏蔽箱内，上游 AP 为 2.4 GHz 信道 6、WPA3。

| 项 | 内容 | 结果 |
|---|---|---|
| 开放 AP | 两个客户端关联、DHCP，与 AP 双向 ping 各 20 次 | 通过，0 丢包 |
| WPA2-PSK | 同上；一个客户端用错误口令 | 通过；错误口令四次握手失败，另一个客户端同时 ping 80/80 |
| 信道 | 信道 1、6、11、13 抓包；信道 14（含 2484 MHz） | beacon 都在对应信道；14 返回 `-EINVAL` |
| 默认信道 | STA 未连、已连信道 6 | 11、6 |
| AP 内部转发 | 两个客户端互 ping、广播 ARP | 0 丢包，ARP 正常解析 |
| 连接上限 | MAX_STA=1：第二个客户端；第一个离开后再连。MAX_STA=2：两个同时 | =1 时被拒（状态码 17）、第一个不受影响、离开后可连；=2 时都在线并互通 |
| 启停 | `essid … 0`、`ifdown`、`ifup`，STA 在线 | 广播 deauth（reason 3）、beacon 消失，再启可连；STA 不受影响；`ifup` 不启动 AP |
| 反复关联 | 客户端关联/去关联 50 次 | 50/50，空闲内存不变 |
| AP 启停 | 20 次，每次客户端重连 | 20/20，空闲内存在前 5 次内一次性减少 636 B，之后不变 |
| AP↔STA 切换 | AP 启动、客户端连接、AP 停止、STA 连上游并 ping 网关、断开，10 轮 | 10/10 |
| 空闲 | 无客户端约 1 h | beacon 持续，之后可扫描并关联 |
| 空闲清理 | 超时 300 s：注入一个未认证就关联、之后不再发帧的客户端；Linux 客户端连上后 10 min 不发业务流量 | 注入的客户端在 300 s 时被 deauth（reason 4）并移除；Linux 客户端一直在线 |
| STA 回归 | 关、开 SoftAP 两种镜像跑 STA 连接与 iperf 回归 | 与改动前镜像一致 |

隐藏 SSID（2026-11-13，同一台架，AP 在信道 1，空口用另一块网卡在 monitor 模式抓包）：

| 项 | 内容 | 结果 |
|---|---|---|
| beacon | 隐藏的 open 与 WPA2 AP | SSID 元素长度 0，其余元素与不隐藏时相同；WPA2 时带 RSN、不带 WPS |
| probe | 客户端通配、定向、错误 SSID 的定向 probe 各 3 轮 | 通配与错误 SSID 无应答；定向有应答，应答带真实 SSID、不带 WPS |
| 扫描 | Linux 被动扫描；Vela `wapi scan` | 都只看到 BSSID，SSID 为空 |
| 连接 | Linux（隐藏网络配置）与 Vela 客户端，open 与 WPA2 | 都能连上，DHCP、双向 ping 与客户端互 ping 各 20 次 0 丢包；错误口令握手失败，另一客户端同时 ping 150/150 |
| 重连 | Linux 客户端断开重连 10 次；AP 停启 5 次后重连 | 15/15 |
| 省电客户端 | Linux 客户端省电开启，1 s 间隔 ping 60 次 | 60/60 |
| 不隐藏回归 | 默认 `wifi` 配置 | beacon 元素与改动前相同（WPA2 时仍带 WPS），回应通配 probe，两种客户端 open/WPA2 可连 |

NetworkManager 1.52 对所有连接配置都用定向 probe 扫描，所以在 Linux 上“不按隐藏网络配置就连不上”不成立；隐藏是否生效以空口抓包为准。

吞吐（iperf2，30 s，Mbit/s；TX 为 AP 发送，UDP RX 对端发送 30 Mbit/s；同一时段）：

| 链路 | TCP TX | UDP TX | TCP RX | UDP RX |
|---|---|---|---|---|
| AP ↔ Linux 客户端 | 18.4 | 40.7（客户端收到 26.9） | 19.1 | 31.5 |
| AP ↔ bl616cl-ave-v10 | 15.9 | — | 15.7 | — |
| STA ↔ 上游 AP 后的有线主机，改动前镜像 | 17.6–17.8 | 36.0–36.4 | 21.5 | 30.0 |
| 同上，`wifi`（SoftAP 开） | 18.3 | 39.4 | 21.7 | 30.0 |

- 开启 SoftAP 后 STA 吞吐不低于改动前；两个镜像 `.text` 相差约 8 KB，代码布局不同，UDP TX 的差异按布局影响看待。`wlan1` 注册使空闲堆少 544 B。
- 板端 iperf server 默认绑定 `wlan0` 的地址，在 AP 侧测试时加 `-B 192.168.169.1`。
