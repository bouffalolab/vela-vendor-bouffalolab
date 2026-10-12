# BL616CL 硬件加密：/dev/crypto 与 mbedTLS ALT

本文说明 BL616CL 如何把安全引擎 SEC_ENG 的 AES、SHA 接入 OpenVela crypto 框架，
对用户态提供标准 `/dev/crypto`，并让 mbedTLS 经 ALT 层使用硬件。内容包括支持的
算法、配置、数据通路、mbedTLS ALT 的限制和实测结果。运行环境为
Ai-M64L-32S-Kit、`nsh-peripherals` 配置。

## 支持范围

驱动以 `hwcr_init()` 注册为 crypto 框架的硬件驱动，用户态经 `/dev/crypto`
（`CRIOGET`、`CIOCGSESSION`、`CIOCCRYPT`）访问。注册的算法：

| 算法 ID | 密钥 | 长度要求 | 说明 |
|---|---|---|---|
| `CRYPTO_AES_CBC`、`CRYPTO_AES_192_CBC`、`CRYPTO_AES_256_CBC` | 128/192/256 位，三个 ID 都接受任一长度 | 16 B 的整数倍 | 带 `COP_FLAG_UPDATE` 的请求从上一请求的末块密文续接 |
| `CRYPTO_AES_CTR` | 128/192/256 位 | 任意 | 计数器按 128 位进位；末尾不足一块也消耗一个计数器，续接请求从下一个计数器开始 |
| `CRYPTO_AES_XTS` | 256 或 512 位（两把 128 或 256 位密钥） | 16 B 的整数倍，不支持密文挪用 | 一个数据单元可以分多次 `COP_FLAG_UPDATE` 请求处理 |
| `CRYPTO_SHA1`、`CRYPTO_SHA2_224/256/384/512` | — | 任意 | 语义见下 |

与上游软件驱动一致的约定：

- 驱动不回写 `crp_iv`；分段处理同一条消息时，后续请求带 `COP_FLAG_UPDATE`，由
  session 保存的链值或计数器续接。
- 哈希请求带 `COP_FLAG_UPDATE` 时只追加数据、不写摘要；不带时只结束消息并写出
  摘要，本次请求的数据不参与哈希。摘要按算法写满 20、28、32、48 或 64 B，忽略
  `olen`。结束后同一 session 开始新消息。
- 一个 session 只能有一个算法，不支持 cipher 与 hash 串联。
- 请求同步完成。异步请求（`CRYPTO_F_CBIMM`）和按 key id 引用密钥返回 `ENOTSUP`。

CTR 与软件驱动的差异：软件驱动只在计数器低 32 位内进位，本驱动按 128 位进位。
计数器低 32 位在一次请求中回绕时，两者结果不同。硬件只自增低 32 位，驱动在回绕
处拆分请求并由 CPU 进位。

不注册的算法：`*_HMAC`（mbedTLS 用 SHA 自行组合 HMAC）、AES-CFB/OFB/GCM/GMAC、
CMAC、MD5、RIPEMD-160、Poly1305、3DES、CRC32，以及 PKA（`CRK_*`）。关闭软件
驱动时，对这些算法建 session 会失败。

CRC32 不注册的原因：SEC_ENG 的 CRC32 把多项式最低位固定为 1，算不出偶数多项式；
并且只按 4 B 整字处理，不足 4 B 的尾部被静默丢弃。框架 CRC32 接口和测试向量都需要
任意长度，硬件无法单独完成。

## 配置

| 选项 | 作用 | 默认 |
|---|---|---|
| `CRYPTO`、`CRYPTO_CRYPTODEV`、`CRYPTO_CRYPTODEV_HARDWARE` | 框架、`/dev/crypto` 与硬件驱动入口 | 关 |
| `BL616CL_CRYPTO` | 注册 SEC_ENG 驱动；依赖 `CRYPTO_CRYPTODEV_HARDWARE` | 开 |
| `BL616CL_CRYPTO_AES`、`BL616CL_CRYPTO_SHA` | 分别注册 AES、SHA 算法；关闭时对应源文件不编译 | 开 |
| `BL616CL_SEC_ENG_BOUNCE_SIZE` | 每个引擎的 non-cacheable bounce 缓冲大小，也是引擎一次处理的最大段，128 的倍数 | 1024 |
| `BL616CL_SEC_ENG_DIRECT` | 允许引擎直接读写调用者缓冲（见下节） | 开 |
| `BL616CL_SEC_ENG_DIRECT_MIN` | 直连的最小请求长度 | 256 |
| `CRYPTO_CRYPTODEV_SOFTWARE_CRYPTO` | 上游软件驱动 | 关，建议保持 |

软件驱动保持关闭：只要本驱动注册了 session 里的全部算法，框架就选本驱动，软件
驱动只会接手本驱动不注册的算法（如 HMAC、MD5），镜像却增加约 100 KB。同一 session
同时带 cipher 与 SHA 时，框架仍选本驱动，建 session 返回 `EINVAL`，不会回退到软件
驱动。

TRNG（`/dev/random`）与本驱动共用 SEC_ENG 时钟和锁，配置与验证见
[BL616CL TRNG](bl616cl-trng.md)。

## 数据通路

SEC_ENG 不感知 CPU 数据 cache。驱动用两种方式把数据交给引擎：

- **bounce**：每个引擎在 non-cacheable RAM 里有一个 bounce 缓冲，数据按段拷入、
  处理、拷出。任意地址、任意对齐都可用。
- **直连**：引擎直接读写调用者缓冲。驱动先 clean 源缓冲、clean 并 invalidate
  目的缓冲，按最多 32 KiB 一段运行，结束后再 invalidate 目的缓冲。

满足以下全部条件时走直连，否则自动走 bounce：

1. 打开 `BL616CL_SEC_ENG_DIRECT`，请求长度不小于 `BL616CL_SEC_ENG_DIRECT_MIN`；
2. 源、目的缓冲都在内部可缓存 RAM（栈上缓冲也可以）；PSRAM 和 flash 中的缓冲
   一律走 bounce；
3. 源地址 4 B 对齐；
4. 目的地址和长度都是 32 B（cache 行）的整数倍；
5. 源与目的是同一缓冲（原地），或者互不重叠；
6. 不是 AES-XTS（XTS 的 tweak 由 CPU 逐块处理，总是走 bounce）。

条件 4 的原因：目的缓冲如果和别的数据共用一个 cache 行，CPU 写回该行时会覆盖
引擎写入的结果。哈希请求没有目的缓冲，只检查源缓冲。

每个引擎（AES、SHA、TRNG）各有一把锁，不同引擎的请求可以交替进行；同一引擎上
的多个 session 在锁内换入各自保存的链值或哈希状态，可以交错使用。驱动只用轮询，
每段等待上限 100 ms，请求在调用者线程里忙等完成。

## 错误与超时

- 参数错误（缓冲为空、AES-CBC/XTS 长度不是 16 B 的整数倍、不支持的密钥长度等）
  返回 `EINVAL`。
- 引擎在 100 ms 内没有完成时返回 `ETIMEDOUT`。之后该 session 的每个请求都返回
  同一错误，需要释放后重建；SHA 在结束消息时超时会把摘要缓冲清零。其他 session
  和新 session 不受影响。
- 超时后驱动只清理该引擎的状态，不整体复位 SEC_ENG（整体复位会同时复位 TRNG）。
  实测中引擎恢复后会把被打断的那一段做完并写出结果，所以收到 `ETIMEDOUT` 后，
  不要假定输出缓冲保持原样：直连时这一段会晚些写进调用者缓冲。

SHA 依赖 drivers 中 `bflb_sha{1,256,512}_link_finish()` 返回 `int` 的版本：超时时
返回错误并清零摘要，旧版本会静默输出错误摘要。

## mbedTLS ALT

openvela 的 mbedTLS ALT（`apps/crypto/mbedtls/source/*_alt.c`）全部经
`/dev/crypto` 调用，所以先按上节打开 crypto 驱动。可以开的 ALT：

| ALT | 说明 |
|---|---|
| `MBEDTLS_AES_ALT` | ECB 单块用 CBC session 加全零 IV 实现；CBC 需要 apps 包含 openvela 提交 `b55330d9d`（按密钥长度选 CBC ID，并由 ALT 推进 IV），否则多次调用时 IV 不前进 |
| `MBEDTLS_SHA1_ALT`、`MBEDTLS_SHA256_ALT`、`MBEDTLS_SHA512_ALT` | 有 clone 限制，见下 |

开 `MBEDTLS_AES_ALT` 时还要关闭 `MBEDTLS_CIPHER_MODE_CFB` 和
`MBEDTLS_CIPHER_MODE_OFB`：ALT 用 CFB/OFB 算法 ID 建 session，本驱动不注册它们。

不要开的 ALT：

- `MD5`、`CMAC`、`RIPEMD160`、`POLY1305`：没有对应硬件，请求会失败；
- `ECDSA`、`BIGNUM`：本驱动不支持 PKA。

**SHA ALT 的 clone 限制**：`mbedtls_sha*_clone()` 在 ALT 里不复制哈希状态，只让两个
上下文共用同一个内核 session，之后任一方继续 update 或 finish 都会改动另一方。
TLS 握手和 PSA hash clone 依赖独立副本，在 OpenVela sim 上实测开 SHA ALT 后 TLS 1.2
握手失败。所以带 SSL/TLS 或 PSA hash clone 的镜像不要开 SHA ALT。这是 ALT 层的
问题，硬件驱动无法弥补。

**开销**：ALT 每次调用都新建并释放一个 `/dev/crypto` session，小数据量时这部分
开销大于硬件节省的时间，ALT 只在大块数据上有收益。是否在某个产品配置开某个 ALT，
要实测对比。

**熵源**：打开 mbedTLS（`CRYPTO_MBEDTLS`）时，`BL616CL_TRNG` 默认打开，并默认打开
`MBEDTLS_ENTROPY_HARDWARE_ALT`。mbedTLS 经 apps 的 `entropy_alt.c` 读
`/dev/random`（TRNG），不再经 `getrandom()` 读 `/dev/urandom`，所以熵源不受
`DEV_URANDOM` 选用哪种实现影响（除 `DEV_URANDOM_ARCH` 外都是软件伪随机数）。这个
ALT 不经过 `/dev/crypto`，不需要打开 crypto 驱动。TRNG 健康检查失败时 `/dev/random`
读不满，mbedTLS 收到错误，不会用到这次的数据。

当前板级配置：`nsh-peripherals` 打开 AES、SHA1、SHA256、SHA512 四个 ALT 和
`mbedselftest`；`wifi` 不开 AES、SHA ALT，原因见下。两者都按上段默认打开熵源 ALT。

### Wi-Fi 连接耗时

在 `wifi` 配置上逐个打开 ALT，对同一个 2.4G AP 各连 20 次，计时从发出
`wapi essid` 到 4-way 握手完成。对照镜像打开 crypto 驱动但不开 ALT：

| 镜像 | WPA3-SAE 中位数 / P90 | WPA2-PSK 中位数 / P90 |
|---|---:|---:|
| 对照（无 ALT） | 3.330 / 3.338 s | 3.252 / 3.254 s |
| + AES ALT | 3.340 / 3.350 s | 3.254 / 3.257 s |
| + SHA1 ALT | 3.331 / 3.347 s | 4.886 / 5.003 s |
| + SHA256 ALT | 3.363 / 3.369 s | 3.252 / 3.253 s |
| + SHA512 ALT | 3.328 / 3.333 s | 3.252 / 3.255 s |

没有一个 ALT 缩短连接时间。WPA2 每次连接都由口令算 PMK（4096 轮 PBKDF2，共
8192 次 HMAC-SHA1），SHA1 ALT 下每次 HMAC 都新建并释放 session，连接因此慢约
1.6 s；SHA256 ALT 让 WPA3 慢约 33 ms。所以 `wifi` 不开 AES、SHA ALT。默认打开熵源 ALT
后，`wifi`（不开 crypto 驱动）WPA3 连接 20 次的中位数为 3.340 s，连接时间不受影响。

## 测试

`nsh-peripherals` 打开了以下测试：

- 上游 cmocka 测试 `TESTING_CRYPTO`：AES-CBC、AES-CTR、AES-XTS 与 HASH（含
  `HUGE_BLOCK`）。HASH 里的 MD5 用例预期失败（不注册 MD5），不修改上游测试。
- vendor 测试命令 `mcu_crypto_test`：补上游未覆盖的边界，包括 SHA-224/384 与
  忽略 `olen`、AES-192/256 CBC 与原地、CTR 低 32 位与 128 位回绕、XTS 分段、多
  session 交错、0 长度与非对齐、跨 bounce 边界、直连阈值附近长度与各种对齐、四
  线程并发并同时读 TRNG。用法和各 case 的覆盖范围见
  `apps/mcu_peripheral_tests/crypto/README.md`。
- `mbedselftest`：在 ALT 打开时检查 mbedTLS 各算法自检。

cmocka crypto 测试任务栈需要 `TESTING_CRYPTO_STACKSIZE=8192`，2048 时 XTS 用例栈
溢出。

## 实测结果

带 KASAN 与 stack canary 的 `nsh-peripherals`，直连开、关各构建一次，结果相同：

```text
CRYPTO_TEST RESULT case=cbc PASS
CRYPTO_TEST RESULT case=ctr PASS
CRYPTO_TEST RESULT case=xts PASS
CRYPTO_TEST RESULT case=sha PASS
CRYPTO_TEST RESULT case=session PASS sessions=5
CRYPTO_TEST RESULT case=edge PASS
CRYPTO_TEST RESULT case=direct PASS
CRYPTO_TEST RESULT case=all PASS
```

cmocka 的 AES-CBC、AES-CTR、AES-XTS 和 HASH 的 SHA 用例全部通过，只有 MD5 预期
失败；`mbedselftest` 30 个套件全部通过；同一镜像上 TRNG 测试全部通过。

`mcu_crypto_test direct` 对 4096 B AES-CBC 连续 64 次的耗时（经 `/dev/crypto`，
含 ioctl 开销；KASAN 镜像中 `memcpy` 带插桩，bounce 的拷贝偏慢，不代表产品镜像）：

| 构建 | 32 B 对齐缓冲 | 偏移 1 B 的缓冲（走 bounce） |
|---|---:|---:|
| 直连开 | 10.2 ms | 85.3 ms |
| 直连关 | 80.2 ms | 84.9 ms |

另用高优先级线程在引擎运行时关闭 SEC 时钟 300 ms，触发 SHA 与 AES 各一次真实
超时：驱动返回 `ETIMEDOUT`，同一 session 之后返回同一错误；时钟恢复后新 session 的
结果与参考值一致，随后 `mcu_crypto_test all` 全部通过。

## 限制与残留风险

- 引擎彻底卡死、时钟恢复后也不完成的情况没有验证；超时恢复只用关时钟的方法测过。
- PSRAM、flash 中的缓冲只走 bounce，没有评估它们作直连源。
- CTR 不支持在一个块中间续接；末尾不足一块的请求之后，续接从下一个计数器开始。
- SHA ALT 的 clone 限制和 ALT 每次新建 session 的开销在 openvela ALT 层，本驱动
  不处理。
