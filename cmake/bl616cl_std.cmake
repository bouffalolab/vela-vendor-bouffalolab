# SPDX-License-Identifier: Apache-2.0
#
# bl_std is shipped as a prebuilt library for external users, so every
# bl616cl-capable source file must be compiled in regardless of the current
# Kconfig selection. Otherwise a library built with a minimal .config lacks
# code that external users later enable (the library is never rebuilt with
# all options turned on). Sources follow the non-ROMAPI branch of the
# upstream drivers/soc/bl616cl/std/CMakeLists.txt.
#
# Deliberately excluded (known to break the build or conflict):
# - src/bl616cl_romapi_e907.c, src/bl616cl_romapi_patch.c: ROM-resident
#   alternative implementations of aon/clock/common/ef_cfg/glb/hbn/pds
#   (mutually exclusive symbols). bouffalo_sdk defaults bl616cl to
#   CONFIG_ROMAPI=y, but this SDK never links against the ROM API
#   (no BFLB_USE_ROM_DRIVER / romapi_* references anywhere on the NuttX
#   side), so the flash-resident versions are used exclusively.
# - src/bl616cl_romdriver_e907.c: bootrom driver table, only built upstream
#   with CONFIG_STD_BOOTROM_ROMDRIVER; references bflb_efuse_get_adc_trim
#   which does not exist in this SDK's headers.

set(BL616CL_STD_DIR "${CMAKE_CURRENT_LIST_DIR}/../drivers/soc/bl616cl/std")

nuttx_add_kernel_library(bl_std)

# The SDK sources use the toolchain libc headers and define BL_Err_Type::ERROR.
# Keep NuttX headers out of this library to avoid the NuttX ERROR enumerator.
set_property(TARGET bl_std PROPERTY INCLUDE_DIRECTORIES "")

target_sources(
  bl_std
  PRIVATE ${BL616CL_STD_DIR}/port/bl616cl_clock.c
          ${BL616CL_STD_DIR}/port/bl616cl_reset.c
          ${BL616CL_STD_DIR}/src/bl616cl_aon.c
          ${BL616CL_STD_DIR}/src/bl616cl_clock.c
          ${BL616CL_STD_DIR}/src/bl616cl_common.c
          ${BL616CL_STD_DIR}/src/bl616cl_ef_cfg.c
          ${BL616CL_STD_DIR}/src/bl616cl_glb.c
          ${BL616CL_STD_DIR}/src/bl616cl_hbn.c
          ${BL616CL_STD_DIR}/src/bl616cl_pds.c
          ${BL616CL_STD_DIR}/src/bl616cl_pm.c
          ${BL616CL_STD_DIR}/src/bl616cl_psram.c
          ${BL616CL_STD_DIR}/src/bl616cl_sec_dbg.c
          ${BL616CL_STD_DIR}/src/bl616cl_tzc_sec.c
          ${BL616CL_STD_DIR}/src/bl616cl_mfg_efuse.c
          ${BL616CL_STD_DIR}/src/bl616cl_mfg_flash.c
          ${BL616CL_STD_DIR}/src/bl616cl_mfg_media.c)

target_include_directories(
  bl_std
  PRIVATE
    ${BL616CL_STD_DIR}/include
    ${BL616CL_STD_DIR}/include/hardware
    ${CMAKE_CURRENT_LIST_DIR}/../drivers/lhal/include
    ${CMAKE_CURRENT_LIST_DIR}/../drivers/lhal/include/arch
    ${CMAKE_CURRENT_LIST_DIR}/../drivers/lhal/include/arch/risc-v/t-head
    ${CMAKE_CURRENT_LIST_DIR}/../drivers/lhal/include/arch/risc-v/t-head/Core/Include
    ${CMAKE_CURRENT_LIST_DIR}/../drivers/lhal/src/flash)

target_compile_definitions(bl_std PRIVATE ARCH_RISCV=1 BL616CL
                                          CONFIG_IRQ_NUM=83)
