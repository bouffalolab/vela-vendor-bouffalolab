# BL616CL 客户板级仓库接入

客户可以在自己的 git 仓库里维护 BL616CL 板级代码，用 repo 拉进 SDK 树，不需要把板子提交到 `vela-vendor-bouffalolab`。本文说明三件事：板级目录怎么组织，怎样复用 SDK 的公共代码，以及 repo 清单怎么写、版本怎么锁定、怎样跟随 SDK 升级。

已验证的范围：
- 主机：Linux x86_64；
- 配置：`nsh`；
- 硬件：Ai-M64P-32S-Kit。

其他配置和硬件按同样方式接入，但需要客户自己验证。

## 1. 目录结构

客户板级仓库挂在 SDK 树的 `vendor/<customer>/`，每块板一个目录：

```
<SDK 根>/
├── nuttx/、apps/ …                 SDK 基座
├── vendor/bouffalolab/             BL 适配层：芯片层、板级公共代码、构建 helper、烧录工具
└── vendor/<customer>/              客户板级仓库（客户清单里的一个 project）
    └── boards/bl616cl/<board>/
        ├── CMakeLists.txt
        ├── Kconfig
        ├── include/board.h
        ├── src/
        │   ├── CMakeLists.txt
        │   ├── <board>_board.c
        │   ├── <board>_fw_header.c、<board>_fw_header.h
        │   └── etc/（init.d/rc.sysinit、init.d/rcS、group）
        ├── config/
        │   ├── boot2_bl616cl_isp_release_v8.2.1.bin
        │   ├── partition_cfg_4M.toml
        │   └── bl_factory_params_IoTKitA_auto.dts
        └── configs/<name>/defconfig
```

板子从 SDK 复用三部分，都不复制：
- **芯片层**：defconfig 的 `CONFIG_ARCH_CHIP_CUSTOM_DIR` 指向 `vendor/bouffalolab/chips/bl616cl`。
- **板级公共代码**：`boards/bl616cl/common` 提供 bring-up、`board_app_initialize()`、复位和唯一的链接脚本。
- **构建与后处理**：链接参数、分区大小、固件后处理、烧录配置的生成。

后两部分都由 `vendor/bouffalolab/cmake/bl616cl_board.cmake` 接入。

`vendor/<customer>/` 根目录不要放 `CMakeLists.txt`。板目录由 NuttX 根据 defconfig 加入；vendor 顶层会把 `vendor/<customer>/CMakeLists.txt` 当作 app 或组件目录加入，如果它再向下加入 `boards/`，同一个板目录就会被构建两次。

## 2. 新建一块板

最快的方式是用 GitHub 模板仓库 [bouffalolab/vela-board-template](https://github.com/bouffalolab/vela-board-template) 新建板级仓库：它带一块最小示例板 `acme-kit`、改名脚本和 manifest 示例，用法见它的 README。也可以从 SDK 的 `boards/bl616cl/ai-m64p-32s-kit` 开始：复制固件头、`src/etc/`、`config/` 和 `configs/nsh/`，按板名改名。

**`CMakeLists.txt`**：

```cmake
include(${NUTTX_DIR}/../vendor/bouffalolab/cmake/bl616cl_board.cmake)

add_subdirectory(src)

bl616cl_board(FW_HEADER g_<board>_fw_header)
```

- 要先 include 再 `add_subdirectory(src)`，因为 `src/` 里可以用 helper 导出的 `BL616CL_VENDOR_DIR`。
- `bl616cl_board()` 发现板目录旁边没有 SDK 的 common 时，会自己加入 SDK 的 common，客户不用复制。
- 板目录旁边（`vendor/<customer>/boards/bl616cl/common`）不要放 common 的副本或符号链接。NuttX 会自动加入板目录旁边的 `common/`，helper 还会再加入 SDK 的 common，同一份代码会被加入两次。

**`src/CMakeLists.txt`**：列出板级源码，并把 `etc/` 编进 romfs。

```cmake
set(SRCS <board>_board.c <board>_fw_header.c)

nuttx_add_romfs(
  NAME etc
  MOUNTPOINT etc
  RCSRCS etc/init.d/rcS etc/init.d/rc.sysinit
  RCRAWS etc/group
  PATH ${CMAKE_CURRENT_BINARY_DIR}/etc)

target_link_libraries(board PRIVATE romfs_etc)
target_sources(board PRIVATE ${SRCS})
```

**板级代码要提供的东西**：

| 内容 | 要求 |
|---|---|
| `bl616cl_board_initialize()` | 声明在 SDK 的 `bl616cl_board_common.h`，由公共 bring-up 在芯片驱动就绪后调用。在这里注册本板接线相关的外设；只用 UART0 控制台时函数可以为空。外设写法参照 `ai-m64p-32s-kit`。 |
| 固件头 | 256 字节、放在 `.fw_header` 段的常量，变量名就是 `FW_HEADER` 参数。flash 和时钟参数要和实际硬件一致。 |
| `include/board.h` | 必须存在，芯片启动代码会包含 `<arch/board/board.h>`，内容可以为空。 |
| `Kconfig` | 定义板身份符号 `config ARCH_BOARD_<BOARD>`（`def_bool y`）。板级选项用 `<BOARD>_*` 命名，不要复用 SDK 板的符号。 |
| `config/` | 三个文件名固定，后处理脚本按这些名字读取。应用分区大小取自 `partition_cfg_4M.toml` 的 `FW` 条目。 |

**defconfig**：从 SDK 板的配置复制，改两行，芯片相关的行保持不变：

```
CONFIG_ARCH_BOARD_CUSTOM_DIR="../vendor/<customer>/boards/bl616cl/<board>"
CONFIG_ARCH_BOARD_CUSTOM_NAME="<board>"
```

- 路径相对 `nuttx/` 目录，不是相对 defconfig 本身。用相对路径，SDK 树搬到别的目录后仍然能构建。
- 用 `./vela menuconfig` 改配置；在别处改了 `.config` 后，用 `./vela savedefconfig` 写回。

## 3. 构建与烧录

`vela` 会列出 `vendor/*/boards` 下所有使用本 SDK 芯片的配置，客户板和 SDK 自带的板写法相同。下面以板名 `acme-kit` 为例，换成自己的板名：

```bash
./vela build --list
./vela build acme-kit/nsh
./vela flash acme-kit/nsh --port /dev/ttyUSB0
./vela menuconfig acme-kit/nsh
```

- 目标写法：可以写短名 `<board>/<name>`，也可以写 `<chip>/<board>/configs/<name>`、相对 SDK 根的 `vendor/<customer>/boards/bl616cl/<board>/configs/<name>`，或者绝对路径。以 `vendor/<customer>/boards/` 开头的写法只在这个 vendor 里查找，路径不存在时报错，不会落到别的 vendor 的同名板上。
- 板名要保持唯一。产物目录是 `cmake_out/<board>_<config>`，同名的板会共用这个目录：目录已经属于另一块板时，`build`、`menuconfig`、`savedefconfig`、`clean` 和 `flash` 都会报错，并提示用 `vela clean` 清理原来那块板。原来那块板已经删掉或移走，或者看不出目录是哪块板构建的，只能先 `vela clean`，再构建。短名有歧义时，`vela` 会列出可以区分的写法。
- `./build.sh vendor/<customer>/boards/bl616cl/<board>/configs/nsh --cmake` 也能构建，标准入口仍是 `vela`。不支持 Make 构建。

## 4. repo 清单

我们的清单仓库是 `bouffalolab/vela-manifest`。对外清单 `manifests/bl-vela-sdk-release.xml` 跟踪各 project 的分支，Wi-Fi 使用 vendor 里的预编译包。

### 4.1 推荐：客户自己的 manifest 仓库

下面以客户 `acme` 为例。客户的 manifest 仓库放三个文件：

```
acme-manifest/
├── default.xml
├── acme.xml
└── sdk/bl-vela-sdk-linux.lock.xml
```

`default.xml`：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<manifest>
  <include name="sdk/bl-vela-sdk-linux.lock.xml" />
  <include name="acme.xml" />
</manifest>
```

`acme.xml`：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<manifest>
  <remote name="acme" fetch="ssh://git@git.example.com/acme/" />
  <project name="vela-boards" path="vendor/acme" remote="acme"
           revision="0123456789abcdef0123456789abcdef01234567"
           upstream="refs/tags/acme-board-v1.0.0" />
</manifest>
```

要替换的值：文件名和 remote 名里的 `acme` 换成客户名；`fetch` 换成客户服务器上放板级仓库的地址前缀；`path` 换成 `vendor/<客户名>`；`revision` 换成板级仓库要发布的提交的完整 SHA；`upstream` 换成包含这个提交的 tag 或分支。

客户板 project 的写法：
- remote 用自己的名字，不要和 SDK 的 `openvela`、`bouffalo` 重名。
- `revision` 必须显式写；不写时会落到 SDK 的默认分支 `trunk` 上。
- `fetch` 是地址前缀，`name` 是服务端的仓库名，两者拼成克隆地址，repo 不会补 `.git`。
- 开发期间 `revision` 可以写分支名；发版时写完整 SHA 或 `refs/tags/<tag>`，tag 必须写全 `refs/tags/`。

**生成 SDK 锁文件**：在一个干净的目录里操作。

```bash
repo init -u https://github.com/bouffalolab/vela-manifest.git -b main \
          -m manifests/bl-vela-sdk-release.xml -p linux
repo sync -j8
# 确认这一版能构建、能启动后再导出
repo manifest -r --no-local-manifests -o bl-vela-sdk-linux.lock.xml
sed -i 's#fetch="../open-vela/"#fetch="https://github.com/open-vela/"#; s#fetch="../bouffalolab/"#fetch="https://github.com/bouffalolab/"#' \
    bl-vela-sdk-linux.lock.xml
```

每一步的原因：
- **`-p linux` 不能省**：不写时 repo 不记录 groups，`repo manifest -r` 只导出默认组，会漏掉 `notdefault,platform-linux` 组里的 6 个工具链和构建工具项目。
- **fetch 要改成绝对地址**：我们清单里的 remote 用相对 fetch，repo 按 manifest 仓库自己的地址解析它。文件放进客户的 manifest 仓库后，相对地址会指向客户的服务器。客户把全部仓库镜像到自己服务器时，改成镜像地址。
- **导出的结果**：每个 project 都钉在完整 SHA 上，`upstream` 记下原来跟踪的分支。

**开发者拉取**：

```bash
repo init -u ssh://git@git.example.com/acme/acme-manifest.git \
          -b refs/tags/acme-product-v1.0.0 -m default.xml -p linux
repo sync -j8
./vela build acme-kit/nsh
```

**升级 SDK**：
1. 在 manifest 仓库的升级分支上，按上面的步骤从新版 SDK 重新生成锁文件，整体替换旧文件。
2. 对比新旧文件，看 project 的 `path`、`name`、`groups`、`linkfile` 和 SHA 有哪些变化。
3. 有 `extend-project` 的仓按下面的说明更新 fork，再用 `repo manifest -o -` 查看合并后的清单，确认每个 project 都是预期的 SHA。
4. 构建并验证客户板，然后打新的产品 tag。
5. 失败时，用旧 tag 在新目录重新初始化。

客户自己的改动都放在 `acme.xml` 里，锁文件整份替换、不手改，这样可以少做手工合并。如果新锁文件里 SDK 的 SHA 或 project 标识（`name`、`path`）变了，先更新 fork 和 `acme.xml` 里引用它们的条目，再核对合并后的清单。

如果客户要改 SDK 的某个仓（例如在自己的 fork 上带补丁），在 `acme.xml` 里用 `extend-project` 换 remote 和 revision：

```xml
<extend-project name="vela-nuttx" remote="acme"
                revision="fedcba9876543210fedcba9876543210fedcba98"
                upstream="refs/heads/acme-main"
                base-rev="00112233445566778899aabbccddeeff00112233" />
```

`extend-project` 的 `revision` 会盖掉锁文件里这个仓的 SHA。如果没写 `base-rev`，换了新锁文件而 fork 没跟着更新时，这个仓会停在旧版本，repo 不会提示。所以三个属性都要写：
- `revision`：fork 上的完整 SHA。
- `upstream`：fork 上包含这个 SHA 的分支或 tag。`repo sync -c` 只取这个 ref，SHA 不在它上面就取不到。
- `base-rev`：fork 所基于的 SDK 版本，即锁文件里这个仓的 SHA。换了锁文件后这个值对不上，repo 解析清单时报 `detected base-revision mismatch` 并停下。这时把 fork rebase 到锁文件里的新 SHA，再同时更新 `revision` 和 `base-rev`。

升级 SDK 时还要核对两个接口：板级代码实现的 `bl616cl_board_initialize()`，以及 `CMakeLists.txt` 调用的 `bl616cl_board()`。

### 4.2 联调：local_manifests

想在已有的 SDK 工作区里快速加一块板，可以把 `acme.xml` 放进 `.repo/local_manifests/`：

```bash
repo init -u https://github.com/bouffalolab/vela-manifest.git -b main \
          -m manifests/bl-vela-sdk-release.xml -p linux
mkdir -p .repo/local_manifests
cp acme.xml .repo/local_manifests/
repo sync -j8
```

这种方式有三个缺点，只适合评估和本地联调：
- `repo init` 不会带入这个文件，每个工作区都要手工放；
- 这个文件不在版本管理里；
- SDK 仍然跟踪分支，没有锁定版本。

### 4.3 不推荐的做法

- **在客户清单里 `<include>` 我们的 xml**：做不到，`<include>` 只能引用同一个 manifest 仓库里的文件。
- **`<submanifest>`**：SDK 会被放到一个子目录下，`path` 不能为 `.`；外层清单的 project 也不能放进这个子目录，客户板没法挂到 `vendor/<customer>`。
- **fork `vela-manifest`**：只有 fork 也托管在 github.com 上时，相对 fetch 才能解析到原来的地址；另外还要持续合并上游历史。

## 5. 限制

- 我们的清单目前跟踪分支，还没有发版 tag。锁定版本要由客户按 4.1 节自己导出。
- 锁定的 SHA 必须一直能取到。上游重写历史或清理对象后，旧 SHA 可能取不到；需要长期复现的版本，请在自己的服务器上保留镜像。
- `cmake_out/<board>_<config>/flash_prog_cfg.ini` 里记录的是 boot2 和镜像的绝对路径。SDK 树移动后，要重新构建再烧录。
- `apps/mcu_peripheral_tests` 下依赖 SDK 板身份或接线的测试，在客户板上不会出现。
- 所有板共用 SDK 的链接脚本，flash 布局按 4 MiB 分区表。`config/` 下的文件名是固定的。
