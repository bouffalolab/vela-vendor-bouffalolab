# SPDX-License-Identifier: Apache-2.0
#
# Display framework of the Bouffalo SDK (bsp/common/display), copied as it
# is, with its NuttX platform layer and LCD device glue under nuttx/. The
# controller backends (DBI, SPI, software DBI type C) need LHAL and are
# built by the chip (chips/bl616cl). Touch drivers live under touch/.

nuttx_add_subdirectory()

if(NOT CONFIG_BL_COMPONENT_DISPLAY)
  return()
endif()

set(DISPLAY_SRCS
    display.c
    bus/display_bus.c
    bus/display_io.c
    panel/display_panel.c
    panel/display_board_gpio.c
    panel/dcs/display_panel_dcs.c
    panel/video_standard/display_video_standard.c
    scanout/display_scanout.c
    simple_font/display_simple_font.c
    simple_font/display_simple_font_ascii_16x8.c
    nuttx/display_os_nuttx.c)

if(CONFIG_LCD)
  list(APPEND DISPLAY_SRCS nuttx/display_lcddev.c)
endif()

# Panel descriptions: only the ones the board selects are built

set(DISPLAY_PANELS
    ST7789
    gram_st7789/display_st7789.c
    ST7796
    gram_st7796/display_st7796.c
    ILI9341
    gram_ili9341/display_ili9341.c
    ILI9488
    gram_ili9488/display_ili9488.c
    GC9A01
    gram_gc9a01/display_gc9a01.c
    GC9307
    gram_gc9307/display_gc9307.c
    ILI9486
    gram_ili9486/display_ili9486.c
    JD9853
    gram_jd9853/display_jd9853.c
    ST7735
    gram_st7735/display_st7735.c
    ST77922
    gram_st77922/display_st77922.c
    SPD2010
    gram_spd2010/display_spd2010.c
    NT35510
    gram_nt35510/display_nt35510.c
    ST7701S
    video_st7701s/display_st7701s.c
    ST7701P
    video_st7701p/display_st7701p.c
    GC9503V
    video_gc9503v/display_gc9503v.c
    JD9165BA
    video_jd9165ba/display_jd9165ba.c
    EK79007
    video_ek79007/display_ek79007.c
    AXS15231B
    video_axs15231b/display_axs15231b.c
    AXS15231E
    video_axs15231e/display_axs15231e.c
    FL7707N
    video_fl7707n/display_fl7707n.c
    ILI9806E
    video_ili9806e/display_ili9806e.c
    ILI9881C
    video_ili9881c/display_ili9881c.c
    JD9365TX
    video_jd9365tx/display_jd9365tx.c
    ST7102
    video_st7102/display_st7102.c)

while(DISPLAY_PANELS)
  list(POP_FRONT DISPLAY_PANELS panel source)
  if(CONFIG_BL_COMPONENT_DISPLAY_PANEL_${panel})
    list(APPEND DISPLAY_SRCS panel/${source})
  endif()
endwhile()

nuttx_add_kernel_library(bl_display)
target_sources(bl_display PRIVATE ${DISPLAY_SRCS})
target_include_directories(bl_display PRIVATE ${CMAKE_CURRENT_LIST_DIR})
