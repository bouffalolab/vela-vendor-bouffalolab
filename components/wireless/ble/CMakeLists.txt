# SPDX-License-Identifier: Apache-2.0
#
# BL616CL BLE controller. lib/ and include/ are copied unchanged from the
# Bouffalo SDK release v2.3.36 (components/wireless/bluetooth/
# btblecontroller, library version 1.6.210). The HCI transport and the
# NuttX port of the library are built from chips/bl616cl.

if(NOT CONFIG_BL_COMPONENT_BLE)
  return()
endif()

nuttx_add_extra_library(
  ${CMAKE_CURRENT_LIST_DIR}/lib/libbtblecontroller_bl616cl_m2s1.a)
# The controller reads its TX power table (wl_ble_power_cfg_get) from the
# PHY/RF library, which also brings up the RF (rfparam_init -> wl_init).
nuttx_add_extra_library(
  ${BL_BOUFFALO_DRIVERS_DIR}/soc/bl616cl/phyrf/lib-gcc_10.2.0-toolchain_V2.6.1/libbl616cl_phyrf.a
)

# zblue's H4 driver, selected by CONFIG_BT_H4 but left out of the default
# zblue build. It compiles with the include paths and options of the
# zblue library.
if(NOT CONFIG_BT_H4)
  return()
endif()

nuttx_add_library(bl_zblue_ext)
target_sources(
  bl_zblue_ext
  PRIVATE ${NUTTX_APPS_DIR}/external/zblue/zblue/port/drivers/bluetooth/hci/h4.c
          ${CMAKE_CURRENT_LIST_DIR}/zblue_btsnoop.c)
target_include_directories(bl_zblue_ext
                           PRIVATE $<TARGET_PROPERTY:zblue,INCLUDE_DIRECTORIES>)
target_compile_options(bl_zblue_ext
                       PRIVATE $<TARGET_PROPERTY:zblue,COMPILE_OPTIONS>)
target_link_libraries(bl_zblue_ext PRIVATE zblue_headers)
