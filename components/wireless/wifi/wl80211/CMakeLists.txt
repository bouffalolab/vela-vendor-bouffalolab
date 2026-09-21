# SPDX-License-Identifier: Apache-2.0

if(NOT CONFIG_BL_COMPONENT_WL80211)
  return()
endif()

if(NOT CONFIG_ARCH_CHIP_BL616CL OR NOT CONFIG_MACSW_SELECT STREQUAL "default")
  message(FATAL_ERROR "The Vela WL80211 port supports BL616CL/default")
endif()

set(CHIP bl616cl)
set(WL80211_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/wl80211/src)
set(WL80211_SUBBUILD_DIR ${CMAKE_BINARY_DIR}/wl80211-bl616cl)
set(WL80211_ARCHIVE ${WL80211_SUBBUILD_DIR}/libwl80211_${CHIP}.a)

string(REGEX REPLACE "gcc$" "" CROSS_COMPILE "${CMAKE_C_COMPILER}")
set(PHYRF_INCLUDE ${BL_BOUFFALO_DRIVERS_DIR}/soc/${CHIP}/phyrf/include)
set(MACSW_INCLUDE
    ${CMAKE_CURRENT_LIST_DIR}/../macsw/macsw/inc
    ${CMAKE_CURRENT_LIST_DIR}/../macsw/macsw/include)
set(SUPPLICANT_INCLUDE
    ${CMAKE_CURRENT_LIST_DIR}/../bl_wpa_supplicant/bl_wpa_supplicant/include
    ${CMAKE_CURRENT_LIST_DIR}/../bl_wpa_supplicant/bl_wpa_supplicant/include/bl_supplicant)

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
          -DCONFIG_MACSW_SELECT=default
          -DPHYRF_INCLUDE=${PHYRF_INCLUDE}
          "-DMACSW_INCLUDE=${MACSW_INCLUDE}"
          "-DSUPPLICANT_INCLUDE=${SUPPLICANT_INCLUDE}"
  RESULT_VARIABLE wl80211_configure_result
  COMMAND_ERROR_IS_FATAL ANY)

execute_process(
  COMMAND ${CMAKE_COMMAND} --build ${WL80211_SUBBUILD_DIR}
  RESULT_VARIABLE wl80211_build_result
  COMMAND_ERROR_IS_FATAL ANY)

if(NOT EXISTS ${WL80211_ARCHIVE})
  message(FATAL_ERROR "wl80211 core archive not found: ${WL80211_ARCHIVE}")
endif()

nuttx_add_extra_library(${WL80211_ARCHIVE})
