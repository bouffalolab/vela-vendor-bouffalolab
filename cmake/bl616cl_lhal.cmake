# SPDX-License-Identifier: Apache-2.0

set(BL616CL_LHAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../drivers/lhal")

nuttx_add_kernel_library(bl_lhal)

# The SDK sources use the toolchain libc headers and define BL_Err_Type::ERROR.
# Keep NuttX headers out of this library to avoid the NuttX ERROR enumerator.
set_property(TARGET bl_lhal PROPERTY INCLUDE_DIRECTORIES "")

target_sources(
  bl_lhal
  PRIVATE ${BL616CL_LHAL_DIR}/src/bflb_common.c
          ${BL616CL_LHAL_DIR}/src/bflb_ef_ctrl.c
          ${BL616CL_LHAL_DIR}/src/bflb_gpio.c
          ${BL616CL_LHAL_DIR}/src/bflb_timer.c
          ${BL616CL_LHAL_DIR}/src/bflb_uart.c
          ${BL616CL_LHAL_DIR}/src/bflb_wdg.c
          ${BL616CL_LHAL_DIR}/src/bflb_flash.c
          ${BL616CL_LHAL_DIR}/src/flash/bflb_sf_cfg.c
          ${BL616CL_LHAL_DIR}/src/flash/bflb_xip_sflash.c
          ${BL616CL_LHAL_DIR}/src/flash/bflb_sflash.c
          ${BL616CL_LHAL_DIR}/src/flash/bflb_sf_ctrl.c
          ${BL616CL_LHAL_DIR}/src/bflb_l1c.c
          ${BL616CL_LHAL_DIR}/src/bflb_mtimer.c
          ${BL616CL_LHAL_DIR}/include/arch/risc-v/t-head/rv_pmp.c
          ${BL616CL_LHAL_DIR}/config/bl616cl/device_table.c)

if(CONFIG_BL616CL_I2C0 OR CONFIG_BL616CL_I2C1 OR CONFIG_BL616CL_SPI0
   OR CONFIG_BL616CL_SPI1)
  target_sources(bl_lhal PRIVATE ${BL616CL_LHAL_DIR}/src/bflb_clock.c)
endif()

if(CONFIG_BL616CL_I2C0 OR CONFIG_BL616CL_I2C1)
  target_sources(bl_lhal PRIVATE ${BL616CL_LHAL_DIR}/src/bflb_i2c.c)
endif()

if(CONFIG_BL616CL_SPI0 OR CONFIG_BL616CL_SPI1)
  target_sources(bl_lhal PRIVATE ${BL616CL_LHAL_DIR}/src/bflb_spi.c)
endif()

target_include_directories(
  bl_lhal
  PRIVATE ${BL616CL_LHAL_DIR}/include
          ${BL616CL_LHAL_DIR}/include/arch
          ${BL616CL_LHAL_DIR}/include/arch/risc-v/t-head
          ${BL616CL_LHAL_DIR}/include/arch/risc-v/t-head/Core/Include
          ${BL616CL_LHAL_DIR}/config/bl616cl
          ${BL616CL_LHAL_DIR}/src/flash
          ${BL_BOUFFALO_DRIVERS_DIR}/soc/bl616cl/std/include
          ${BL_BOUFFALO_DRIVERS_DIR}/soc/bl616cl/std/include/hardware)

target_compile_definitions(
  bl_lhal PRIVATE APP_VER_X=0 APP_VER_Y=0 APP_VER_Z=0 ARCH_RISCV=1 BL616CL
                  CONFIG_IRQ_NUM=83)
