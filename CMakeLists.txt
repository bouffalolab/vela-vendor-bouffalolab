# SPDX-License-Identifier: Apache-2.0 vendor/bouffalolab/CMakeLists.txt
#
# Bouffalo Lab vendor top-level.
#
# nuttx_add_subdirectory() globs ONLY the immediate "*/CMakeLists.txt" (one
# level, non-recursive), so it pulls in examples/ and components/. The drivers
# release repo uses Bouffalo SDK build files, so OpenVela selects supported
# sources through parent-owned wrappers. Tools are host-only, while boards/ and
# chips/ are included through the custom board/chip configuration.
#
# Order matters: add subdirectories first (so their Kconfig menus are generated)
# then generate this menu which sources them.

include(${CMAKE_CURRENT_LIST_DIR}/cmake/bl_include_directories.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/bl_component.cmake)

set(BL_USE_LIB_COMPONENTS
    "${BL_USE_LIB_COMPONENTS}"
    CACHE STRING "Bouffalo components forced to prebuilt library mode")

set(BL_BOUFFALO_DRIVERS_DIR
    "${CMAKE_CURRENT_LIST_DIR}/drivers"
    CACHE INTERNAL "BouffaloLab drivers directory")

if(NOT DEFINED CHIP OR "${CHIP}" STREQUAL "")
  set(CHIP "${CONFIG_ARCH_CHIP}")
endif()

if((NOT DEFINED CHIP OR "${CHIP}" STREQUAL "") AND DEFINED BOARD_CONFIG)
  string(REGEX MATCH "(^|/)boards/([^/]+)/" _chip_match "${BOARD_CONFIG}")
  set(CHIP "${CMAKE_MATCH_2}")
endif()

string(TOLOWER "${CHIP}" CHIP)

bl_include_directories(
  SCOPE
  CHIP_BOARD
  DIRECTORIES
  ${CMAKE_CURRENT_LIST_DIR}/drivers/lhal/include
  ${CMAKE_CURRENT_LIST_DIR}/drivers/lhal/include/arch
  ${CMAKE_CURRENT_LIST_DIR}/drivers/lhal/include/arch/risc-v/t-head
  ${CMAKE_CURRENT_LIST_DIR}/drivers/lhal/include/arch/risc-v/t-head/Core/Include
  ${CMAKE_CURRENT_LIST_DIR}/drivers/lhal/config/bl616cl
  ${CMAKE_CURRENT_LIST_DIR}/drivers/lhal/src/flash
  ${CMAKE_CURRENT_LIST_DIR}/drivers/soc/bl616cl/std/include
  ${CMAKE_CURRENT_LIST_DIR}/drivers/soc/bl616cl/std/include/hardware
  ${CMAKE_CURRENT_LIST_DIR}/drivers/sys
  ${CMAKE_CURRENT_LIST_DIR}/drivers/sys/bl616cl)

if(COMMAND nuttx_add_kernel_library AND "${CHIP}" STREQUAL "bl616cl")
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/bl616cl_lhal.cmake)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/bl616cl_std.cmake)

  target_compile_options(
    bl_lhal PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wno-strict-prototypes>)
  target_compile_options(
    bl_std PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wno-strict-prototypes>)
endif()

nuttx_add_subdirectory()
nuttx_generate_kconfig(MENUDESC "Bouffalo Lab")
