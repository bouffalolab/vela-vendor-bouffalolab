# SPDX-License-Identifier: Apache-2.0
#
# Source or prebuilt selection for the macsw and wl80211 cores, shared by the
# Wi-Fi wrappers and chips/bl616cl.
#
# The development manifest checks out macsw and wl80211 public/private under
# components/wireless/wifi. The release manifest does not; the cores then
# come from the prebuilt bundles committed next to the wrappers
# (macsw/prebuilt, wl80211/prebuilt), exported by
# tools/bl616cl/export_wifi_prebuilt.sh. "vela build --use-lib
# macsw,wl80211" forces the bundles in a source tree. The two cores share
# headers and ABI, so they always use the same mode.
#
# Sets BL_WIFI_PREBUILT, BL_MACSW_INC_DIR, BL_WL80211_PUBLIC_DIR and
# BL_SDK_ROOT (mapped away from the exported LTO objects).

get_filename_component(BL_SDK_ROOT ${NUTTX_DIR}/.. REALPATH)
get_filename_component(BL_WIFI_DIR
                       ${CMAKE_CURRENT_LIST_DIR}/../components/wireless/wifi
                       ABSOLUTE)

string(REPLACE "," ";" _bl_wifi_force "${BL_USE_LIB_COMPONENTS}")
list(FIND _bl_wifi_force macsw _bl_wifi_force_macsw)
list(FIND _bl_wifi_force wl80211 _bl_wifi_force_wl80211)
set(_bl_wifi_macsw_src ${BL_WIFI_DIR}/macsw/macsw/CMakeLists.txt)
set(_bl_wifi_wl80211_src ${BL_WIFI_DIR}/wl80211/wl80211/src/CMakeLists.txt)

if(_bl_wifi_force_macsw GREATER -1 AND _bl_wifi_force_wl80211 GREATER -1)
  set(BL_WIFI_PREBUILT TRUE)
elseif(_bl_wifi_force_macsw GREATER -1 OR _bl_wifi_force_wl80211 GREATER -1)
  message(FATAL_ERROR "--use-lib must name both macsw and wl80211")
elseif(EXISTS ${_bl_wifi_macsw_src} AND EXISTS ${_bl_wifi_wl80211_src})
  set(BL_WIFI_PREBUILT FALSE)
elseif(EXISTS ${_bl_wifi_macsw_src} OR EXISTS ${_bl_wifi_wl80211_src})
  message(
    FATAL_ERROR
      "Only one of the macsw and wl80211 private sources is checked out; "
      "check out both, or neither to use the prebuilt bundles")
else()
  set(BL_WIFI_PREBUILT TRUE)
endif()

if(BL_WIFI_PREBUILT)
  set(BL_MACSW_INC_DIR ${BL_WIFI_DIR}/macsw/prebuilt/inc)
  set(BL_WL80211_PUBLIC_DIR ${BL_WIFI_DIR}/wl80211/prebuilt)
  foreach(lib macsw/prebuilt/lib/libmacsw_bl616cl.a
              wl80211/prebuilt/lib/libwl80211_bl616cl.a)
    if(NOT EXISTS ${BL_WIFI_DIR}/${lib})
      message(FATAL_ERROR "missing Wi-Fi prebuilt library: ${BL_WIFI_DIR}/${lib}")
    endif()
  endforeach()
else()
  set(BL_MACSW_INC_DIR ${BL_WIFI_DIR}/macsw/macsw/inc)
  set(BL_WL80211_PUBLIC_DIR ${BL_WIFI_DIR}/wl80211/wl80211)
endif()
