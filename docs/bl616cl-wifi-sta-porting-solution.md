# BL616CL Wi-Fi STA 移植方案

## 1. 方案结论

本次工作将 BouffaloLab BL616CL 的 Wi-Fi STA 能力接入 openvela/NuttX，形成以下闭环：

```text
NSH / WEXT
    |
    v
BL616CL netdev glue
    |
    +--> wifi_mgmr: 扫描、关联、断开、状态
    |       |
    |       +--> wl80211 core archive
    |       |       |
    |       |       +--> net80211 / STA / scan / connect
    |       |       +--> macsw BL616CL/default
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
| `macsw` | Wi-Fi MAC/协议数据路径和 BL616CL/default 配置库 | `f9b9a8b4`，恢复单天线扫描间的 coex plan |
| `wl80211/public` | wl80211 对外头文件、macsw 接口和公共兼容层 | 当前本地 `7369603` |
| `wl80211/private` | wl80211/net80211 core，单独生成 `libwl80211_bl616cl.a` | 当前本地 `089097b` |
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
└── wifi/
    ├── macsw/
    ├── wl80211/
    └── bl_wpa_supplicant/
```

顶层只做一层 `nuttx_add_subdirectory()` 和 Kconfig 菜单生成。每个组件 wrapper 自己根据 `CONFIG_BL_COMPONENT_*` 选择是否接入，避免 vendor 顶层直接拉入所有无线源码。

### 4.2 macsw wrapper

`components/wireless/wifi/macsw/CMakeLists.txt` 的职责是：

1. 只接受 `CONFIG_ARCH_CHIP_BL616CL` 和 `CONFIG_MACSW_SELECT="default"`；
2. 进入 macsw 独立 CMake 工程，使用与 Vela 相同的交叉工具链；
3. 输出 `libmacsw_bl616cl.a` 和 `libmacsw_config_bl616cl_default.a`；
4. 删除 Vela GCC 不支持的 `-mtune=e907`、`-march=rv32imafc_xtheade`；
5. 使用标准 ISA `-march=rv32imafc_zicsr_zifencei`，并关闭不适用于该源码的局部告警；
6. 以 external kernel library 方式交给 NuttX 最终链接。

`default` 是 macsw 的功能/资源 profile，不是工具链或 ABI 标识。芯片、profile、工具链必须作为一组输入锁定。

### 4.3 wl80211 core 与 host glue 分界

这是本次集成的核心边界。

`components/wireless/wifi/wl80211/CMakeLists.txt` 不在 NuttX 编译环境中直接编译 private `src/net80211`。它执行独立的 CMake/Ninja 子构建：

```text
wl80211/wl80211/src
        |
        +--> libwl80211_bl616cl.a
```

子构建显式接收：

- `CHIP=bl616cl`；
- `CONFIG_MACSW_SELECT=default`；
- PHYRF include；
- macsw include；
- supplicant include；
- Vela 工具链。

vendor 主构建随后只消费该 archive，并额外链接 BL616CL PHYRF 库。这样隔离了 private core 的 SDK 侧 CMake、FreeRTOS/lwIP 假设和 NuttX 头文件差异。

host-side glue 保留在 vendor：

```text
components/wireless/wifi/wl80211/glue/
├── wifi_mgmr.c              # 管理面封装
├── country.c
├── nuttx.c                  # NuttX OS/网络接口
├── rtos_al_nuttx.c          # NuttX RTOS 抽象
├── bl616_wlan.c             # netdev/WEXT/收发回调
├── bl616_wifi_adapter.c     # BL616CL adapter 和控制流程
├── bl616cl_efuse_mac.c      # MAC 地址读取
├── bl616cl_macsw_lp.c       # 低功耗/平台 glue
└── bl616cl_rfparam_ext.c    # rfparam 扩展接口
```

这种划分对应 BL4 的实际结构：private core 作为库，平台 glue 在芯片/板级适配层编译。它避免将 NuttX 特定实现反向塞入 wl80211 private core。

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

## 5. BL616CL 平台 glue

### 5.1 启动和 netdev 注册

`boards/bl616cl/common/src/bl616cl_bringup.c` 在 `CONFIG_BL_COMPONENT_WL80211` 打开时调用：

```c
bl616_wlan_sta_initialize();
```

初始化顺序包括：

1. `bl616_wifi_adapter_init()` 建立 Wi-Fi adapter 和底层控制环境；
2. 读取 eFuse/MAC 地址；
3. `bl616_net_initialize()` 注册 BL616CL STA netdev；
4. 注册 RX callback 和 TX-done callback；
5. 根据配置启动后续 Wi-Fi 管理流程。

`bl616_wlan.c` 提供 NuttX 网络接口，设置 `d_txavail`，并通过 `netdev_register(netdev, NET_LL_IEEE80211)` 将设备纳入 NuttX 网络栈。carrier 状态由连接/断开事件更新。

### 5.2 控制面

NuttX WEXT/ioctl 请求在 `bl616_wifi_adapter.c` 中转换为 wl80211/wifi_mgmr 操作：

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

扫描结果在 glue 中转换为 NuttX/WAPI 所需的 SSID、BSSID、信道、RSSI、认证和 cipher 信息。`71bd08b` 补齐了 BL616CL 的 WEXT scan result 路径和相关 NuttX RTOS 辅助接口。

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
  -> wl80211/macsw TX pool
  -> Wi-Fi hardware

Wi-Fi hardware
  -> bl616_wlan RX callback
  -> NuttX IOB / netdev receive path
  -> TCP/UDP/IP
```

TX/RX 的关键原则是：

- NuttX socket 和 IOB 负责上层网络缓冲；
- wl80211/macsw 负责 Wi-Fi 帧、描述符和底层 TX pool；
- 跨硬件可见区域的数据必须遵守 shared RAM/cache ownership；
- 异步 TX 完成后由明确的 owner 回收资源；
- pool 暂时耗尽时不得丢失可重试的发送机会。

## 6. Shared RAM、cache 和 linker

BL616CL linker 在 `boards/bl616cl/ai-m64l-32s-kit/scripts/ld.script` 中为 Wi-Fi 保留独立的 `ram_wifi`：

```text
ram_wifi ORIGIN = 0x21020000 - 128K
         LENGTH = 128K
```

`.wifibss` 将以下对象放入 Wi-Fi 可见区域：

- `SHAREDRAMIPC` / `SHAREDRAM`；
- macsw 的 TX buffer/frame；
- scan/shared 数据；
- MFP、MIC 和其他 Wi-Fi shared/common 对象；
- `wifi_ram*` 区段。

最终验收镜像中：

- `ram_wifi` 起始地址为 `0x21000000`；
- 区域大小为 `0x20000`；
- `.wifibss` 使用 `0x1ab30`；
- 剩余约 21,712 字节。

wl80211 public/private 侧的配套修改包括：

- public `a769100`：适配 NuttX `sys/queue.h` / `sys/tree.h`；
- public `c315207`：保留 BSD queue/tree 兼容头；
- public `aa94781`：补充 supplicant API platform header；
- public `d229441`：统一 scan-result tree 布局；
- public `93e1af5`：在 TX descriptor 中携带 shared-RAM pool 引用；
- public `7369603`：暴露 TX pool 状态；
- private `b86b33e`：把 host TX frame 放入 Wi-Fi shared RAM；
- private `2fee8fe`：加固 shared TX pool；
- private `089097b`：pool 暂时不可用时进行 backpressure。

`wl80211.h` 强制使用统一的 tree layout，避免不同 translation unit 对 RB tree entry 的大小和布局理解不一致。这是扫描结果树跨 public header、private core 和 vendor glue 时的 ABI 约束。

## 7. TX 资源所有权与稳定性修复

vendor glue 的两笔修复和 wl80211 private 的一笔修复对应底层数据路径的三个问题：

- vendor `f112fa0 fix(wifi): correct STA TX ownership`：修正 STA TX buffer 在提交、异步完成和失败路径中的 owner 转移；
- vendor `62b724f fix(wifi): retry TX after pool release`：TX pool 释放后重新触发可发送路径，避免 pool 恢复后没有新的 poll/notify；
- wl80211 private `089097b fix(wifi6): backpressure TX pool`（`macsw/tx.c`）：pool 无空闲 slot 时返回 `-EAGAIN` 而不是丢帧，TX IOB 仍由发送方持有并等待重试。

macsw `f9b9a8b4` 则恢复 BL616CL 单天线跨扫描流程的 coex plan，保证反复扫描和连接过程中无线协同状态不会被错误地耗尽或遗失。

这些修复共同形成如下 ownership 合同：

```text
上层 NuttX IOB / socket buffer
        |
        | copy/prepare
        v
Wi-Fi shared TX pool
        |
        | submit: ownership -> hardware/macsw
        v
异步 TX complete / fail
        |
        +--> release/recycle
        +--> wake/retry waiting sender
```

任何失败路径都必须完成资源回收；任何 pool release 都必须让等待发送者重新获得进展机会。

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

vendor `253c8a0` 在 `wl80211/glue/CMakeLists.txt` 中加入：

- `rfparam_adapter.c`；
- `rfparam_bl616cl_flash_otp.c`；
- `rfparam_rftlv.c`。

同时对 rfparam 源使用 `bl616cl_soc_preinc.h`，解决 BL SoC 头文件中的 `ERROR` 枚举与 NuttX `sys/types.h` 冲突，并加入 LHAL flash include 路径。

`rfparam_bl616cl_flash_otp.c` 负责 BL616CL Flash OTP 记录、CRC、trim/power offset 和 MAC slot 的读取定义，使运行时能够使用 BL616CL 对应的 RF 校准参数来源。

`bl616cl_rfparam_ext.c` 提供当前 glue 所需的扩展接口。天线增益目前由适配层记录；country setter 对尚未接入的 BL616CL rfparam 流程明确返回 `-EOPNOTSUPP`，避免静默成功。

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
- `CONFIG_NSH_READLINE=y`；
- `CONFIG_READLINE_TABCOMPLETION=y`；
- `CONFIG_READLINE_CMD_HISTORY=y`；
- `CONFIG_BL616CL_TRNG=y`、`CONFIG_DEV_URANDOM=y`：由 `BL_COMPONENT_WPA_SUPPLICANT` 在 Kconfig 中 `select`，不写在 defconfig 中；`DEV_URANDOM` 默认 `DEV_URANDOM_ARCH`，同时启用 `/dev/random`。

此前 `1c95e7b feat(bl616): enable WiFi in nsh` 将 Wi-Fi 选项错误地加入 `nsh`。本次配置收尾已恢复 `nsh`，并将 Wi-Fi、iperf、Tab 补全和命令历史集中到 `wifi/defconfig`。defconfig 应继续通过 menuconfig/savedefconfig 生成，不能直接维护生成的 `.config`。

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

推荐的实现顺序：

1. 通过 manifest 拉取并确认四个无线子仓；
2. 先独立构建 macsw，确认 `bl616cl/default` profile 和工具链参数；
3. 用 wl80211 private 子构建生成 `libwl80211_bl616cl.a`；
4. 在 vendor wrapper 中链接 PHYRF、macsw、wl80211 core 和 supplicant；
5. 编译 BL616CL glue 和直接 netdev；
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
