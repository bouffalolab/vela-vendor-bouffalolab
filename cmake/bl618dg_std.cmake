# SPDX-License-Identifier: Apache-2.0
#
# AP startup uses the SDK clock, power and bus helpers. Compile these
# independently of the NuttX headers, as for CL: BL_Err_Type has its own
# ERROR enumerator. The final link includes only referenced SDK objects.
# SDK startup, IRQ vectors and FPU context code are supplied by NuttX.

set(BL618DG_STD_DIR "${CMAKE_CURRENT_LIST_DIR}/../drivers/soc/bl618dg/std")
nuttx_add_kernel_library(bl_std)
set_property(TARGET bl_std PROPERTY INCLUDE_DIRECTORIES "")

target_sources(
  bl_std
  PRIVATE ${BL618DG_STD_DIR}/port/bl618dg_clock.c
          ${BL618DG_STD_DIR}/src/bl618dg_aon.c
          ${BL618DG_STD_DIR}/src/bl618dg_clock.c
          ${BL618DG_STD_DIR}/src/bl618dg_common.c
          ${BL618DG_STD_DIR}/src/bl618dg_ef_cfg.c
          ${BL618DG_STD_DIR}/src/bl618dg_glb.c
          ${BL618DG_STD_DIR}/src/bl618dg_hbn.c
          ${BL618DG_STD_DIR}/src/bl618dg_pds.c
          ${BL618DG_STD_DIR}/src/bl618dg_tzc_sec.c)

target_include_directories(
  bl_std
  PRIVATE ${BL618DG_STD_DIR}/include
          ${BL618DG_STD_DIR}/include/hardware
          ${CMAKE_CURRENT_LIST_DIR}/../drivers/lhal/include
          ${CMAKE_CURRENT_LIST_DIR}/../drivers/lhal/include/arch
          ${CMAKE_CURRENT_LIST_DIR}/../drivers/lhal/include/arch/risc-v/n900
          ${CMAKE_CURRENT_LIST_DIR}/../drivers/lhal/include/arch/risc-v/n900/Core/Include
          ${CMAKE_CURRENT_LIST_DIR}/../drivers/lhal/config/bl618dg
          ${CMAKE_CURRENT_LIST_DIR}/../drivers/lhal/src/flash)

target_compile_definitions(bl_std PRIVATE ARCH_RISCV=1 BL618DG CPU_AP
                                        CONFIG_IRQ_NUM=117)
target_compile_options(bl_std PRIVATE -Wno-strict-prototypes)
