# MCU crypto 外设测试

`crypto_test_main.c` 编译为独立的 NSH 命令 `mcu_crypto_test`。测试通过标准
`/dev/crypto`（`CRIOGET`、`CIOCGSESSION`、`CIOCCRYPT`）访问 BL616CL SEC_ENG
硬件加密驱动，不直接调用芯片私有接口。

## 背景与边界

SEC_ENG 不感知 CPU cache，驱动把任意内存的数据分段拷进 non-cacheable bounce
buffer（默认 1024 B，`CONFIG_BL616CL_SEC_ENG_BOUNCE_SIZE`）再交给引擎；各 session
的链值在锁内拷入、拷出唯一的工作描述符。CTR 计数器按 128 位进位（硬件只自增低
32 位，驱动在回绕处拆分请求），XTS 用 ECB 加软件 tweak。上游 cmocka 用例只覆盖
标准向量，本测试补充这些实现细节的边界。

期望值来自独立实现：大块向量的密文用 `crypto_vectors.py`（Python `cryptography`）
按同样的确定性输入算出 FNV-1a 校验和，CBC 定长向量取 NIST SP 800-38A F.2。改动
向量参数后在主机执行 `python3 crypto_vectors.py`，把输出更新到源码的
`CRYPTO_*_FNV`。

## 运行前检查

测试固件需要：

```text
CONFIG_CRYPTO_CRYPTODEV_HARDWARE=y
CONFIG_BL616CL_CRYPTO=y
CONFIG_BL616CL_CRYPTO_AES=y
CONFIG_BL_MCU_PERIPHERAL_TESTS_CRYPTO=y
```

`nsh-peripherals` 配置已打开。固件中不应同时编入软件 crypto 驱动：CTR 回绕用例按
128 位进位给出期望值，框架的软件 CTR 只给低 32 位进位，结果会不同。

## 命令

```text
mcu_crypto_test cbc
mcu_crypto_test ctr
mcu_crypto_test xts
mcu_crypto_test session
mcu_crypto_test edge
mcu_crypto_test all
```

每个 case 结束打印 `CRYPTO_TEST RESULT case=<name> PASS`，`all` 最后打印
`CRYPTO_TEST RESULT case=all PASS`。失败行带 `FAIL`、步骤名和返回值或实际校验和。

## case 说明

每个大块向量都走五步：一次加密、一次解密、按 `pieces` 分段流式加密、分段原地解密、
一次原地加密。加密结果比对校验和，解密结果比对原文；每步都检查输出末尾 8 B 守卫
字节，发现越界写即失败。

| case | 覆盖内容 |
|---|---|
| `cbc` | NIST 向量：`CRYPTO_AES_192_CBC`、`CRYPTO_AES_256_CBC`，以及 `CRYPTO_AES_CBC` 用 256 位密钥；2064 B 大块（跨 bounce 边界，流式分段 1040 + 1024） |
| `ctr` | 低 32 位回绕进位到下一个字、第二段恰好从回绕点开始；128 位全进位回绕到 0；5000 B 大块，回绕点落在 bounce 分段中间；三者都带非整块尾部 |
| `xts` | 4096 B 数据单元（2×256 位密钥），流式分段 16 + 1008 + 1040 + 2032；1040 B 数据单元（2×128 位密钥） |
| `session` | CBC、CTR、XTS 三个 session 同时打开，按段轮流处理，每段都从其他 session 用过引擎后保存的链值续接 |
| `edge` | 0 长度不写输出；CBC 15 B、XTS 20 B 返回 `EINVAL`；源、目的地址非 4 字节对齐（异地与原地） |

大块长度按默认 bounce 大小选取。调大 bounce 后用例仍应通过，但不再覆盖跨分段边界。
