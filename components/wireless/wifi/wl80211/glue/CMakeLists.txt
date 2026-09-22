# SPDX-License-Identifier: Apache-2.0
#
# BL616CL wl80211 platform glue (host-side adaptation layer).
#
# These sources are the NuttX/Vela side of the wl80211 stack: the OS
# adapter (rtos_al_nuttx.c, nuttx.c), the management plane (wifi_mgmr.c,
# country.c), the netdev registration (bl616_wlan.c) and the platform
# bring-up (bl616_wifi_adapter.c). They are NOT part of the upstream
# wl80211 component repository - the core archive is built separately
# from components/wireless/wifi/wl80211/wl80211/src and consumed as a
# prebuilt library. This layout mirrors the BL4 vendor tree
# (chip/bl616/wl80211 + chip/bl616) and keeps the component repository
# free of host-specific code.
#
# Single-stack build: this SDK ships only the wl80211 MAC stack (no fhost
# alternative), so the BL4 dual-stack gate CONFIG_BL616_WLAN_USE_WL80211
# was expanded away; the sources now carry the wl80211 path only.

if(NOT CONFIG_BL_COMPONENT_WL80211)
  return()
endif()

set(glue_dir ${CMAKE_CURRENT_LIST_DIR})

nuttx_add_kernel_library(bl616cl_wlan_glue)
target_sources(bl616cl_wlan_glue PRIVATE
  ${glue_dir}/wifi_mgmr.c
  ${glue_dir}/country.c
  ${glue_dir}/nuttx.c
  ${glue_dir}/rtos_al_nuttx.c
  ${glue_dir}/../wl80211/supplicant.c
  ${glue_dir}/bl616_wlan.c
  ${glue_dir}/bl616_wifi_adapter.c
  ${glue_dir}/bl616cl_efuse_mac.c
  ${glue_dir}/bl616cl_macsw_lp.c
  ${glue_dir}/bl616cl_rfparam_ext.c
  ${BL_BOUFFALO_DRIVERS_DIR}/rfparam/Src/rfparam_adapter.c
  ${BL_BOUFFALO_DRIVERS_DIR}/rfparam/Src/rfparam_bl616cl_flash_otp.c
  ${BL_BOUFFALO_DRIVERS_DIR}/rfparam/Src/rfparam_rftlv.c)

# rfparam_adapter.c pulls the SoC headers whose BL_Err_Type ERROR enumerator
# collides with the enum in sys/types.h; force the pre-include shim that
# renames it (same trick as chips/bl616cl/bl616cl_sdk.h) and pull in the
# lhal flash include dir for bflb_sflash.h.
set_source_files_properties(
  ${BL_BOUFFALO_DRIVERS_DIR}/rfparam/Src/rfparam_adapter.c
  ${BL_BOUFFALO_DRIVERS_DIR}/rfparam/Src/rfparam_bl616cl_flash_otp.c
  ${BL_BOUFFALO_DRIVERS_DIR}/rfparam/Src/rfparam_rftlv.c
  PROPERTIES COMPILE_OPTIONS
  "-include;${glue_dir}/include/bl616cl_soc_preinc.h")

target_include_directories(bl616cl_wlan_glue PRIVATE
  ${glue_dir}
  ${glue_dir}/include
  ${BL_BOUFFALO_DRIVERS_DIR}/rfparam/Inc
  ${CMAKE_CURRENT_LIST_DIR}/../wl80211/include
  ${CMAKE_CURRENT_LIST_DIR}/../wl80211
  ${CMAKE_CURRENT_LIST_DIR}/../wl80211/macsw
  ${CMAKE_CURRENT_LIST_DIR}/../../macsw/macsw/inc
  ${CMAKE_CURRENT_LIST_DIR}/../../macsw/macsw/ip/lmac/src/hal
  ${CMAKE_CURRENT_LIST_DIR}/../../macsw/macsw/plf/refip/src/arch/risc-v
  ${CMAKE_CURRENT_LIST_DIR}/../../macsw/macsw/plf/refip/src/arch/risc-v/ll/thead_e907
  ${CMAKE_CURRENT_LIST_DIR}/../../macsw/macsw/plf/refip/src/driver/intc/risc-v
  ${CMAKE_CURRENT_LIST_DIR}/../../macsw/macsw
  ${CMAKE_CURRENT_LIST_DIR}/../../bl_wpa_supplicant/bl_wpa_supplicant/include
  ${CMAKE_CURRENT_LIST_DIR}/../../bl_wpa_supplicant/bl_wpa_supplicant/include/bl_supplicant
  ${BL_BOUFFALO_DRIVERS_DIR}/soc/bl616cl/phyrf/include
  ${BL_BOUFFALO_DRIVERS_DIR}/lhal/include
  ${BL_BOUFFALO_DRIVERS_DIR}/lhal/include/arch
  ${BL_BOUFFALO_DRIVERS_DIR}/lhal/include/arch/risc-v/t-head
  ${BL_BOUFFALO_DRIVERS_DIR}/lhal/include/arch/risc-v/t-head/Core/Include
  ${BL_BOUFFALO_DRIVERS_DIR}/lhal/src/flash
  ${BL_BOUFFALO_DRIVERS_DIR}/soc/bl616cl/std/include
  ${BL_BOUFFALO_DRIVERS_DIR}/soc/bl616cl/std/include/hardware
  ${NUTTX_DIR}/arch/risc-v/src/common
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../chips/bl616cl/include
  ${CMAKE_CURRENT_LIST_DIR}/../../../../../apps/crypto/mbedtls/include)

target_compile_definitions(bl616cl_wlan_glue PRIVATE
  BL616CL ARCH_RISCV
  CONFIG_MACSW_SELECT_INCLUDE="macsw_default_config.h")
target_compile_options(bl616cl_wlan_glue PRIVATE
  -Wno-undef -Wno-unused-function -Wno-unused-variable)
