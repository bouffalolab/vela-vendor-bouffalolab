# Bouffalo Lab Drivers

每个驱动放在本目录下的**独立子目录**，自带 `CMakeLists.txt` + `Kconfig`，
被上层 `nuttx_add_subdirectory()` 自动发现（一层 glob，非递归）。

每个驱动**自建独立库**（产出独立 `.a`，不并入 `libarch.a`/`libboard.a`）：

```cmake
# drivers/foo/CMakeLists.txt
if(CONFIG_BL_DRIVER_FOO)
  nuttx_add_kernel_library(bl_foo)        # 寄存器级驱动需内核 flag；纯用户态用 nuttx_add_library
  target_sources(bl_foo PRIVATE foo.c)
  target_include_directories(bl_foo PRIVATE ${CMAKE_CURRENT_LIST_DIR})  # 私有
  nuttx_export_header(TARGET bl_foo INCLUDE_DIRECTORIES include)        # 仅导出对外 API 头 -> <bl_foo/...>
endif()
```

```kconfig
# drivers/foo/Kconfig
config BL_DRIVER_FOO
	bool "Bouffalo foo driver"
	default n
```
