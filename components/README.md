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

## 导入独立 git 仓库（如复用 bouffalo_sdk 的 lhal）

两个 SDK 的 CMakeLists 不通用，所以用「外层包装」避免 openvela 读到内仓的 CMakeLists：

```
components/lhal/
├── CMakeLists.txt   ← 本仓(openvela 风格)。★绝不 add_subdirectory(lhal) / nuttx_add_subdirectory()
└── lhal/            ← 独立 git 仓(自带 bouffalo_sdk 的 CMakeLists，openvela 永不读)
```

原理：`nuttx_add_subdirectory()` 只 glob 一层、且只处理被显式 add 的目录，CMake 本身不递归。
外层只要不去 add 内层、而是**直接引内层源码**，内层 CMakeLists 就是惰性文件：

```cmake
# components/lhal/CMakeLists.txt
if(CONFIG_BL_LHAL)
  set(LHAL ${CMAKE_CURRENT_LIST_DIR}/lhal)
  nuttx_add_kernel_library(lhal)
  file(GLOB SRCS ${LHAL}/src/*.c)
  target_sources(lhal PRIVATE ${SRCS})                          # 直接引内层，不 add_subdirectory
  target_include_directories(lhal PRIVATE ${LHAL}/src)
  nuttx_export_header(TARGET lhal INCLUDE_DIRECTORIES ${LHAL}/include)
endif()
```

释放态（闭源发预编译库）改用：`nuttx_add_extra_library(<path>/liblhal.a)`（参考 `external/liblhdc`）。
