# MCU TRNG 外设测试

`trng_test_main.c` 编译为独立的 `libapps_mcu_trng_test.a`，注册 NSH 命令
`mcu_trng_test`。测试通过标准 `/dev/random` 或架构提供的 `/dev/urandom`
访问 BL616CL TRNG，不直接调用芯片私有接口。

## 背景与边界

BL616CL TRNG 每次硬件事务产生 32 B。驱动需要把标准 VFS 的任意长度、非对齐
buffer、并发调用和错误返回映射到该固定块接口。测试因此不只检查“能读到数据”，
还检查零长度、跨块长度、canary、标准 `getrandom()`、`poll()`、未知 ioctl、重复块
和多线程调用。

本文的 bit 计数、全 0/全 `0xff` 和重复块检查只用于发现明显的数据链路故障，
不能证明密码学熵、FIPS 合规或硬件健康测试认证。

## 运行前检查

测试固件需要：

```text
CONFIG_BL616CL_TRNG=y
CONFIG_BL_MCU_PERIPHERAL_TESTS_TRNG=y
CONFIG_DEV_RANDOM=y
```

也支持以下仅使用硬件 `/dev/urandom` 的合法组合：

```text
CONFIG_BL616CL_TRNG=y
# CONFIG_DEV_RANDOM is not set
CONFIG_DEV_URANDOM=y
CONFIG_DEV_URANDOM_ARCH=y
CONFIG_BL_MCU_PERIPHERAL_TESTS_TRNG=y
```

固件启动后先完成以下检查：

1. 串口出现 `NuttShell (NSH)` 和 `nsh>`。
2. 执行 `help`，确认存在 `mcu_trng_test`。
3. 执行 `ls -l /dev/random`；urandom-only 配置则检查 `/dev/urandom`。
4. 设备权限应为只读字符设备，测试过程不需要 GPIO 跳线或外部仪器。

## 命令

```text
mcu_trng_test lengths
mcu_trng_test api
mcu_trng_test stats
mcu_trng_test concurrent
mcu_trng_test all
```

建议先分别执行四个 case，再执行 `all`。独立执行便于定位失败；`all` 用于验证
前一个 case 的资源和设备状态不会污染下一个 case。

## lengths：长度、非对齐与边界保护

背景：硬件以 32 B 为块输出，但 VFS read 必须支持任意长度和任意字节地址。

流程：

1. 使用 `uint32_t` 数组建立确定的 4 字节对齐基址，把实际 buffer 设为基址加 1。
2. 运行前检查地址模 4 不为 0；否则直接失败，不把“计划非对齐”当成证据。
3. 以只读方式打开目标随机设备一次。
4. 依次请求 `0、1、3、4、31、32、33、255、256、257` B。
5. 每轮先重置整个 guarded buffer，在目标区前后分别写 `0x5a` 和 `0xa5`。
6. `read()` 返回值必须与请求长度完全相等；零长度必须返回 0。
7. 检查前后 canary 均未变化，覆盖不足一块、整块、跨块和尾块复制。
8. 长度不小于 32 B 时，拒绝全 0 或全 `0xff` 数据。
9. 输出实际 `align` 和 checksum，完成十轮后关闭 fd。

完成判据：十种长度均输出 `PASS`，全部 `align=1`，canary 无变化，最后输出
`case=lengths PASS count=10` 并返回 NSH。

## api：标准接口和异常请求

背景：验证设备节点不依赖私有 ioctl，且 `getrandom()` 能按 NuttX flags 选择正确
设备。

流程：

1. 以 `O_RDONLY | O_NONBLOCK` 打开目标设备。
2. 对 `POLLIN | POLLOUT` 执行零超时 `poll()`。
3. 必须立即得到 `POLLIN`，且不能出现 `POLLOUT/POLLERR/POLLHUP/POLLNVAL`。
4. 调用未知 ioctl `0x7fffffff`，必须返回 `-1` 且 `errno=ENOTTY`。
5. random 配置调用
   `getrandom(buffer, 257, GRND_RANDOM | GRND_NONBLOCK)`；必须完整返回且数据不能
   全 0 或全 `0xff`。
6. urandom-arch 配置调用 `getrandom(buffer, 257, 0)`；必须完整返回。
7. 关闭 fd，输出 poll mask 和 checksum。

完成判据：输出 `case=api PASS poll=0001` 并返回 NSH。

## stats：4096 B 数据链路冒烟

背景：完整 read 成功仍可能隐藏固定输出、块未刷新或重复拷贝。本 case 检查这些
明显故障，不把短样本外推为熵质量证明。

流程：

1. 分配 4096 B buffer，以只读方式打开目标设备。
2. 记录 `CLOCK_MONOTONIC`，用一次 `read()` 请求完整 4096 B，再记录结束时间。
3. 对 32768 bit 统计 1 的数量和千分比。
4. 将数据划分为 128 个 32 B 块，检查每块是否全 0 或全 `0xff`。
5. 对全部 32 B 块做两两比较，统计完全重复块。
6. 要求固定块和重复块均为 0，1 的比例处于 35% 到 65%。
7. 输出总耗时和 checksum，关闭 fd 并释放 buffer。

完成判据：输出 `case=stats PASS`、`fixed=0`、`repeated=0`，bit 比例在门限内。

## concurrent：多线程调用与资源收尾

背景：驱动用 mutex 串行化完整 VFS read。本 case 验证多个调用者反复打开、读取、
关闭时均能完成，并检查每个线程的累计长度和独立 checksum。

流程：

1. 创建 4 个线程，每个线程独立打开目标设备。
2. 每个线程执行 32 次读取，按线程编号错开轮换
   `1、31、32、33、127、255、257` B。
3. 每次 read 必须完整返回；长度不小于 32 B 时拒绝全 0 或全 `0xff`。
4. 每个线程累计字节数和 checksum，然后关闭自己的 fd。
5. 主线程 join 全部线程，任何 create、read、close 前的检查或 join 失败均使 case
   失败。
6. 汇总四线程总字节数；固定配置下应为 13215 B。

完成判据：4 个线程均完成 32 次读取，输出
`case=concurrent PASS threads=4 iterations=32 bytes=13215`。

限制：当前 BL616CL 为单核，同优先级线程使用 FIFO 调度；该 case 证明多线程调用
都完成，但没有确定性制造“一个线程持锁、另一个线程已阻塞在同一 mutex”的竞争
窗口，不能单独作为 mutex 竞争路径的时序证明。

## all：顺序回归

`mcu_trng_test all` 严格按 `lengths -> api -> stats -> concurrent` 执行。任一子项
失败时最终返回失败；全部完成时输出 `case=all PASS`。

## Ai-M64L-32S-Kit 实测数据

环境：BL616CL Ai-M64L-32S-Kit，`/dev/ttyUSB2`，2,000,000 baud。

### `/dev/random` 测试固件

十种长度全部 PASS，实际地址均为 `align=1`：

```text
TRNG_TEST length PASS len=0 align=1 checksum=811c9dc5
TRNG_TEST length PASS len=1 align=1 checksum=c20bf3a6
TRNG_TEST length PASS len=3 align=1 checksum=dc1e1393
TRNG_TEST length PASS len=4 align=1 checksum=749a0da7
TRNG_TEST length PASS len=31 align=1 checksum=23b011f1
TRNG_TEST length PASS len=32 align=1 checksum=30781fce
TRNG_TEST length PASS len=33 align=1 checksum=84d8fe13
TRNG_TEST length PASS len=255 align=1 checksum=ea115d74
TRNG_TEST length PASS len=256 align=1 checksum=57b8e8c1
TRNG_TEST length PASS len=257 align=1 checksum=1d538516
TRNG_TEST RESULT case=lengths PASS count=10
```

API 检查：

```text
TRNG_TEST BEGIN case=api
TRNG_TEST RESULT case=api PASS poll=0001 checksum=1d8cf234
```

4096 B 统计共执行四轮：

| 轮次 | 1 bit 数 | 千分比 | 固定块 | 重复块 | 耗时 |
|---|---:|---:|---:|---:|---:|
| 1 | 16272 | 496 | 0 | 0 | 9851 us |
| 2 | 16312 | 497 | 0 | 0 | 9822 us |
| 3 | 16328 | 498 | 0 | 0 | 9819 us |
| all 内 | 16422 | 501 | 0 | 0 | 9634 us |

并发 case 独立执行五轮，再在 `all` 中执行一轮；六轮均为 4 线程、每线程 32 次、
总计 13215 B。单轮各线程累计字节数固定为 3041、3167、3391、3616 B，checksum
随随机数据变化。

`all` 的关键输出：

```text
TRNG_TEST RESULT case=lengths PASS count=10
TRNG_TEST RESULT case=api PASS poll=0001 checksum=08058ea8
TRNG_TEST RESULT case=stats PASS bytes=4096 ones=16422 \
ones_permille=501 fixed=0 repeated=0 elapsed_us=9634 checksum=8efb8e00
TRNG_TEST RESULT case=concurrent PASS threads=4 iterations=32 bytes=13215
TRNG_TEST RESULT case=all PASS
nsh>
```

### 正式固件裁剪与存活

正式固件关闭 `CONFIG_BL_MCU_PERIPHERAL_TESTS_TRNG`。运行结果：

```text
Builtin Apps:
    cpuload  trace  mcu_gpio_test  critmon  gpio  mcu_timer_test
    dumpstack  hello  mcu_wdt_test  nsh  oneshot  sh  timer  wdog
nsh> ls -l /dev/random
 cr--r--r--           0 /dev/random
nsh> dd if=/dev/random of=/dev/null bs=32 count=4
nsh> echo ST015_FINAL_ALIVE
ST015_FINAL_ALIVE
nsh>
```

`help` 中没有 `mcu_trng_test`，但 `/dev/random` 可连续读取并返回 NSH，证明测试
入口已裁剪、正式驱动仍然工作。

### `/dev/urandom` 组合

urandom-only 配置完成 1222/1222 clean build，`final_nuttx` 为 844524 B，
`nuttx.bin` 为 468944 B，测试 archive 为 17368 B。最终 ELF 只包含
`devurandom_register()`，不包含 `devrandom_register()`；`nuttx.bin` SHA256 为：

```text
3e31409a2a94294a95c1feece221c107a8eda3fc6714c8ae8e44744484d82785
```

USB2 烧录的 host/device SHA 一致。`/dev/urandom` 权限为 `cr--r--r--`，
`/dev/random` 不存在；十种长度、API、四轮统计、六轮并发和 `all` 均通过，runner
汇总为：

```text
bytes=8899 stats=4 concurrent=6 failures=[]
```

四轮统计的 1 bit 千分比为 495、507、501、493，固定块和重复块均为 0；每轮并发
均完成 4 线程、每线程 32 次和 13215 B。现役外设回归和最终 NSH 存活也通过。

## 未覆盖项

- 没有故障注入 `BUSY` timeout 或 `HT_ERROR`，因此未实板证明失败后的清理恢复；
  驱动的超时和健康错误返回只能由静态代码审查支持。
- 没有测量单个 32 B 块的延迟分布、长时吞吐、温压角或重启间统计。
- 没有验证 RCT/APT 参数、熵估计、FIPS 或其他认证口径。
