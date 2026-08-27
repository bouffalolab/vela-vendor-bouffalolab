# SPDX-License-Identifier: Apache-2.0

set(BL616CL_STD_DIR "${CMAKE_CURRENT_LIST_DIR}/../drivers/soc/bl616cl/std")

nuttx_add_kernel_library(bl_std)

# The SDK sources use the toolchain libc headers and define BL_Err_Type::ERROR.
# Keep NuttX headers out of this library to avoid the NuttX ERROR enumerator.
set_property(TARGET bl_std PROPERTY INCLUDE_DIRECTORIES "")

target_sources(
  bl_std
  PRIVATE ${BL616CL_STD_DIR}/port/bl616cl_clock.c
          ${BL616CL_STD_DIR}/src/bl616cl_aon.c
          ${BL616CL_STD_DIR}/src/bl616cl_clock.c
          ${BL616CL_STD_DIR}/src/bl616cl_common.c
          ${BL616CL_STD_DIR}/src/bl616cl_glb.c
          ${BL616CL_STD_DIR}/src/bl616cl_hbn.c
          ${BL616CL_STD_DIR}/src/bl616cl_pm.c)

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
