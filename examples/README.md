# Bouffalo Lab Examples

每个示例放在本目录下的**独立子目录**，自带 `CMakeLists.txt` + `Kconfig`，
由上层 `nuttx_add_subdirectory()` 自动发现。

```cmake
# examples/hello_bl/CMakeLists.txt
if(CONFIG_BL_EXAMPLES_HELLO)
  nuttx_add_application(
    NAME hello_bl
    SRCS hello_bl_main.c
    STACKSIZE ${CONFIG_BL_EXAMPLES_HELLO_STACKSIZE}
    PRIORITY ${CONFIG_BL_EXAMPLES_HELLO_PRIORITY})
endif()
```

```kconfig
# examples/hello_bl/Kconfig
config BL_EXAMPLES_HELLO
	tristate "Bouffalo hello example"
	default n

if BL_EXAMPLES_HELLO
config BL_EXAMPLES_HELLO_STACKSIZE
	int "stack size"
	default 2048
config BL_EXAMPLES_HELLO_PRIORITY
	int "priority"
	default 100
endif
```
