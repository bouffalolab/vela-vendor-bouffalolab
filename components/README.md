# Bouffalo Lab Components / Middleware

中间件 / 可复用组件。每个组件一个**独立子目录** + **独立库**（独立 `.a`，可单独打包发布）。

## Kconfig

组件符号 `BL_COMPONENT_<NAME>` 不带 prompt，不出现在 menuconfig 里，也不写进
defconfig；由用到它的芯片功能 `select`，例如 `chips/bl616cl/Kconfig` 的
`BL616CL_WIRELESS_WIFI` 选择 macsw、wl80211 和 supplicant。组件之间的依赖写在组件
自己的 Kconfig 里。每层目录的 `Kconfig` 用 `rsource "*/Kconfig"` 读入下一层，不生成
菜单；新组件目录放一个 `Kconfig` 就会被读入。

```kconfig
# components/bar/Kconfig
config BL_COMPONENT_BAR
	bool
	help
	  Selected by BL616CL_<FEATURE>.
```

## 普通组件（源码在本仓）

```cmake
# components/bar/CMakeLists.txt
if(CONFIG_BL_COMPONENT_BAR)
  nuttx_add_library(bl_bar STATIC)        # 独立 libbl_bar.a
  target_sources(bl_bar PRIVATE bar.c)
  nuttx_export_header(TARGET bl_bar INCLUDE_DIRECTORIES include)
endif()
```

## 源码/预编译库双模组件

通用组件使用顶层已加载的 `bl_add_component()`；组件自己的 CMake 不需要重复
`include()` helper：

```cmake
# components/foo/CMakeLists.txt
bl_add_component(NAME foo)
```

helper 默认优先使用 `components/<name>/<name>/` 源码；源码不存在或组件被
`BL_USE_LIB_COMPONENTS` 指定时，改用 `components/<name>/libs/<chip>/lib<name>.a`。
源码和预编译模式都导出相同的组件头文件。

Wi-Fi 的 macsw/wl80211 core 不使用 `bl_add_component()`：源码/预编译选择见
`cmake/bl_wifi.cmake` 与 `docs/bl616cl-wifi-sta-porting-solution.md` 4.5 节。

`vendor/bouffalolab/drivers` 是独立 drivers release repo，不属于 components
双模目录。其 CMake 文件面向 Bouffalo SDK，OpenVela 由父仓 wrapper 显式选择
已适配源码，不执行 drivers release repo 内的 CMake 文件。
