# SPDX-License-Identifier: Apache-2.0

if(NOT CONFIG_BL_COMPONENT_WL80211)
  return()
endif()

if(NOT CONFIG_ARCH_CHIP_BL616CL OR NOT CONFIG_MACSW_SELECT STREQUAL "vela_bl616cl")
  message(FATAL_ERROR "The Vela WL80211 port supports BL616CL/vela_bl616cl")
endif()

set(CHIP bl616cl)
set(WL80211_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/wl80211/src)
set(WL80211_SUBBUILD_DIR ${CMAKE_BINARY_DIR}/wl80211-bl616cl)
set(WL80211_ARCHIVE ${WL80211_SUBBUILD_DIR}/libwl80211_${CHIP}.a)

string(REGEX REPLACE "gcc$" "" CROSS_COMPILE "${CMAKE_C_COMPILER}")
set(PHYRF_INCLUDE ${BL_BOUFFALO_DRIVERS_DIR}/soc/${CHIP}/phyrf/include)
set(MACSW_INCLUDE ${BL_MACSW_INC_DIR})
set(SUPPLICANT_INCLUDE
    ${CMAKE_CURRENT_LIST_DIR}/../bl_wpa_supplicant/bl_wpa_supplicant/include
    ${CMAKE_CURRENT_LIST_DIR}/../bl_wpa_supplicant/bl_wpa_supplicant/include/bl_supplicant)

if(BL_WIFI_PREBUILT)
  message(STATUS "BL Wi-Fi: macsw/wl80211 cores from prebuilt bundles")
  set(WL80211_ARCHIVE ${CMAKE_CURRENT_LIST_DIR}/prebuilt/lib/libwl80211_${CHIP}.a)
else()
  message(STATUS "BL Wi-Fi: macsw/wl80211 cores from source")
  list(APPEND MACSW_INCLUDE ${CMAKE_CURRENT_LIST_DIR}/../macsw/macsw/include)

  # Build the wl80211 core archive with its own CMake project in standalone
  # mode (the same flow the Bouffalo SDK library.mk uses to produce
  # libwl80211_<chip>.a). Private net80211 sources never compile in the
  # NuttX build environment; the Vela side consumes the archive only.
  execute_process(
    COMMAND ${CMAKE_COMMAND} -S ${WL80211_SRC_DIR} -B ${WL80211_SUBBUILD_DIR} -G Ninja
            -DCMAKE_SYSTEM_NAME=Generic
            -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
            -DCROSS_COMPILE=${CROSS_COMPILE}
            -DCHIP=${CHIP}
            -DCONFIG_MACSW_SELECT=${CONFIG_MACSW_SELECT}
            -DPHYRF_INCLUDE=${PHYRF_INCLUDE}
            "-DMACSW_INCLUDE=${MACSW_INCLUDE}"
            "-DSUPPLICANT_INCLUDE=${SUPPLICANT_INCLUDE}"
            # Keep the build path out of the objects exported to
            # wl80211/prebuilt.
            "-DCMAKE_C_FLAGS=-ffile-prefix-map=${BL_SDK_ROOT}=."
    RESULT_VARIABLE wl80211_configure_result
    COMMAND_ERROR_IS_FATAL ANY)

  execute_process(
    COMMAND ${CMAKE_COMMAND} --build ${WL80211_SUBBUILD_DIR}
    RESULT_VARIABLE wl80211_build_result
    COMMAND_ERROR_IS_FATAL ANY)

  if(NOT EXISTS ${WL80211_ARCHIVE})
    message(FATAL_ERROR "wl80211 core archive not found: ${WL80211_ARCHIVE}")
  endif()
endif()

nuttx_add_extra_library(${WL80211_ARCHIVE})
# The wl80211 core and macsw paths call into the PHY/RF library (phy_init,
# phy_get_channel, wl_wlan_bb_*, wl_wlan_power_cfg_get); link the BL616CL
# PHYRF prebuilt next to the core archive.
nuttx_add_extra_library(${BL_BOUFFALO_DRIVERS_DIR}/soc/bl616cl/phyrf/lib-gcc_10.2.0-toolchain_V2.6.1/libbl616cl_phyrf.a)

# NuttX host port straight from the public wl80211 repository. The native
# Bouffalo SDK build selects the FreeRTOS/lwIP set (wl80211_platform.c,
# lwip.c, wifi_mgmr_cli.c) from the same repository instead. The BL616CL
# netdev and adapter live in chips/bl616cl.
set(WL80211_PUBLIC_DIR ${BL_WL80211_PUBLIC_DIR})

nuttx_add_kernel_library(bl_wl80211)
target_sources(
  bl_wl80211
  PRIVATE ${WL80211_PUBLIC_DIR}/wifi_mgmr.c
          ${WL80211_PUBLIC_DIR}/country.c
          ${WL80211_PUBLIC_DIR}/supplicant.c
          ${WL80211_PUBLIC_DIR}/nuttx.c
          ${WL80211_PUBLIC_DIR}/rtos_al_nuttx.c)

target_include_directories(
  bl_wl80211
  PRIVATE ${WL80211_PUBLIC_DIR}/include
          ${WL80211_PUBLIC_DIR}
          ${WL80211_PUBLIC_DIR}/macsw
          ${MACSW_INCLUDE}
          ${SUPPLICANT_INCLUDE}
          ${PHYRF_INCLUDE}
          ${BL_BOUFFALO_DRIVERS_DIR}/lhal/include
          ${BL_BOUFFALO_DRIVERS_DIR}/lhal/include/arch)

# The wl80211 timers and events run on LPWORK next to the driver RX/TX and
# the network stack; HPWORK is left for interrupt bottom halves (BLE).
target_compile_definitions(
  bl_wl80211
  PRIVATE BL616CL ARCH_RISCV WL80211_WORK=LPWORK
          CONFIG_MACSW_SELECT_INCLUDE="macsw_${CONFIG_MACSW_SELECT}_config.h")
target_compile_options(bl_wl80211 PRIVATE -Wno-undef -Wno-unused-function
                                          -Wno-unused-variable)
