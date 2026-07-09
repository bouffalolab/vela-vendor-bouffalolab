# SPDX-License-Identifier: Apache-2.0 vendor/bouffalolab/CMakeLists.txt
#
# Bouffalo Lab vendor top-level.
#
# nuttx_add_subdirectory() globs ONLY the immediate "*/CMakeLists.txt" (one
# level, non-recursive) and add_subdirectory() each match. So it pulls in
# drivers/ examples/ components/ (which carry a CMakeLists.txt), while: - tools/
# is intentionally left without a CMakeLists.txt -> not built - boards/ chips/
# have no top-level CMakeLists.txt here; they are included by the kernel/arch
# side via CONFIG_ARCH_BOARD_CUSTOM_DIR / CONFIG_ARCH_CHIP_CUSTOM_DIR, NOT by
# this glob.
#
# Order matters: add subdirectories first (so their Kconfig menus are generated)
# then generate this menu which sources them.

include(${CMAKE_CURRENT_LIST_DIR}/cmake/bouffalo_sdk_compat.cmake)

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

if(COMMAND nuttx_add_kernel_library OR COMMAND nuttx_add_library)
  bl_add_driver_subdirectories(${CMAKE_CURRENT_LIST_DIR}/drivers)
endif()

if(TARGET bl_lhal AND "${CHIP}" STREQUAL "bl616cl")
  set(BL616CL_STDLIB_GLB_SOURCE
      ${CMAKE_CURRENT_LIST_DIR}/drivers/soc/bl616cl/std/src/bl616cl_glb.c)
  set_source_files_properties(${BL616CL_STDLIB_GLB_SOURCE}
                              PROPERTIES COMPILE_FLAGS "-Wno-strict-prototypes")
  target_sources(
    bl_lhal
    PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}/drivers/soc/bl616cl/std/port/bl616cl_clock.c
      ${CMAKE_CURRENT_LIST_DIR}/drivers/soc/bl616cl/std/src/bl616cl_clock.c
      ${BL616CL_STDLIB_GLB_SOURCE}
      ${CMAKE_CURRENT_LIST_DIR}/drivers/soc/bl616cl/std/src/bl616cl_hbn.c)
endif()

nuttx_add_subdirectory()
nuttx_generate_kconfig(MENUDESC "Bouffalo Lab")
