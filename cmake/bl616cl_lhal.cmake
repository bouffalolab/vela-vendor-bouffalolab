# SPDX-License-Identifier: Apache-2.0
#
# bl_lhal is shipped as a prebuilt library for external users, so every
# bl616cl-capable source file must be compiled in regardless of the current
# Kconfig selection. Otherwise a library built with a minimal .config lacks
# code that external users later enable (the library is never rebuilt with
# all options turned on). Sources are kept in sync with the upstream
# bl616cl list in drivers/lhal/CMakeLists.txt.
#
# Deliberately excluded (known to break the build or conflict):
# - src/bflb_irq.c: chips/bl616cl provides all bflb_irq_* via the NuttX
#   adapter (duplicate symbols).
# - src/bflb_sec_mutex.c: FreeRTOS-based; chips/bl616cl/bl616cl_sec_mutex.c
#   provides the same hooks on NuttX (duplicate symbols).
# - src/bflb_usb_v2.c: requires CherryUSB headers, not part of this SDK.
# - src/bflb_lhal_romapi_patch.c: ROMAPI path, unused on NuttX.
# - Files absent from the upstream bl616cl list (bflb_adc.c, bflb_adc_v3.c,
#   bflb_acomp.c, bflb_dac.c, bflb_pwm_v1.c, bflb_sdio3.c, bflb_kys.c,
#   bflb_spi_psram.c, bflb_emac_v2.c, bflb_dpi.c, bflb_canfd.c, bflb_ipc.c,
#   bflb_multi_core_sync.c, bflb_touch.c, bflb_sec_irq.c, bflb_gmac.c):
#   other chips' IP blocks, not present on bl616cl.

set(BL616CL_LHAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../drivers/lhal")

nuttx_add_kernel_library(bl_lhal)

# The SDK sources use the toolchain libc headers and define BL_Err_Type::ERROR.
# Keep NuttX headers out of this library to avoid the NuttX ERROR enumerator.
set_property(TARGET bl_lhal PROPERTY INCLUDE_DIRECTORIES "")

target_sources(
  bl_lhal
  PRIVATE ${BL616CL_LHAL_DIR}/src/bflb_common.c
          ${BL616CL_LHAL_DIR}/src/bflb_cks.c
          ${BL616CL_LHAL_DIR}/src/bflb_ef_ctrl.c
          ${BL616CL_LHAL_DIR}/src/bflb_gpio.c
          ${BL616CL_LHAL_DIR}/src/bflb_i2c.c
          ${BL616CL_LHAL_DIR}/src/bflb_dma.c
          ${BL616CL_LHAL_DIR}/src/bflb_rtc.c
          ${BL616CL_LHAL_DIR}/src/bflb_sec_aes.c
          ${BL616CL_LHAL_DIR}/src/bflb_sec_sha.c
          ${BL616CL_LHAL_DIR}/src/bflb_sec_trng.c
          ${BL616CL_LHAL_DIR}/src/bflb_spi.c
          ${BL616CL_LHAL_DIR}/src/bflb_timer.c
          ${BL616CL_LHAL_DIR}/src/bflb_uart.c
          ${BL616CL_LHAL_DIR}/src/bflb_wdg.c
          ${BL616CL_LHAL_DIR}/src/bflb_flash.c
          ${BL616CL_LHAL_DIR}/src/flash/bflb_sf_cfg.c
          ${BL616CL_LHAL_DIR}/src/flash/bflb_xip_sflash.c
          ${BL616CL_LHAL_DIR}/src/flash/bflb_sflash.c
          ${BL616CL_LHAL_DIR}/src/flash/bflb_sf_ctrl.c
          ${BL616CL_LHAL_DIR}/src/flash/bflb_flash_secreg_port.c
          ${BL616CL_LHAL_DIR}/src/flash/bflb_flash_secreg.c
          ${BL616CL_LHAL_DIR}/src/bflb_clock.c
          ${BL616CL_LHAL_DIR}/src/bflb_reset.c
          ${BL616CL_LHAL_DIR}/src/bflb_adc_v2.c
          ${BL616CL_LHAL_DIR}/src/bflb_emac.c
          ${BL616CL_LHAL_DIR}/src/bflb_mjpeg.c
          ${BL616CL_LHAL_DIR}/src/bflb_pwm_v2.c
          ${BL616CL_LHAL_DIR}/src/bflb_cam.c
          ${BL616CL_LHAL_DIR}/src/bflb_sdio2.c
          ${BL616CL_LHAL_DIR}/src/bflb_i2s.c
          ${BL616CL_LHAL_DIR}/src/bflb_dbi.c
          ${BL616CL_LHAL_DIR}/src/bflb_audac.c
          ${BL616CL_LHAL_DIR}/src/bflb_auadc.c
          ${BL616CL_LHAL_DIR}/src/bflb_wo.c
          ${BL616CL_LHAL_DIR}/src/bflb_sdh.c
          ${BL616CL_LHAL_DIR}/src/bflb_ir.c
          ${BL616CL_LHAL_DIR}/src/bflb_sec_gmac.c
          ${BL616CL_LHAL_DIR}/src/bflb_touch_v2.c
          ${BL616CL_LHAL_DIR}/src/bflb_l1c.c
          ${BL616CL_LHAL_DIR}/src/bflb_mtimer.c
          ${BL616CL_LHAL_DIR}/src/bflb_rv_privilege.c
          ${BL616CL_LHAL_DIR}/include/arch/risc-v/t-head/rv_pmp.c
          ${BL616CL_LHAL_DIR}/include/arch/risc-v/t-head/rv_hart.c
          ${BL616CL_LHAL_DIR}/config/bl616cl/device_table.c)

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
