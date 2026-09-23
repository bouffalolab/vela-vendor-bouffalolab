# SPDX-License-Identifier: Apache-2.0
#
# BL616CL RF parameter library. The sources stay in the drivers tree
# (drivers/rfparam); this wrapper selects them and supplies the NuttX
# shims under include/.

if(NOT CONFIG_BL_COMPONENT_RFPARAM)
  return()
endif()

set(RFPARAM_DIR ${BL_BOUFFALO_DRIVERS_DIR}/rfparam)
set(RFPARAM_SRCS
    ${RFPARAM_DIR}/Src/rfparam_adapter.c
    ${RFPARAM_DIR}/Src/rfparam_bl616cl_flash_otp.c
    ${RFPARAM_DIR}/Src/rfparam_rftlv.c)

nuttx_add_kernel_library(bl_rfparam)
target_sources(bl_rfparam PRIVATE ${RFPARAM_SRCS})

# The SoC headers define a BL_Err_Type enumerator named ERROR, which
# collides with the enum in sys/types.h; the forced pre-include renames it
# (same approach as chips/bl616cl/bl616cl_sdk.h).
set_source_files_properties(
  ${RFPARAM_SRCS}
  PROPERTIES COMPILE_OPTIONS
             "-include;${CMAKE_CURRENT_LIST_DIR}/include/bl616cl_rfparam_preinc.h")

target_include_directories(
  bl_rfparam
  PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include
          ${RFPARAM_DIR}/Inc
          ${BL_BOUFFALO_DRIVERS_DIR}/soc/bl616cl/phyrf/include
          ${BL_BOUFFALO_DRIVERS_DIR}/lhal/include
          ${BL_BOUFFALO_DRIVERS_DIR}/lhal/include/arch
          ${BL_BOUFFALO_DRIVERS_DIR}/lhal/include/arch/risc-v/t-head
          ${BL_BOUFFALO_DRIVERS_DIR}/lhal/include/arch/risc-v/t-head/Core/Include
          ${BL_BOUFFALO_DRIVERS_DIR}/lhal/src/flash
          ${BL_BOUFFALO_DRIVERS_DIR}/soc/bl616cl/std/include
          ${BL_BOUFFALO_DRIVERS_DIR}/soc/bl616cl/std/include/hardware)

target_compile_definitions(bl_rfparam PRIVATE BL616CL ARCH_RISCV)
target_compile_options(bl_rfparam PRIVATE -Wno-undef -Wno-unused-function
                                          -Wno-unused-variable)
