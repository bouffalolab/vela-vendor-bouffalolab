# SPDX-License-Identifier: Apache-2.0

if(NOT CONFIG_BL_COMPONENT_WPA_SUPPLICANT)
  return()
endif()

set(wpa_source ${CMAKE_CURRENT_LIST_DIR}/bl_wpa_supplicant)

# The public repository's CMakeLists.txt targets the Bouffalo SDK. Select
# sources here so its FreeRTOS port and SDK build macros are never evaluated.
nuttx_add_kernel_library(bl_wpa_supplicant)
target_sources(bl_wpa_supplicant PRIVATE
  ${wpa_source}/port/os_nuttx.c
  ${wpa_source}/src/ap/ap_config.c
  ${wpa_source}/src/ap/wpa_auth_ie.c
  ${wpa_source}/src/ap/wpa_auth_rsn_ccmp_only.c
  ${wpa_source}/src/bl_supplicant/bl_hostap.c
  ${wpa_source}/src/bl_supplicant/bl_wpa3.c
  ${wpa_source}/src/bl_supplicant/bl_wpa_main.c
  ${wpa_source}/src/bl_supplicant/bl_wpas_glue.c
  ${wpa_source}/src/common/sae.c
  ${wpa_source}/src/common/wpa_common.c
  ${wpa_source}/src/crypto/aes-omac1.c
  ${wpa_source}/src/crypto/aes-unwrap.c
  ${wpa_source}/src/crypto/aes-wrap.c
  ${wpa_source}/src/crypto/dh_group5.c
  ${wpa_source}/src/crypto/dh_groups.c
  ${wpa_source}/src/crypto/rc4.c
  ${wpa_source}/src/crypto/sha1-pbkdf2-nuttx.c
  ${wpa_source}/src/crypto/sha256-prf.c
  ${wpa_source}/src/crypto/crypto_mbedtls-ec.c
  ${wpa_source}/src/crypto/crypto_mbedtls-bignum.c
  ${wpa_source}/src/crypto/crypto_mbedtls_misc.c
  ${wpa_source}/src/rsn_supp/wpa.c
  ${wpa_source}/src/rsn_supp/wpa_ie.c
  ${wpa_source}/src/utils/common.c
  ${wpa_source}/src/utils/wpa_debug.c
  ${wpa_source}/src/utils/wpabuf.c)

target_include_directories(bl_wpa_supplicant PRIVATE
  ${wpa_source}/include
  ${wpa_source}/include/bl_supplicant
  ${wpa_source}/port/include
  ${wpa_source}/src
  ${BL_WL80211_PUBLIC_DIR}/include
  ${BL_WL80211_PUBLIC_DIR}
  ${BL_BOUFFALO_DRIVERS_DIR}/lhal/include
  ${BL_BOUFFALO_DRIVERS_DIR}/lhal/include/arch
  ${CMAKE_CURRENT_LIST_DIR}/../../../../apps/crypto/mbedtls/include)
nuttx_export_header(TARGET bl_wpa_supplicant INCLUDE_DIRECTORIES
  ${wpa_source}/include
  ${wpa_source}/include/bl_supplicant)

target_compile_definitions(bl_wpa_supplicant PRIVATE
  CONFIG_NEWLIB_NANO_FORMAT=0 CONFIG_WPA_WAPI_PSK=0
  PSK_SUPPLICANT=1 WPA3_SUPPORTED=1 CONFIG_ECC USE_MBEDTLS_CRYPTO
  BL616CL)
target_compile_options(bl_wpa_supplicant PRIVATE
  -Wno-unused-function -Wno-unused-variable)
