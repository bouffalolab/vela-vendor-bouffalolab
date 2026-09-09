# Bouffalo Lab Components / Middleware

中间件 / 可复用组件。每个组件一个**独立子目录** + **独立库**（独立 `.a`，可单独打包发布）。

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
# components/wl80211/CMakeLists.txt
bl_add_component(NAME wl80211)
```

helper 默认优先使用 `components/<name>/<name>/` 源码；源码不存在或组件被
`BL_USE_LIB_COMPONENTS` 指定时，改用 `components/<name>/libs/<chip>/lib<name>.a`。
源码和预编译模式都导出相同的组件头文件。

`vendor/bouffalolab/drivers` 是独立 drivers release repo，不属于 components
双模目录。其 CMake 文件面向 Bouffalo SDK，OpenVela 由父仓 wrapper 显式选择
已适配源码，不执行 drivers release repo 内的 CMake 文件。
