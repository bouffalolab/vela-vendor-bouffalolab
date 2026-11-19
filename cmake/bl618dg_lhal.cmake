# SPDX-License-Identifier: Apache-2.0
#
# LHAL sources used by the DG NSH platform. The NuttX adapter supplies
# SDK IRQ hooks; NuttX owns the vectors and system timer.

set(BL618DG_LHAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../drivers/lhal")
nuttx_add_kernel_library(bl_lhal)
set_property(TARGET bl_lhal PROPERTY INCLUDE_DIRECTORIES "")

target_sources(
  bl_lhal PRIVATE ${BL618DG_LHAL_DIR}/src/bflb_common.c
                  ${BL618DG_LHAL_DIR}/src/bflb_gpio.c
                  ${BL618DG_LHAL_DIR}/src/bflb_uart.c
                  ${BL618DG_LHAL_DIR}/src/bflb_mtimer.c
                  ${BL618DG_LHAL_DIR}/src/bflb_clock.c
                  ${BL618DG_LHAL_DIR}/config/bl618dg/device_table.c
                  ${BL618DG_LHAL_DIR}/src/bflb_l1c.c
                  ${BL618DG_LHAL_DIR}/src/bflb_rv_privilege.c)

target_include_directories(
  bl_lhal
  PRIVATE ${BL618DG_LHAL_DIR}/include
          ${BL618DG_LHAL_DIR}/include/arch
          ${BL618DG_LHAL_DIR}/include/arch/risc-v/n900
          ${BL618DG_LHAL_DIR}/include/arch/risc-v/n900/Core/Include
          ${BL618DG_LHAL_DIR}/config/bl618dg
          ${BL618DG_LHAL_DIR}/src/flash
          ${BL_BOUFFALO_DRIVERS_DIR}/soc/bl618dg/std/include
          ${BL_BOUFFALO_DRIVERS_DIR}/soc/bl618dg/std/include/hardware)

target_compile_definitions(bl_lhal PRIVATE ARCH_RISCV=1 BL618DG CPU_AP
                                         CONFIG_IRQ_NUM=117
                                         APP_VER_X=0 APP_VER_Y=0 APP_VER_Z=0)
target_compile_options(bl_lhal PRIVATE -Wno-strict-prototypes
                                      -fno-sanitize=all -fno-stack-protector)
