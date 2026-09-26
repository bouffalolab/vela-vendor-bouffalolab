# BL616CL Wi-Fi STA 移植方案

## 1. 方案结论

本次工作将 BouffaloLab BL616CL 的 Wi-Fi STA 能力接入 openvela/NuttX，形成以下闭环：

```text
NSH / WEXT
    |
    v
BL616CL netdev / adapter（chips/bl616cl）
    |
    +--> wifi_mgmr: 扫描、关联、断开、状态
    |       |
    |       +--> wl80211 core archive
    |       |       |
    |       |       +--> net80211 / STA / scan / connect
    |       |       +--> macsw BL616CL/vela_bl616cl
    |       |       +--> WPA supplicant
    |       |
    |       +--> BL616CL adapter / IRQ / worker / RF 参数
    |
    +--> NuttX netdev / IOB / TCP-IP
            |
            +--> DHCP / ping / TCP / UDP
```

最终方案的首版目标是：

- BL616CL 以 STA 模式启动并注册 NuttX `netdev`；
- 支持 Open AP 和 WPA2-PSK 扫描、关联、断开；
- 通过 NuttX DHCP 获取 IPv4 地址；
- 能够 ping 网关，并完成 TCP/UDP 双向数据收发；
- Wi-Fi 配置独立于最简 `nsh` 配置；
- 不把 wl80211 私有 core 的 Bouffalo SDK/FreeRTOS 构建逻辑带入 NuttX 固件。

WPA3-SAE 已完成 100 轮连接循环和四方向 100 秒压力，尚未做 500 轮/500 秒级压力。首版不包含 SoftAP、P2P 或 FHOST 双栈集成。

## 2. 仓库与版本边界

本次移植保持各仓库的原有职责，不把不同层的适配代码混放到一个仓库。

| 仓库 | 作用 | 本次关键提交/状态 |
| --- | --- | --- |
| `vela-manifest` | 通过 repo 固定 wireless 子仓路径和远端 | `884c975`，增加 `blgerrit` remote 及 macsw、wl80211 public/private、supplicant 项目 |
| `vela-nuttx` | NuttX 网络栈和 OS 基座 | `97496437931`，修复 buffered send 唤醒竞态 |
| `vela-vendor-bouffalolab` | OpenVela wrapper、BL616CL glue、板级、linker、配置 | `6365c1f`、`4f56261`、`7069305`、`71bd08b`、`f112fa0`、`62b724f`、`253c8a0` 等 |
| `macsw` | Wi-Fi MAC/协议数据路径和 BL616CL/vela_bl616cl 配置库 | `f9b9a8b4`，恢复单天线扫描间的 coex plan；本地 `5245e902`，增加 `vela_bl616cl` profile（未推送） |
| `wl80211/public` | wl80211 对外头文件、macsw 接口和公共兼容层 | 当前本地 `1f98b57` |
| `wl80211/private` | wl80211/net80211 core，单独生成 `libwl80211_bl616cl.a` | 当前本地 `d189124` |
| `bl_wpa_supplicant` | WPA/WPA2/WPA3 认证相关源码 | `de35a74`（`ffc9839` rebase 到 2.3.35），增加 NuttX OS port 和 mbedTLS PBKDF2；`36e4c44`，`os_get_random()` 改用 `getrandom()` |
| `bouffalo_sdk-drivers` | BL616CL PHYRF、rfparam、LHAL 等原厂驱动 | `8470912`，补充 BL616CL PHYRF 兼容头 |

manifest 当前按 `master` 跟踪无线子仓，移植验收时使用的提交 SHA 仍需作为发布输入清单保存。本次工作没有执行 push、发布或合并；当前仓库引用状态中，部分提交已存在于对应远端主分支，vendor、NuttX、wl80211 public/private、supplicant 和 manifest 的本地工作分支仍应按实际 remote/ref 状态分别审计，不能把工作区 HEAD 直接视作已发布版本。

## 3. Manifest 集成

`vela-manifest` 的 `884c975` 增加了无线组件的 repo 管理：

```xml
<project path="vendor/bouffalolab/components/wireless/wifi/bl_wpa_supplicant/bl_wpa_supplicant"
         name="bouffalo_sdk-bl_wpa_supplicant" remote="bouffalo" revision="master" />
<project path="vendor/bouffalolab/components/wireless/wifi/macsw/macsw"
         name="bouffalo/components/wifi6/macsw" remote="blgerrit" revision="master" />
<project path="vendor/bouffalolab/components/wireless/wifi/wl80211/wl80211"
         name="bouffalo/components/wl80211/public" remote="blgerrit" revision="master" />
<project path="vendor/bouffalolab/components/wireless/wifi/wl80211/wl80211/src"
         name="bouffalo/components/wl80211/private" remote="blgerrit" revision="master" />
```

这样做的目的有两个：

1. 将 macsw、wl80211 public/private 和 supplicant 作为独立组件管理，避免依赖工作区中偶然存在的 clone；
2. 让 vendor wrapper 可以按明确的源码/库边界编译，同时保留各组件自己的提交历史和 owner。

发布版本应进一步把最终验证使用的 revision 写入冻结 manifest 或 release lock，不能只依赖 floating `master`。

内部 gerrit 项目直接写入 `bl-vela-sdk.xml` 是项目初期的临时安排。它与 manifest 仓 README 中“下游消费方不接触内部仓、无线组件以预编译库分发”的规划不一致：没有内部 gerrit 权限的使用者无法完成 `repo sync`。待初版 ready、release 流程跑通后，再把内部源码项目拆到内部 overlay manifest，公开 manifest 只保留可分发内容。

## 4. Vendor 适配层

### 4.1 组件布局和 Kconfig

原有组件 wrapper 首先在 `6365c1f` 中加入：

- `components/macsw/{CMakeLists.txt,Kconfig}`；
- `components/wl80211/{CMakeLists.txt,Kconfig}`；
- `components/bl_wpa_supplicant/{CMakeLists.txt,Kconfig}`；
- `boards/.../configs/wifi/defconfig`。

随后 `4f56261` 将它们整理到：

```text
components/wireless/
├── rfparam/                 # 选择 drivers/rfparam 源文件的 wrapper
└── wifi/
    ├── macsw/
    ├── wl80211/
    └── bl_wpa_supplicant/
```

`wifi/` 下每个组件目录只有公共子仓及对应的 `CMakeLists.txt`、`Kconfig`；BL616CL 专用实现在 `chips/bl616cl`。

顶层只做一层 `nuttx_add_subdirectory()` 和 Kconfig 菜单生成。每个组件 wrapper 自己根据 `CONFIG_BL_COMPONENT_*` 选择是否接入，避免 vendor 顶层直接拉入所有无线源码。

### 4.2 macsw wrapper

`components/wireless/wifi/macsw/CMakeLists.txt` 的职责是：

1. 只接受 `CONFIG_ARCH_CHIP_BL616CL` 和 `CONFIG_MACSW_SELECT="vela_bl616cl"`（Kconfig 默认值）；
2. 进入 macsw 独立 CMake 工程，使用与 Vela 相同的交叉工具链；
3. 输出 `libmacsw_bl616cl.a` 和 `libmacsw_config_bl616cl_vela_bl616cl.a`；
4. 删除 Vela GCC 不支持的 `-mtune=e907`、`-march=rv32imafc_xtheade`；
5. 使用标准 ISA `-march=rv32imafc_zicsr_zifencei`，并关闭不适用于该源码的局部告警；
6. 以 external kernel library 方式交给 NuttX 最终链接。

`vela_bl616cl` 是 macsw 的功能/资源 profile，不是工具链或 ABI 标识。它对应 macsw 仓的 `inc/macsw_vela_bl616cl_config.h`：在 `macsw_default_config.h` 基础上把 `CFG_RXL_BUFFER1_AMSDU_CNT` 从 1 改为 2、`CFG_REORD_BUF` 从 12 改为 8，原因见 12.8。芯片、profile、工具链必须作为一组输入锁定。

### 4.3 wl80211 core、host port 与 chip 适配分界

这是本次集成的核心边界。

`components/wireless/wifi/wl80211/CMakeLists.txt` 不在 NuttX 编译环境中直接编译 private `src/net80211`。它执行独立的 CMake/Ninja 子构建：

```text
wl80211/wl80211/src
        |
        +--> libwl80211_bl616cl.a
```

子构建显式接收：

- `CHIP=bl616cl`；
- `CONFIG_MACSW_SELECT`（与 macsw wrapper 相同，为 `vela_bl616cl`）；
- PHYRF include；
- macsw include；
- supplicant include；
- Vela 工具链。

vendor 主构建随后只消费该 archive，并额外链接 BL616CL PHYRF 库。这样隔离了 private core 的 SDK 侧 CMake、FreeRTOS/lwIP 假设和 NuttX 头文件差异。

NuttX host port 直接从 public 子仓编译，wrapper 生成 `bl_wl80211` 库；原生 Bouffalo SDK 从同一仓库选择 FreeRTOS/lwIP 文件（`wl80211_platform.c`、`lwip.c`、`wifi_mgmr_cli.c`），两套源文件互不进入对方构建：

```text
components/wireless/wifi/wl80211/wl80211/   # public 子仓
├── wifi_mgmr.c              # 管理面封装
├── country.c
├── supplicant.c
├── nuttx.c                  # NuttX OS/网络接口
└── rtos_al_nuttx.c          # NuttX RTOS 抽象
```

BL616CL 专用实现编入 `arch`（`chips/bl616cl/CMakeLists.txt`，`CONFIG_BL_COMPONENT_WL80211`）：

```text
chips/bl616cl/
├── bl616cl_wlan.[ch]            # netdev/WEXT/收发回调
├── bl616cl_wifi_adapter.[ch]    # adapter 和控制流程
├── bl616cl_efuse_mac.[ch]       # bl616_efuse_read_mac_address()：出厂 MAC（mfg media）与本地管理回退
├── bl616cl_macsw_plat.c         # macsw 低功耗 hook 与单调时间源
├── bl616cl_rfparam_ext.c        # rfparam 扩展接口
└── bl616cl_wifi_glb.h
```

public 与 chip 之间的接口：chip 实现 `bl616_wifi_event_handler()`、`bl616_efuse_read_mac_address()`，并通过 `internal_register_recv_cb()`、`internal_register_txdone_cb()` 注册 RX 与 TX 完成回调。

这种划分对应 BL4 的实际结构：private core 作为库，平台适配在芯片层编译。它避免将 NuttX 特定实现反向塞入 wl80211 private core。

### 4.4 supplicant wrapper

`components/wireless/wifi/bl_wpa_supplicant/CMakeLists.txt` 显式列出首版所需源文件：

- `port/os_nuttx.c`；
- WPA/RSN、SAE、AES、DH、SHA、ECC 等认证和加密源文件；
- `src/crypto/sha1-pbkdf2-nuttx.c`；
- Vela mbedTLS crypto 适配源。

wrapper 不执行公共仓库面向 Bouffalo SDK 的原始 CMake，也不编译 FreeRTOS port 或 test 目录。Kconfig 通过 `BL_COMPONENT_WPA_SUPPLICANT` 选择 mbedTLS 依赖，包括 PKCS5、CTR、ECDH、ECDSA、SHA512 等配置。

`os_nuttx.c` 将：

- 时间和 sleep 映射到 `gettimeofday()`、`nanosleep()`；
- 内存映射到 `kmm_malloc/kmm_free/kmm_realloc/kmm_calloc`；
- `os_random()` 映射到 NuttX `random()`；`os_get_random()` 调用 `getrandom(buf, len, 0)` 并要求返回完整长度，经 `/dev/urandom` 由 BL616CL TRNG 驱动提供。

PBKDF2 则使用 Vela 的 mbedTLS `mbedtls_pkcs5_pbkdf2_hmac()`，避免引入另一套 crypto 实现。

## 5. BL616CL 平台适配

### 5.1 启动和 netdev 注册

`boards/bl616cl/common/src/bl616cl_bringup.c` 在 `CONFIG_BL_COMPONENT_WL80211` 打开时调用：

```c
bl616_wlan_sta_initialize();   /* chips/bl616cl/bl616cl_wlan.h */
```

初始化顺序包括：

1. `bl616_wifi_adapter_init()` 建立 Wi-Fi adapter 和底层控制环境；
2. 读取 eFuse/MAC 地址；
3. `bl616_net_initialize()` 注册 BL616CL STA netdev；
4. 注册 RX callback 和 TX-done callback；
5. 根据配置启动后续 Wi-Fi 管理流程。

`bl616cl_wlan.c` 提供 NuttX 网络接口，设置 `d_txavail`，并通过 `netdev_register(netdev, NET_LL_IEEE80211)` 将设备纳入 NuttX 网络栈。carrier 状态由连接/断开事件更新。

### 5.2 控制面

NuttX WEXT/ioctl 请求在 `bl616cl_wifi_adapter.c` 中转换为 wl80211/wifi_mgmr 操作：

```text
WEXT scan
  -> wifi_mgmr_sta_scan()
  -> wl80211_scan()
  -> scan result RB tree
  -> WEXT scan result

WEXT connect
  -> wifi_mgmr_sta_connect()
  -> wl80211_sta_connect()
  -> macsw/STA + WPA supplicant
  -> connected event
  -> netdev_carrier_on()

WEXT disconnect
  -> wifi_mgmr_sta_disconnect()
  -> wl80211_sta_disconnect()
  -> netdev_carrier_off()
```

扫描结果在 adapter 中转换为 NuttX/WAPI 所需的 SSID、BSSID、信道、RSSI、认证和 cipher 信息。`71bd08b` 补齐了 BL616CL 的 WEXT scan result 路径和相关 NuttX RTOS 辅助接口。

连接流程支持：

- Open AP；
- WPA2-PSK/CCMP；
- 由 `wifi_mgmr` 将频率/信道、SSID、密码/BSSID、PMF 参数转换成 wl80211 connect 参数；
- 连接完成后由 netdev carrier 状态向 NuttX 网络栈报告链路状态。

### 5.3 数据面

数据面采用直接 NuttX `net_driver_s` 集成，不再额外引入 lwIP netif：

```text
NuttX socket
  -> TCP/UDP buffered send
  -> NuttX netdev poll / d_txavail
  -> bl616_wlan TX
  -> wl80211 NuttX host port（IOB 链直接作为 PBD 段）
  -> Wi-Fi hardware

Wi-Fi hardware
  -> bl616_wlan RX callback
  -> NuttX IOB / netdev receive path
  -> TCP/UDP/IP
```

TX/RX 的关键原则是：

- NuttX socket 和 IOB 负责上层网络缓冲，IOB 固定池位于 Wi-Fi 可见的 `ram_wifi`，TX 不复制；
- wl80211/macsw 负责 Wi-Fi 帧和描述符，描述符放在首个 IOB 的 guard 中；
- 跨硬件可见区域的数据必须遵守 shared RAM/cache ownership；
- 异步 TX 完成后由明确的 owner 回收资源；
- 在途帧达到上限时不得丢失可重试的发送机会。

`wifi` 配置打开 RX 零拷贝（`CONFIG_BL616CL_WLAN_RX_ZEROCOPY`）：

- ≥500 B 的帧不再从 wl80211 的 host RX 槽拷进 IOB 池。驱动用 `iob_init_with_data()` 把槽包成 IOB：IOB 头放在槽的 `rx_info` 上（wl80211 调用 RX 回调时已经读完它），`io_offset` 指向 L3，与拷贝路径相同；IOB 释放时 `io_free` 把槽还给 wl80211。短帧照旧拷贝：拷贝便宜，而且收到的 TCP ACK 要留在池 IOB 上，协议栈会在 ACK 的 IOB 上构造下一个数据段。
- 同时借给协议栈的槽最多 `(CFG_BARX - 1) × CFG_REORD_BUF` 个（`vela_bl616cl` profile 为 8 个，槽共 18 个），其余留给 BA 重排序和交接；借满后照旧拷贝。槽用光时 MAC 连 beacon 也收不到。
- 协议栈可能在收到的 IOB 上直接构造回复（ICMP echo 等），而槽里只有约 60 B 的 headroom。`wlan_transmit()` 发现首个 IOB 的 `io_offset` 小于 `CONFIG_NET_LL_GUARDSIZE` 时，先把整帧复制进池 IOB 再发送；池里没有 IOB 就丢掉这一帧，由上层重传。
- 该选项 select `IOB_ALLOC`，每个 IOB 头多 12 B（池 60 时 `.wifibss` 多 720 B）。配套修改：nuttx `c016d23b0d6` 在 `iob_initialize()` 中清零池 IOB 的 `io_free`；wl80211 public `7eb191f` 适配指针形式的 `io_data`；macsw `4da8c811` 给 `GLOBAL_INT_DISABLE/RESTORE` 加 memory clobber，因为 `wl80211_mac_rx_free()` 现在也在网络线程中调用，槽队列的更新必须留在临界区内。

## 6. Shared RAM、cache 和 linker

BL616CL linker 在 `boards/bl616cl/ai-m64l-32s-kit/scripts/ld.script` 中为 Wi-Fi 保留独立的 `ram_wifi`：

```text
ram_wifi ORIGIN = 0x21020000 - CONFIG_BL616CL_WRAM_SIZE KiB
         LENGTH = (CONFIG_BL616CL_WRAM_SIZE - CONFIG_BL616CL_EM_SIZE) KiB
```

WRAM 和 BLE EM 的划分沿用原生 `bl616cl_common.ld.in`：EM 从 WRAM 顶部划走，启动时 `bl616cl_em_select()` 按 `__LD_CONFIG_EM_SEL` 设置 GLB EM_SEL，系统 RAM 为 `384K - 1K - WRAM`。Kconfig 默认 EM 为 0、WRAM 为 128K；选 EM 16K/32K 时 WRAM 默认改为 144K/160K，`ram_wifi` 保持 128K，多出的部分从系统 RAM 让出（EM 32K 时系统 RAM 由 255K 降为 223K）。NuttX CMake 不会因配置变化重新预处理链接脚本，改这两项后要先 `vela clean`。

各配置的 WRAM 取值：`wifi` 设 102K（IOB 池 60，`.wifibss` 之外约留 4.9 KiB 余量）；`nsh`、`nsh-peripherals`、`ostest` 不用 Wi-Fi，`.wifibss` 为空，设为 Kconfig 下限 64K，系统 RAM 为 319K。defconfig 显式写了 WRAM 之后，再选 EM 不会自动加大 WRAM，需要手动把它改为原值加 EM（例如 `wifi` 选 EM 32K 时设为 134K）。

`.wifibss` 将以下对象放入 Wi-Fi 可见区域：

- `SHAREDRAMIPC` / `SHAREDRAM`，其中包括 NuttX IOB 固定池（wifi defconfig `CONFIG_IOB_SECTION="SHAREDRAM"`，与 BL4 相同）；
- macsw 的 TX buffer/frame；
- scan/shared 数据；
- MFP、MIC 和其他 Wi-Fi shared/common 对象；
- `wifi_ram*` 区段。

TX 零拷贝镜像中（EM 0、WRAM 128K；EM 32K 时起始地址为 `0x20ff8000`，其余相同）：

- `ram_wifi` 起始地址为 `0x21000000`；
- 区域大小为 `0x20000`；
- `.wifibss` 使用 `0x1df00`（其中 IOB 池 `g_iob_buffer` 58,683 字节）；
- 剩余 8,448 字节。

当前 `wifi` 镜像（WRAM 102K、IOB 池 60）：`ram_wifi` 起始地址为 `0x21006800`，大小为 `0x19800`；`.wifibss` 使用 `0x18440`，剩余 5,056 字节（打开 RX 零拷贝前为 `0x18170`；换用 `vela_bl616cl` profile 前为 `0x192a0`、剩余 1,376 字节，见 12.8）。

XIP 代码段 `.text` 的起点对齐到 32 KiB（I-cache 大小），RAM 段的 flash 镜像变大或变小时，代码的 cache set 不再跟着移动。`.text` 开头按调用顺序排列 Wi-Fi 热函数，分为任务循环、TX、TX 确认、RX 四组，共约 35 KB，让这些函数集中在少数 cache set 里，中间不夹冷代码。列表以 macsw 的 `macsw_cache_affinity.ld.in` 为起点：其中 147 个函数名只有 75 个在本构建的 LTO 输出中仍是独立函数；再加上 80 MHz tick 采样里同一路径上的任务循环、host port 和 glue 函数。LTO 会给局部函数加 `.lto_priv/.isra/.constprop` 后缀，所以每个名字都同时匹配 `.text.fn` 和 `.text.fn.*`。列表直接写在 `ld.script` 中，因为 NuttX CMake 预处理链接脚本时不跟踪被 include 的文件。效果见 12.9；组件更新后的检查和回归步骤见 [bl616cl-hot-code-layout.md](bl616cl-hot-code-layout.md)。

`ram_wifi` 链接在 nocache 别名上，协议栈对 IOB 的读写都不经 cache；原生 SDK 的 lwIP 内存同样从这里分配。改为零拷贝前，`.wifibss` 为 `0x1ab30`，其中 45,408 字节是 wl80211 private 的 TX pool；IOB 池移出后系统堆增加约 58.7 KB。`.wifibss` 为 NOLOAD 且启动时不清零；`CONFIG_IOB_ALLOC` 关闭时 `iob_initialize()` 写入每个节点的链表指针、分配时重置长度与偏移，不依赖清零；打开时 `iob_initialize()` 还要把 `io_free` 置空（nuttx `c016d23b0d6`），否则 `iob_free()` 会调用残留的野指针。

wl80211 public/private 侧的配套修改包括：

- public `38a8fb9`：保留 BSD queue/tree 兼容头；
- public `4f85514`：NuttX 下使用 `sys/queue.h`，scan-result tree 统一使用 vendored `tree.h`；
- public `0b1e062`：修正 NuttX host port（`nuttx.c`、`rtos_al_nuttx.c`）在当前 NuttX 下的编译；
- public `bf349bc`：`bl_lp.h` 仅在 `CONFIG_LPAPP` 下包含；
- public `d1d2a8a`：`wifi_mgmr.c` 的 scan-result 读者在锁内复制记录；
- public `22224e4`：TX 描述符大小的静态检查扣除以太头占用的 guard；
- public `b086c54`：NuttX STA TX 在途帧上限、完成回调与 `wl80211_output_ready()`；
- public `7eb191f`：`CONFIG_IOB_ALLOC` 下由数据地址找回池 IOB，TX 头放在 `io_data`（RX 零拷贝需要）；
- private `151365a`：BL616CL 按工具链探测 ISA 参数，并增加 `CONFIG_MACSW_SELECT` profile 定义；
- private `aad7cb5`：空 SSID 上报不再清除已知 SSID；
- private `d189124`：`wl80211_scan_result_lock/unlock` 基于 `rtos_lock()` 实现，生产者在锁外构造记录。

private 的 TX 路径与 master 相同。此前为复制方案加入的 TX pool（private `b86b33e`、`2fee8fe`、`089097b`，public `93e1af5`、`7369603`）已撤回。

`wl80211.h` 强制使用统一的 tree layout，避免不同 translation unit 对 RB tree entry 的大小和布局理解不一致。这是扫描结果树跨 public header、private core 和 chip adapter 时的 ABI 约束。

scan-result tree 由 private 的 `wl80211_scan_result_lock/unlock` 保护，底层是 `rtos_lock()`（FreeRTOS `vTaskEnterCritical`，NuttX `sched_lock`）。持锁期间不得分配或释放内存、打印、回调或阻塞：WiFi task 在锁外构造记录，锁内只链接或合并；`wifi_mgmr.c` 和 chip adapter 在锁内只计数、复制或摘除节点，内存分配、排序和释放都在锁外进行。NuttX 的 `sched_lock` 只在单核上提供互斥。

## 7. TX 资源所有权与稳定性修复

STA TX 采用零拷贝，与原生 SDK 和 BL4 相同：

- `wl80211_output()` 把 TX 描述符放在首个 IOB 的 guard（`CONFIG_NET_LL_GUARDSIZE=388`，以太头占最后 14 字节），把 IOB 链作为 PBD 段交给 `wl80211_mac_tx()`；一个 MTU 帧占 3 个 640 字节 IOB，不超过 `TX_PBD_CNT=5`；
- MAC 从 L3 数据起点向前写 LLC、安全头和 MAC 头，最坏需要以太头之前 38 字节，落在 guard 内；SW 重传复用同一份帧，IOB 在最终完成前不得改动；
- 完成回调 `wl80211_sta_tx_complete()` 释放 IOB 链，递减在途计数，并调用 chip 通过 `internal_register_txdone_cb()` 注册的钩子；
- MAC 队列本身没有上限，在途数据帧最多 `WL80211_TX_INFLIGHT_MAX`（24，与原 TX pool 槽数相同）；达到上限时 `wl80211_output()` 返回 `-EAGAIN`，IOB 仍归驱动，驱动存入 `tx_pending`，完成钩子触发下一轮发送；驱动在 poll 前用 `wl80211_output_ready()` 判断；
- raw/EAPOL/管理帧不计入在途上限，复制进同一 IOB 池后发送；`wl80211_output_raw()` 返回非 0 时不调用完成回调，`opaque` 仍归调用者（NuttX 与原生 lwIP 实现一致）；
- `wlan_transmit()` 结束时若 `txb` 或 `tx_pending` 仍有帧，启动 `txtimeout`（`WLAN_TXTOUT`，1 秒），TX 完成时取消，超时后重新发送并 poll，丢失完成唤醒时发送最多延迟 1 秒。

历史修复：vendor `f112fa0 fix(wifi): correct STA TX ownership` 修正了提交、异步完成和失败路径中的 owner 转移；vendor `62b724f fix(wifi): retry TX after pool release` 让资源释放后重新触发发送路径。零拷贝沿用这两项合同，只是资源从 TX pool 槽改为在途帧计数。

macsw `f9b9a8b4` 则恢复 BL616CL 单天线跨扫描流程的 coex plan，保证反复扫描和连接过程中无线协同状态不会被错误地耗尽或遗失。

ownership 合同：

```text
上层 NuttX socket buffer
        |
        | 协议栈 prepare/clone（IOB 固定池，位于 ram_wifi）
        v
dev->d_iob -> 驱动 txb / tx_pending
        |
        | wl80211_output：描述符写入 guard，ownership -> MAC
        | 在途达到上限：-EAGAIN，ownership 留在驱动
        v
异步 TX complete / fail / cleanup
        |
        +--> iob_free_chain，在途计数减一
        +--> txdone 钩子唤醒发送
```

任何失败路径都必须完成资源回收；任何完成都必须让等待发送者重新获得进展机会。

## 8. NuttX buffered-send 竞态修复

TCP 压测中曾出现：

- 对端仍在线，TCP 状态为 `ESTABLISHED`；
- TX queue 为 0，IOB 和 write buffer 均有空闲；
- 发送线程的 send semaphore 为 `-1` 并持续等待。

根因是 NuttX `net_sem_timedwait2()` 在释放网络锁后、真正进入 semaphore wait 前存在 lost wakeup 窗口。ACK 或发送完成事件如果在该窗口到达，旧实现检查到 semaphore 值为 `0`，不执行 post；发送线程随后进入等待，可能永久卡住。

NuttX `97496437931` 对以下四类 buffered send 的 notify 条件统一从 `val < 0` 改为 `val < 1`：

- CAN；
- PKT；
- TCP；
- UDP。

这是 Vela 上游 `8ff84f72e27` 的修复实现，本地采用同一方案，而不是保留只修 TCP 的临时补丁。最终 TCP/UDP 双向 500 秒压力验证未再出现该挂死。

## 9. RF 参数和 BL616CL 特定适配

### 9.1 PHYRF

vendor wl80211 wrapper 将 BL616CL PHYRF include 和预编译库加入 core/final link。PHYRF 必须与芯片、工具链和 SDK 基线匹配，不能仅按库文件名替换。

### 9.2 Flash OTP rfparam

`components/wireless/rfparam/CMakeLists.txt` 从 drivers 树选择以下源文件，生成 `bl_rfparam` 库（`BL_COMPONENT_RFPARAM`，由 `BL_COMPONENT_WL80211` select）：

- `rfparam_adapter.c`；
- `rfparam_bl616cl_flash_otp.c`；
- `rfparam_rftlv.c`。

同时对 rfparam 源强制包含 `include/bl616cl_rfparam_preinc.h`，解决 BL SoC 头文件中的 `ERROR` 枚举与 NuttX `sys/types.h` 冲突；`include/log.h` 把 rfparam 的 `LOG_*` 映射到 NuttX 无线日志，并加入 LHAL flash include 路径。drivers 是上游子仓，这两个 shim 不放进 drivers。按 AGENTS §8.3，drivers 源码原则上由 `cmake/*.cmake` 选择；rfparam 是 Wi-Fi（以及后续 BLE）共用的 RF 参数层，因此作为例外放在 `components/wireless/rfparam`，随无线组件一起自动发现。最初由 vendor `253c8a0` 加在 wl80211 glue 中。

`rfparam_bl616cl_flash_otp.c` 负责 BL616CL Flash OTP 记录、CRC、trim/power offset 和 MAC slot 的读取定义，使运行时能够使用 BL616CL 对应的 RF 校准参数来源。

`chips/bl616cl/bl616cl_rfparam_ext.c` 提供当前 adapter 所需的扩展接口。天线增益目前由适配层记录；country setter 对尚未接入的 BL616CL rfparam 流程明确返回 `-EOPNOTSUPP`，避免静默成功。

## 10. 配置方案

### 10.1 最简 `nsh`

`boards/bl616cl/ai-m64l-32s-kit/configs/nsh/defconfig` 恢复为 Wi-Fi 接入前的最简配置。它不应包含：

- `CONFIG_BL_COMPONENT_MACSW`；
- `CONFIG_BL_COMPONENT_WL80211`；
- `CONFIG_BL_COMPONENT_WPA_SUPPLICANT`；
- `CONFIG_DRIVERS_WIRELESS` / `CONFIG_DRIVERS_IEEE80211`；
- `CONFIG_NETDEV_WIRELESS_IOCTL`；
- `CONFIG_WIRELESS_WAPI`；
- `CONFIG_NETUTILS_DHCPC` / `CONFIG_NETUTILS_IPERF`；
- Wi-Fi 专用 IOB、TCP/UDP write-buffer 配置。

### 10.2 独立 `wifi`

`boards/bl616cl/ai-m64l-32s-kit/configs/wifi/defconfig` 承载完整 STA 目标，包括：

- `CONFIG_BL_COMPONENT_MACSW=y`；
- `CONFIG_BL_COMPONENT_WL80211=y`；
- `CONFIG_BL_COMPONENT_WPA_SUPPLICANT=y`；
- WAPI、WEXT、无线 driver；
- DHCP、DNS、IPv4、TCP/UDP 和 buffered write；
- `CONFIG_NETUTILS_IPERF=y`；
- `CONFIG_IOB_NBUFFERS=60`、`CONFIG_BL616CL_WRAM_SIZE=102`：池和 WRAM 一起缩小，系统堆比池 90、WRAM 128K 时多约 26 KiB，吞吐代价见 12.7；
- `CONFIG_NET_TCP_SELECTIVE_ACK=y`（会 select `NET_TCP_OUT_OF_ORDER`）、`CONFIG_NET_TCP_OUT_OF_ORDER_BUFSIZE=4096`：TCP RX 受接收侧丢帧限制；有了乱序队列，丢帧后已收到的段会保留，只需重传缺的那段。实测 TCP RX 从 7.0–7.6 升到 11.3–12.1 Mbps，TCP TX 从 13.1 降到 12.7，堆少 448 B。OOO 取 4K 是为了满足 NuttX 小内存建议中的 SEND+RECV+OOO < IOB 总量（16K+16K+4K < 38.4K）；实测 4K 与 8K 效果相同；
- `CONFIG_BL616CL_WLAN_RX_ZEROCOPY=y`：≥500 B 的 RX 帧不拷贝，直接把 host RX 槽交给协议栈，见 5.3，效果见 12.10；
- `CONFIG_NETDEV_STATISTICS=y`：驱动统计收发帧数、按类型的 RX、驱动丢帧、TX 错误与超时，`cat /proc/net/wlan0` 查看；过载时据此区分驱动丢帧与 socket 丢帧（`/proc/net/stat`）。代价 flash +1.2 KB、RAM +128 B；
- `CONFIG_NSH_READLINE=y`；
- `CONFIG_READLINE_TABCOMPLETION=y`；
- `CONFIG_READLINE_CMD_HISTORY=y`；
- `CONFIG_BL616CL_TRNG=y`、`CONFIG_DEV_URANDOM=y`：由 `BL_COMPONENT_WPA_SUPPLICANT` 在 Kconfig 中 `select`，不写在 defconfig 中；`DEV_URANDOM` 默认 `DEV_URANDOM_ARCH`，同时启用 `/dev/random`。

此前 `1c95e7b feat(bl616): enable WiFi in nsh` 将 Wi-Fi 选项错误地加入 `nsh`。本次配置收尾已恢复 `nsh`，并将 Wi-Fi、iperf、Tab 补全和命令历史集中到 `wifi/defconfig`。defconfig 应继续通过 menuconfig/savedefconfig 生成，不能直接维护生成的 `.config`。

### 10.3 所有配置共用

`nsh`、`nsh-peripherals`、`ostest`、`wifi` 都打开 `CONFIG_MEMCPY_VIK=y`。不开时 NuttX 的 `memcpy()` 是逐字节循环，在非 cache 的 `ram_wifi` 上每个字节都是一次总线访问。`CONFIG_RISCV_MEMCPY` 只有在源和目的对齐方式相同时才按字拷贝，而 RX 拷贝的源和目的分别是 4 字节对齐和余 2，所以不采用。

四个配置都打开 `CONFIG_ALLOW_BSD_COMPONENTS=y`，它会默认启用 `CONFIG_LIBC_STRING_OPTIMIZE`，memset、memcmp、memchr、strlen、strcmp 等随之换成 newlib 的字宽实现；memcpy 仍用 VIK，因为 CMake 中 VIK 优先。olddefconfig 只多出这两项，wifi 镜像 flash 增加约 1 KB。

`wifi` 打开 `CONFIG_NET_ARCH_CHKSUM=y`，校验和由 `chips/bl616cl/bl616cl_chksum.c` 提供。实现方法是按 4 字节对齐的 32 位字累加，最后折叠并交换字节序。按 NuttX 的约定，这个文件同时提供 `checksum()`、`chksum()`、`net_chksum()`、`ipv4_chksum()` 以及 IPv4/IPv6 的上层封装。

在 TCP 收发时于非 cache 的 IOB 上实测，`chksum_iob()` 的开销从 18–19 cycle/B 降到约 4.2 cycle/B；12 Mbps TCP TX 时约省下 6.5% 的 CPU。

NuttX 需要带上 `fix(net): declare checksum() for NET_ARCH_CHKSUM`。否则 `net_chksum.c` 中的 `chksum_iob()` 调用 `checksum()` 时没有声明，会产生隐式声明告警。

四个配置都使用 TLSF 堆管理器（`CONFIG_MM_TLSF_MANAGER=y`），malloc/free 为 O(1)。每个堆多一个约 3.2 KB 的控制块，malloc 只保证 4 字节对齐（E907 已打开硬件非对齐访问）。实测堆总量减少 3,372 字节（nsh 230,944→227,572，wifi 167,072→163,700）；wifi 的 UDP TX 由约 17 Mbps 升到约 25 Mbps（两次），TCP 吞吐在波动范围内。TLSF 源码是 manifest 中的 `nuttx/mm/tlsf/tlsf` 项目，目录缺失时 NuttX CMake 会改从 GitHub 拉取，离线环境先用 `repo sync -l nuttx/mm/tlsf/tlsf` 从本地对象恢复。

四个配置启动时都把 XIP flash 时钟从 XTAL 40 MHz 提到 80 MHz。`bl616cl_flash_initialize()` 在 `bflb_flash_init()` 之后移植了原生 `board_set_flash_hs(GLB_SFLASH_CLK_MUXPLL_80M)`：关闭 XIP，按 29 档 WIFIPLL 频率扫描 flash 时钟，每档读 boot2 头并校验 CRC，找到采样失效点；再按它落在 1.5T、1T 还是两者之间，设置 `clk_delay`、`rx_clk_invert` 和 pad 延时，切到 MUXPLL 80M。没有合适的窗口时恢复启动时的配置。测试模组的校准值为 55（1T 窗口），20 次复位结果一致，`GLB_SF_CFG0` 由 `0x4800` 变为 `0xd800`。

校准期间 XIP 关闭，经过的代码和常量都必须在 RAM 里：`ld.script` 把 `bl616cl_glb.c`、`bl616cl_clock.c` 的 `.rodata`（`switch` 跳转表和查表）放进 `.ram_rodata`，与原生链接脚本相同；这些源文件、LHAL flash 驱动和 `bl616cl_flash.c` 不做 sanitizer 与 stack protector 插桩（见 `bl616cl-kasan.md`）。RAM 代价：wifi、nsh、ostest 的 `.ram_code` 多 1.6 KB、`.ram_rodata` 多 1.0 KB，wifi 堆 213,860→211,236，ostest 294,068→291,380；nsh-peripherals 的 RAM 代码因为去掉插桩反而少了约 5 KB，堆 4,290,044→4,295,128。

两个布局相同、只差这一开关的 wifi 镜像交替测两轮（每轮 3×20 秒，中位数，Mbps）：TCP TX 13.2→20.4，UDP TX 22.3→37.7，TCP RX 12.1→18.3；UDP RX 以 60 Mbps 灌包时由 17.0 升到 36.6。I-cache 缺失率不变（1.2%–1.5%），提升来自每次缺失时 flash 读得更快。正式 wifi 镜像回归 PASS，3 轮 TCP TX 19.9–20.0、UDP TX 35.6–36.6、TCP RX 17.0–18.1。

WPA3-SAE 必须有 `/dev/urandom`。supplicant 的 `crypto_ec_point_mul()` 用 mbedTLS `ctr_drbg` 做 EC 点乘，其种子来自 `mbedtls_entropy_func()`；在 NuttX 上它经 `getrandom()` 读取 `/dev/urandom`。缺少该节点时 SAE commit 构造失败，串口打印 `wpa3 build sae pkt failed`，连接以 Authentication failure 结束。supplicant 的 `os_get_random()`（WPA2 SNonce、SAE 随机数）同样经 `getrandom()` 读取 `/dev/urandom`，因此 WPA2 也依赖该节点。`BL616CL_TRNG` 由 chip TRNG adapter 提供 `/dev/random` 和 `/dev/urandom`，详见 `bl616cl-trng.md`。

## 11. 构建流程

标准入口是 vendor 的 `vela`，使用 CMake + Ninja：

```bash
# 最简 NSH
vendor/bouffalolab/vela build \
  bl616cl/ai-m64l-32s-kit/configs/nsh -j14

# Wi-Fi STA
vendor/bouffalolab/vela build \
  bl616cl/ai-m64l-32s-kit/configs/wifi -j14
```

macsw 与 wl80211 core 以 `-flto -ffat-lto-objects` 编译，`CONFIG_ALLSYMS` 又使最终固件链接四次（`nuttx`、`first_link`、`second_link`、`final_nuttx`），每次都会重做 LTRANS。`components/wireless/wifi/CMakeLists.txt` 在启用 macsw 或 wl80211 时给链接加 `-flto=auto`，让各 LTRANS 分区并行，单次链接由约 7.8 s 降到约 3.9 s，生成固件不变；`nsh` 不受影响。

推荐的实现顺序：

1. 通过 manifest 拉取并确认四个无线子仓；
2. 先独立构建 macsw，确认 `bl616cl/default` profile 和工具链参数；
3. 用 wl80211 private 子构建生成 `libwl80211_bl616cl.a`；
4. 在 vendor wrapper 中链接 PHYRF、macsw、wl80211 core 和 supplicant；
5. 编译 public NuttX host port 与 `chips/bl616cl` 的 adapter/netdev；
6. 检查 linker map 中的 `ram_wifi`、`.wifibss` 和各独立 archive；
7. 检查 `nm -u final_nuttx` 无未解析符号；
8. 分别构建 `nsh` 和 `wifi` 配置，确认配置隔离。

## 12. 验证结果

### 12.1 构建和链接

验证涉及四类镜像，结论按镜像区分：

- **中间镜像**：PHYRF 2.3.35/Flash OTP rfparam、TX 背压和 NuttX 修复之前，由 `nsh` 目标构建。
- **拆分前最终镜像**：包含上述全部修复，由 `nsh` 目标构建，SHA-256 `e9789748…0396e9`。
- **`wifi` 镜像（TRNG 前）**：配置拆分后由 `wifi` 目标构建。与拆分前最终镜像相比，多了 readline/Tab 补全/命令历史（NSH 行编辑器由 CLE 改为 readline），并修正了 `bl616_wlan.h` 的声明条件；`nuttx.bin` SHA-256 `21dfb58a…ffcdbf`。
- **当前 `wifi` 镜像**：在上一镜像基础上启用 `BL616CL_TRNG` 和 `DEV_URANDOM`；`nuttx.bin` SHA-256 `903ed686…6bf5b0d`。

拆分前最终镜像：

- CMake/Ninja 全量构建通过；
- `libmacsw_bl616cl.a`、`libwl80211_bl616cl.a`、`libbl_wpa_supplicant.a` 均进入最终链接；
- `nm -u final_nuttx` 无 unresolved symbols；
- 固件启动后出现 `NuttShell (NSH)` / `nsh>`，无 panic；
- `ram_wifi` 和 `.wifibss` 预算通过。

`wifi` 镜像（TRNG 前）及最简 `nsh`：

- `wifi` clean 后全量构建通过（Ninja 1713/1713），0 error，无隐式函数声明告警；
- `libmacsw_bl616cl.a`、`libmacsw_config_bl616cl_default.a`、`libwl80211_bl616cl.a`、`libbl_wpa_supplicant.a` 均进入最终链接，`nm -u final_nuttx` 无输出；
- `ram_wifi` 起始 `0x21000000`、大小 `0x20000`，`.wifibss` 使用 `0x1ab30`，剩余 21,712 字节，与拆分前一致；
- 最简 `nsh` 构建通过，`nsh/.config` 不含 Wi-Fi、WAPI、DHCP、iperf 和 readline 历史/Tab 配置；
- 1 Mbps UART 烧录后主机与设备端 SHA 一致，2 Mbps 控制台启动到 `nsh>`，无 panic 或重复启动。

当前 `wifi` 镜像：构建通过，0 error，`nm -u final_nuttx` 无输出，TRNG 与 random 设备注册符号已链接，`.wifibss` 仍为 `0x1ab30`；1 Mbps 烧录 SHA 一致，启动到 `nsh>` 无 panic。

### 12.2 STA 功能

拆分前最终镜像：

- Open AP 扫描，发现目标 AP；
- Open AP 关联，carrier 到 `RUNNING`；
- DHCP 获取地址 `192.168.50.211`，网关 `192.168.50.1`；
- 网关 ping `10/10`。

当前 `wifi` 镜像，`ax86u` 切换为 WPA3 后：

- WPA3-SAE 单轮：SAE commit/confirm 完成，`wpa auth success`，DHCP 获取 `192.168.50.211`，网关 ping `10/10`，主机 `192.168.50.200` 可达；
- 连接、DHCP、断开 100 轮全部成功，每轮 supplicant 均选择 WPA3。

`wifi` 镜像（TRNG 前）：

- 扫描到 35 个 AP，包含 `vela`；
- WPA2/RSN（PTK CCMP）关联 `vela`，carrier 到 `RUNNING`；
- DHCP 获取地址 `192.168.31.132`，网关 `192.168.31.1`；
- 网关 ping `10/10`；
- 断开后 carrier 退出 `RUNNING`，全程无 crash 或重启标记；
- 同一镜像对 WPA3 AP 连接失败（`wpa3 build sae pkt failed`），原因见 10.2。

两台测试路由器都已改为 WPA3，WPA2 未在当前镜像上复测。

中间镜像：

- WPA2-PSK 单轮关联、DHCP、网关 ping `10/10`；
- 扫描 500 轮全部成功；
- 连接 `vela`、DHCP、断开 500 轮全部成功。

500 轮扫描和 500 轮连接尚未在包含全部修复的镜像上重跑。

### 12.3 TCP/UDP 压力

拆分前最终镜像在 open AP 网络（`192.168.50.x`）上完成四个方向的约 500 秒压力，当前 `wifi` 镜像未重跑：

| 方向 | DUT 数据量/时长 | 结果 |
| --- | --- | --- |
| TCP TX | 767,295,488 Bytes / 500.26 s | PASS |
| UDP TX | 920,984,768 Bytes / 500.16 s | PASS |
| TCP RX | 365,690,916 Bytes / 500.34 s | PASS |
| UDP RX | 65,553,180 Bytes / 510.10 s | PASS |

每个方向均有 50 个连续、非零的约 10 秒窗口。日志中未发现 `MAC transmission failed`、`Wi-Fi TX failed`、`ASSERT REC`、panic、hardfault 或 watchdog 标记。

TCP TX 初次脚本失败是 UART 输出一行缺失字符造成的 parser 假阴性，原始失败证据和离线复核已保留在任务目录，不影响最终判定。

当前 `wifi` 镜像在 WPA3 `ax86u` 上完成四个方向各 100 秒压力：

| 方向 | DUT 数据量/时长 | 结果 |
| --- | --- | --- |
| TCP TX | 144,703,488 Bytes / 100.05 s | PASS |
| UDP TX | 184,987,712 Bytes / 100.03 s | PASS，主机丢包 0% |
| TCP RX | 64,618,532 Bytes / 110.05 s | PASS |
| UDP RX | 13,124,160 Bytes / 110.02 s | PASS |

DUT 和主机每个方向都有连续非零的 10 秒窗口，未发现 `MAC transmission failed`、`Wi-Fi TX failed`、`ASSERT REC` 或 crash 标记。UDP RX 由主机 iperf2 以默认 1 Mbps 发送。

### 12.4 TX 零拷贝镜像

`wifi` 零拷贝镜像（`nuttx.bin` SHA-256 `a2360d0e…d3524b`）在屏蔽箱内的 WPA3 `ax86u` 上验证：

- clean 全量构建通过，警告与复制方案镜像相同；`g_iob_buffer` 位于 `.wifibss`，TX pool 符号不再存在；最简 `nsh` 构建通过；
- WPA3 单轮连接、10 轮连接/DHCP/断开、30 轮扫描均 PASS；
- iperf 四方向各 30 秒和各 120 秒均 PASS，无 `ASSERT REC`；
- 同一次上电内 UDP、TCP 各饱和发送 60 秒后 `/proc/iobinfo` 空闲 90/90，`g_tx_inflight` 为 0；UDP 发送中途断开再重连后同样回到 90/90、在途 0，重连后发送恢复满速。

同一环境 30 秒吞吐对比（Mbps，主机端）：

| 方向 | 复制方案 | 零拷贝 | 零拷贝 120 秒 |
| --- | --- | --- | --- |
| TCP TX | 11.3 | 11.2 | 11.1 |
| UDP TX | 14.1 | 15.7 | 15.5（丢包 0%） |
| TCP RX | 5.09 | 3.31 | 4.00 |
| UDP RX | 1.05 | 1.05 | 1.05 |

TCP RX 在同一套复制代码上也曾测得 3.51–5.09，差异在波动范围内。测试中未观察到在途帧达到上限。

### 12.5 既有缺陷修复

`wifi` 镜像（vendor `f908732`，public `1f98b57`，private `d189124`）在同一模组上重跑 WPA3 单轮、10 轮连接、iperf 四方向各 30 秒、30 轮扫描，全部 PASS；MAC 为模组出厂地址 `c8:e7:13:7e:0c:11`（与原生 SDK 读到的一致）。

| 缺陷 | 修复 | 实板验证 |
| --- | --- | --- |
| TX 看门狗从未启动，完成唤醒丢失时帧滞留 | 驱动留帧时启动 1 秒 `txtimeout` | 注入 `-EAGAIN` 并屏蔽 txdone：修复前 3 次中 2 次 4 秒内未发出、1 次靠其他 RX 发出；修复后 3 次均在 1007 ms 发出并打印 `tx timeout` |
| rxb 入队失败时 RX buffer 双重释放 | 释放后置空 `free_cb` | 每 32 帧注入一次入队失败并在 private 打开 `INVARIANTS`：修复后 20 秒 TCP RX 注入 41 次无告警、IOB 回到 90/90；模拟修复前首次注入即报 `RX buf 4 is already FREE` |
| `wlan_rxpoll` 首次发送不持 `net_lock` | 发送前加锁 | 回归与 iperf 通过（竞态窗口无法稳定注入） |
| 连接超时返回正数被当成功，状态码泄漏为 `-status` | 超时返回 `-ETIMEDOUT`，认证类 `-16`，其他 `-EIO` | 未知 SSID 返回 5（EIO）、错误密码返回 16；超时路径代码审查 |
| 扫描忙时返回 -1（`-EPERM`） | `nxsem_trywait()`，返回 `-EBUSY` | 后台扫描时再扫描返回 16 |
| STA 密码堆指针可被并发释放 | 改为定长数组，密码 ioctl 持锁 | 错误密码、断开事件后重新设置密码并连接、ping 通过 |
| 扫描结果 ESSID 按 32 字节越界读 | 按实际长度复制 | 扫描结果 ESSID 正确 |
| 设置 ESSID 长度未检查 | 超过 32 返回 `-EINVAL` | wapi 截断到 32，只验证 32 字节边界可用 |
| 兜底 MAC 前缀写成转义文本（`5c:78:30`） | 改为 `02:e0:4c` | 出厂 MAC 修复前的镜像显示 `02:e0:4c:00:01:02` |
| MAC 读取用 BL616 efuse 布局，所有 BL616CL 板都落到兜底地址 | 改用 `mfg_media_read_macaddr_with_lock()` | 显示出厂地址 `c8:e7:13:7e:0c:11` |
| 原生 `lwip.c` raw 发送失败时先调回调、调用者再释放 | 失败路径清除回调 | 原生 macsw_bare 注入 M4 发送失败：修复前 `tlsf_free` 断言 `block already marked as free`；修复后 3 次连接均成功、ping 4/4 |
| RX 用节流分配 IOB，TCP 写缓冲占到节流线后丢掉所有 RX（包括 ACK），连接永久卡死 | 改为非节流分配，与 netdev_upperhalf 相同 | 两个并发 TCP 发送：修复前第 1 轮卡死（nfree 24/90，ping 不通）；修复后 3/3 轮通过，IOB 回到 90/90 |

### 12.6 memcpy 优化

同一模组、同一 `ax86u`，30 秒 iperf（Mbps，主机端）：

| 方向 | 逐字节 memcpy | `MEMCPY_VIK` |
| --- | --- | --- |
| TCP TX | 12.2 | 13.6–13.7 |
| UDP TX | 14.2 | 17.1 |
| TCP RX | 4.5 | 6.3–7.3 |
| UDP RX（主机 `-b` 限速逐档加压） | CPU 在约 10.6 时跑满，发 30M 时活锁 | 29.8，丢包约 0.7% |

RX 路径中每帧从 host 槽拷到 IOB 的开销，从 3.0–3.8 万 cycle 降到约 1.44 万 cycle（CPU 320 MHz）。

已知问题：主机不限速、连续多轮 TCP RX 时，MAC 会停止接收，最后以 `ap beacon loss` 断开。开 VIK 之前的镜像也能复现；开 VIK 后，4 次回归中有 2 次在 TCP RX 一步触发。原因未定位，暂缓处理。

### 12.7 WRAM 与 IOB 池缩小

四个配置都用 TLSF，`free` 显示的堆总量（字节）：

| 配置 | WRAM 128K | 调整后 | 验证 |
| --- | --- | --- | --- |
| `nsh` | 227,572 | 293,108（WRAM 64K） | 启动、`free`、`ps`；临时加 ramtest，对 0x60fcae84 起 275,000 字节做 32 位和 8 位测试，覆盖新增的 0x61000000–0x6100e0bc，全部通过 |
| `ostest` | 216,948 | 282,484（WRAM 64K） | 退出码 0 |
| `nsh-peripherals` | 4,213,724 | 4,277,212（WRAM 64K） | 启动、`free`、`ps`；KASAN 影子区占去新增部分中的 2 KiB |
| `wifi` | 163,700 | 190,564（WRAM 102K、池 60） | 连接后空闲 146,844→173,752；WPA3 单轮、10 轮连接、iperf 四方向各 30 秒 PASS；两个并发 TCP 发送 3/3 轮通过，IOB 回到 60/60 |

`wifi` 池 90 与池 60 的 30 秒 iperf（Mbps）：

| 方向 | 池 90 | 池 60 |
| --- | --- | --- |
| TCP TX | 13.3 | 12.3 |
| UDP TX | 24.8、25.0 | 19.1、26.8、23.2 |
| TCP RX | 7.93 | 8.53 |
| 两个并发 TCP TX 合计 | 约 12–13 | 约 11.9 |

池 60 时 UDP TX 最多占用 36 个 IOB，正好是池减去节流线 24，说明发送深度受池限制。池 60 的三个 UDP TX 数据来自三个镜像：本节 R1 镜像，以及其后只改 `g_allsyms` 段位置、只开字符串优化的两个镜像；后两项改动不涉及发送路径。三次结果在 19–27 Mbps 之间，所以还不能确定池 60 相对池 90 的 UDP TX 代价，需要多次重复测量。

### 12.8 RX 硬件缓冲与 macsw profile

原生默认配置（`CFG_AMSDU_4K`、`CFG_RXL_BUFFER1_AMSDU_CNT` 1）下，MAC 的 RX 硬件缓冲 `rxl_hw_buffer1` 只有 8,504 B。TCP RX 不限速时，它最高填到 8,460 B，20 秒内 MAC 因 RX FIFO 溢出丢掉约 10% 的 MPDU（MIB `rd_fifo_overflow_count`）；host 侧空闲 RX 缓冲最少仍有 15/26 个，没有耗尽。

`vela_bl616cl` profile 因此把 `CFG_RXL_BUFFER1_AMSDU_CNT` 改为 2（缓冲 17,008 B），同时把 `CFG_REORD_BUF` 从 12 改为 8。host RX 缓冲数按 `CFG_BARX × CFG_REORD_BUF + 2` 计算，从 26 个降为 18 个，所以 `.wifibss` 反而少 4,400 B，WRAM 仍为 102K，堆不变。`CFG_AMSDU_8K` 也能加大缓冲，但它同时向 AP 宣告 7935 B 的 A-MSDU，UDP RX 下降约 17%，所以没有采用。

同一时段、同一 AP 的 3×20 秒 iperf（Mbps，UDP RX 为主机 `-b 60M` 过载发送；括号内为主机 TCP 重传次数）：

| 镜像 | AP | TCP TX | UDP TX | TCP RX | UDP RX |
| --- | --- | --- | --- | --- | --- |
| 原生默认 | WPA3 | 20.1–20.2 | 35.6–36.6 | 17.4–17.7（309–396） | 30.2–30.8 |
| `vela_bl616cl` | WPA3 | 20.1 | 35.5–36.5 | 20.1（7–60） | 24.0–26.5 |
| 原生默认 | open | 20.4–20.6 | 37.1–38.4 | 18.1–18.5（278–329） | 32.3–32.4 |
| 只改缓冲（REORD 12，WRAM 110） | open | 20.2–20.6 | 37.3–38.4 | 20.6–20.8（0–17） | 31.1–31.6 |
| 只改 REORD 8 | open | 20.4–20.5 | 37.4–38.2 | 19.6–20.2（135–219） | 26.2–26.6 |
| `vela_bl616cl` | open | 20.4–20.5 | 36.9–38.1 | 20.6–20.7（1–51） | 28.1–28.3 |

- 缓冲加倍后，MAC 溢出几乎为零（诊断镜像中 TCP RX 0 次，UDP RX 60M 1 次），TCP RX 提高约 13–14%，主机重传从约 300 次降到几十次以内。
- UDP RX 过载时均值下降 13–18%，来自 `CFG_REORD_BUF` 8：只改缓冲时仅降约 3%。主机按 30M 发送时，`vela_bl616cl` 收满 29.95–29.98，与原生默认相同，所以这只影响过载时的吞吐。
- 改 REORD 会改变 `rxu_cntrl_reord_*` 的代码大小，macsw 的 358 个函数随之移位；带 `mw` 命令的另一种布局下，两次测得 UDP RX 过载时分别下降 0% 和 11%。窗口本身和代码布局各占多少，还没有区分。
- 单轮 WPA3 回归中，connect-10、iperf-30s 通过；single 因 ping 9/10 判为失败：seq 2 的回复超过 1 秒才到。2026-09-02 旧配置的 `getrandom-wpa3-ax86u-single.log` 中也出现过同样现象（同为 seq 2），不是本次改动引入的。

### 12.9 Wi-Fi 热函数布局

同一 open AP、每个镜像 3×20 秒（Mbps，UDP RX 为主机 `-b 60M` 过载发送）。B1 只把 `.text` 起点对齐到 32 KiB，W1 再加 Wi-Fi 热函数排序；两者都在 Wi-Fi 代码之后插入 0、0x1e0、0x9a0 字节的填充，把其余代码整体挪位，以模拟无关改动。这些实验镜像都带采样补丁。

| 场景 | B1（三种填充） | W1（三种填充） |
| --- | --- | --- |
| TCP TX | 18.0–20.1 | 20.4–21.1 |
| UDP TX | 34.1–35.0 | 41.0–46.0 |
| TCP RX | 19.2–20.5 | 20.9–21.5 |
| UDP RX | 28.8–33.0 | 39.8–47.8 |

- 三种填充下 W1 都更好，按中位数计：TCP TX +2%～+14%，UDP TX +20%～+32%，TCP RX +4%～+9%，UDP RX +24%～+46%。
- I-cache 缺失率：UDP TX 从 1.50% 降到 1.22%，TCP RX 从 1.28% 降到 1.15%。
- Wi-Fi 中断（`interrupt0_handler`）每次的耗时，在 UDP TX 时从约 16.7k 降到 9.3k cycle。这部分时间 tick 采样看不到，是在 IRQ 分发处按 mcycle 统计得到的。
- W1 的 UDP 吞吐仍随其余代码的位置变化，说明 net、memcpy、调度代码的位置还有影响，需要按模块继续排列。

正式镜像（不带采样补丁）与上一版 `wifi`（`vela_bl616cl` profile，同为 open AP）对比如下，单位 Mbps：

| 场景 | 排序前 | 排序后 |
| --- | --- | --- |
| TCP TX | 20.4–20.5 | 20.3–20.4 |
| UDP TX | 36.9–38.1 | 47.8–48.0 |
| TCP RX | 20.6–20.7 | 21.1–21.2 |
| UDP RX | 28.1–28.3 | 41.1–41.2 |

- 堆仍为 211,364 字节。对齐只多占 flash：`wifi` 的 `nuttx.bin` 从 747,552 增加到 750,304 字节。
- open AP 下四个方向 iperf 各 30 秒通过。
- `nsh`、`nsh-peripherals` 各启动 3 次通过，后者启动仍需 21.5 秒；`ostest` 退出码为 0。
- AP 改回 WPA3 之前，没有跑 WPA3 回归。

之后用回归工具 `tools/bl616cl/perf/layout_ab.sh -b` 在打开 perfmon 的 `wifi` 上重跑一次：`hot` 为当前列表，`base` 删掉列表，其余相同，每个镜像 3×20 秒，表中为中位数（Mbps）。

| 场景 | base（填充 0 / 0x1e0 / 0x9a0） | hot（填充 0 / 0x1e0 / 0x9a0） |
| --- | --- | --- |
| TCP TX | 19.9 / 19.9 / 20.2 | 20.9 / 21.1 / 20.7 |
| UDP TX | 35.1 / 35.4 / 36.0 | 45.3 / 48.8 / 48.5 |
| TCP RX | 20.4 / 20.3 / 20.8 | 21.4 / 21.5 / 21.6 |
| UDP RX | 32.5 / 33.5 / 33.6 | 41.1 / 41.5 / 38.3 |

三种填充下 `hot` 都更好：UDP TX +29%～+38%，UDP RX +14%～+26%，TCP +2%～+6%，满足 [bl616cl-hot-code-layout.md](bl616cl-hot-code-layout.md) 的验收标准。组件更新后按该文档重跑。

### 12.10 RX 零拷贝

同一 AP（open），`wifi` 打开（R2）与关闭（基线）`CONFIG_BL616CL_WLAN_RX_ZEROCOPY`，其余相同；按 12.9 的做法在 Wi-Fi 热函数之后插入 0、0x1e0、0x9a0 字节填充。每个镜像 3×20 秒，表中为中位数（Mbps，UDP RX 为主机 `-b 60M` 过载发送）：

| 场景 | 基线（填充 0 / 0x1e0 / 0x9a0） | R2（填充 0 / 0x1e0 / 0x9a0） |
| --- | --- | --- |
| TCP TX | 20.3 / 20.6 / 20.7 | 20.2 / 20.6 / 20.4 |
| UDP TX | 47.3 / 45.1 / 45.0 | 45.1 / 41.6 / 45.0 |
| TCP RX | 21.2 / 21.2 / 21.9 | 21.5 / 22.2 / 22.3 |
| UDP RX | 42.3 / 37.6 / 43.5 | 46.0 / 52.1 / 45.8 |

- 三种填充下 RX 都更好：TCP RX +1.4%～+4.7%，过载 UDP RX +5%～+39%。主机按 20M、30M 发送时两者都收满。
- TX 不经过零拷贝路径。TCP TX 相差 0～−1.4%；UDP TX 在两种填充下低 5%～8%，另一种持平。perfmon 镜像（布局又不同）中 R2 的 UDP TX 为 45.8、基线为 45.0，R2 的空闲还多 3.4 个百分点，所以 UDP TX 的差别按布局噪声处理，不是 CPU 开销。
- CPU（perfmon 镜像）：UDP RX 20M 时空闲从 75.7% 升到 81.5%，`libc-mem`（主要是 memcpy）从 6.9% 降到 0.8%；TCP RX 时空闲从 29.6% 升到 33.4%，`libc-mem` 从 15.5% 降到 10.7%。
- 探针镜像统计的 ≥500 B 帧包装比例：UDP RX 20M 为 99.9%，TCP RX 为 86%～87%，过载 UDP RX 为 47%～49%；其余是借出的槽满 8 个后的拷贝。主机 `ping -s 1400` 的 200 个 echo 都经发送前复制发出，没有丢包。
- 堆不变（211,364 字节）；`.wifibss` +720 B；`nuttx.bin` +384 B（750,688 字节）。`nsh`、`nsh-peripherals`、`ostest` 不开 `IOB_ALLOC`，clean build 通过。
- 调试镜像（wl80211 `INVARIANTS` + `DEBUG_ASSERTIONS`）跑完四个方向、ICMP、一个连接不读数据的 60 秒 TCP RX（借出的槽不超过 8 个，链路正常）和两个并发 TCP 发送，没有断言。
- 两个并发 TCP 发送偶尔卡住，R2 和基线都会出现：一个连接的写缓冲还剩约 3 KB，在途为 0，也没有重传定时器，数据不再发出。这是发送轮询丢失，与零拷贝无关，另行跟踪。
- 与设计相比有两处调整。一是借出槽的上限由 12 改为 8，因为 `vela_bl616cl` profile 把槽从 26 个减到 18 个。二是池空时不再包装短帧：首版这样做后，TCP TX 降到 16.7–18.0，原因是协议栈把下一个数据段建在包装 ACK 的槽上，发送前的复制又要用池 IOB，失败就丢帧重传。现在池空时照旧丢弃 ACK，TCP TX 与基线相同。
- 短帧与混合长度（主机按包率发送，每例 10 秒）：UDP 64、440、460、1000、1470 B，100 B 与 1470 B 混合，440 B 与 460 B 混合，R2 与基线都零丢失；调试镜像确认 440 B 载荷（帧 482 B）走拷贝、460 B 载荷（帧 502 B）走包装。TCP `-l 200 -N` 和 `-l 100 -N` 加普通流的混合也正常，混合时发送前复制 43 次，没有失败。
- 过载 UDP RX（1470 B，60M）时两者的 IP 层都收到全部报文，丢包都发生在 UDP socket 接收缓冲已满时：iperf 线程（优先级 100）在 `hpwork`（224）和 `wifi_fw`（127）忙时拿不到 CPU。R2 的应用多收 5.9%。`/proc/net/stat` 对这类丢包记两次。
- AP 改回 WPA3 后，R2 镜像的 WPA3 单轮、10 轮连接、iperf 四方向各 30 秒 PASS。

## 13. 后续发布门禁

在将本方案用于正式 SDK release 前，还应完成：

1. 将 manifest 的 floating `master` 转为冻结 revision/tag 或 release manifest，并把内部 gerrit 源码项目从公开 manifest 拆到内部 overlay；
2. 重新核对各组件许可、来源和对外同步策略；
3. 对 WPA3-SAE 补做 500 轮连接和 500 秒级四方向压力；
4. 明确 country code、天线增益和 RF calibration 扩展接口的产品行为；
5. 在新的 openvela/NuttX 基线或工具链变化后重新编译 PHYRF、macsw、wl80211 和 supplicant，并重新检查 ABI；
6. 保留最简 `nsh` 与独立 `wifi` 两个构建目标，避免测试工具和 Wi-Fi 组件重新回流到基础配置；
7. 在发布候选 `wifi` 镜像上重跑 500 轮扫描、500 轮连接和 TCP/UDP 四方向压力。

## 14. 证据索引

- 最终验收：`.tasks/2026-09-02-bl616cl-wifi-sta-porting/work/FINAL_ACCEPTANCE.md`
- `wifi` 镜像（TRNG 前）：`.tasks/2026-09-02-bl616cl-wifi-sta-porting/work/closure-wifi-{build,flash-1m,reboot,wpa2-usb3}.log`
- 当前 `wifi` 镜像：`.tasks/2026-09-02-bl616cl-wifi-sta-porting/work/wpa3-trng-wifi-{build,flash-1m,reboot}.log`、`closure-wifi-wpa3-ax86u-single.log`、`wpa3-connect-100.csv`、`wpa3-iperf-100s-01/`；TRNG 前的 WPA3 失败现场为 `closure-wifi-wpa3-single-01.log`
- 组件边界：`.tasks/2026-09-02-bl616cl-wifi-sta-porting/work/component-integration-contract.md`
- BL4 边界核对：`.tasks/2026-09-02-bl616cl-wifi-sta-porting/work/bl4-integration-boundary.md`
- 迁移记录：`.tasks/2026-09-02-bl616cl-wifi-sta-porting/work/repo-migration/MIGRATION.md`
- 原始调研：`vendor/bouffalolab/docs/bl616cl-wifi-sta-porting-research.md`
- 测试与串口证据：`.tasks/2026-09-02-bl616cl-wifi-sta-porting/work/`
- 既有缺陷修复与故障注入：`.tasks/2026-09-23-bl616cl-wifi-upstream-convergence/subtasks/ST005-existing-defects/work/README.md`
- RX 拷贝、memcpy 测量与断流复现：`.tasks/2026-09-23-bl616cl-wifi-upstream-convergence/subtasks/ST006-rx-copy-research/work/README.md`
- TLSF 切换的构建、启动、ostest 与回归：`.tasks/2026-09-23-bl616cl-wifi-upstream-convergence/subtasks/ST011-tlsf-allocator/work/README.md`
- WRAM 与 IOB 池缩小（R1）、IOB 用量与分配方案调研：`.tasks/2026-09-23-bl616cl-wifi-upstream-convergence/subtasks/ST009-iob-dynamic-zero-copy/work/README.md`
- 网络参数扫描（OOO/SACK、池大小、IOB 几何、代码布局敏感性）：`.tasks/2026-09-23-bl616cl-wifi-upstream-convergence/subtasks/ST010-net-throughput/work/README.md` 的 T5 各节
- 热点采样与 flash 80 MHz A/B、复位、KASAN 启动排查：同一 README 的“热点代码布局”各节，数据在 `work/prof/`、`work/fhs/`
- Wi-Fi 热函数布局实验、perfmon 验证与回归工具首次运行：同一 README 的“热点代码布局 Stage 2”和“perfmon 与布局回归工具”两节，数据在 `work/layout/`、`work/perfmon/`
- RX 硬件缓冲与 `vela_bl616cl` profile：同一 README 的“RX 硬件缓冲”两节，数据在 `work/rxdiag/`、`work/rxcfg/`
- Wi-Fi 热函数布局：同一 README 的“热点代码布局 Stage 2”一节，数据在 `work/layout/`
- RX 零拷贝（R2）的实现、A/B、探针与调试镜像：`.tasks/2026-09-23-bl616cl-wifi-upstream-convergence/subtasks/ST009-iob-dynamic-zero-copy/work/README.md` 的“R2 实施与验证”“R2 补充验证”两节，数据在 `work/r2/`
