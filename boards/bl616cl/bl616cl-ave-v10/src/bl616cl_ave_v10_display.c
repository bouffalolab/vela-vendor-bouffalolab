/****************************************************************************
 * vendor/bouffalolab/boards/bl616cl/bl616cl-ave-v10/src/bl616cl_ave_v10_display.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <sys/param.h>

#include <nuttx/board.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/lcd/lcd_dev.h>
#include <nuttx/video/fb.h>

#include "bl616cl_dbi.h"
#include "display.h"
#include "nuttx/display_lcddev.h"
#include "panel/dcs/display_panel_dcs.h"
#include "panel/display_board_gpio.h"
#include "bl616cl_ave_v10.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if defined(CONFIG_BL616CL_AVE_V10_DISPLAY_ST7796_320X480)
#  include "panel/gram_st7796/display_st7796.h"
#  define AVE_DISPLAY_MODULE (&display_module_st7796_320x480)
#elif defined(CONFIG_BL616CL_AVE_V10_DISPLAY_ILI9488_320X480)
#  include "panel/gram_ili9488/display_ili9488.h"
#  define AVE_DISPLAY_MODULE (&display_module_ili9488_320x480)
#elif defined(CONFIG_BL616CL_AVE_V10_DISPLAY_ILI9486_320X480)
#  include "panel/gram_ili9486/display_ili9486.h"
#  define AVE_DISPLAY_MODULE (&display_module_ili9486_320x480)
#elif defined(CONFIG_BL616CL_AVE_V10_DISPLAY_ST77922_320X480)
#  include "panel/gram_st77922/display_st77922.h"
#  define AVE_DISPLAY_MODULE (&display_module_st77922_320x480)
#elif defined(CONFIG_BL616CL_AVE_V10_DISPLAY_ST7789_240X320)
#  include "panel/gram_st7789/display_st7789.h"
#  define AVE_DISPLAY_MODULE (&display_module_st7789_240x320)
#elif defined(CONFIG_BL616CL_AVE_V10_DISPLAY_ILI9341_240X320)
#  include "panel/gram_ili9341/display_ili9341.h"
#  define AVE_DISPLAY_MODULE (&display_module_ili9341_240x320)
#elif defined(CONFIG_BL616CL_AVE_V10_DISPLAY_GC9307_240X320)
#  include "panel/gram_gc9307/display_gc9307.h"
#  define AVE_DISPLAY_MODULE (&display_module_gc9307_240x320)
#elif defined(CONFIG_BL616CL_AVE_V10_DISPLAY_JD9853_240X320)
#  include "panel/gram_jd9853/display_jd9853.h"
#  define AVE_DISPLAY_MODULE (&display_module_jd9853_240x320)
#elif defined(CONFIG_BL616CL_AVE_V10_DISPLAY_NT35510_480X800)
#  include "panel/gram_nt35510/display_nt35510.h"
#  define AVE_DISPLAY_MODULE (&display_module_nt35510_480x800)
#else
#  error "Select the panel module of U10"
#endif

#if defined(CONFIG_BL616CL_AVE_V10_DISPLAY_ROTATION_90)
#  define AVE_DISPLAY_ROTATION DISPLAY_ROTATION_90
#elif defined(CONFIG_BL616CL_AVE_V10_DISPLAY_ROTATION_180)
#  define AVE_DISPLAY_ROTATION DISPLAY_ROTATION_180
#elif defined(CONFIG_BL616CL_AVE_V10_DISPLAY_ROTATION_270)
#  define AVE_DISPLAY_ROTATION DISPLAY_ROTATION_270
#else
#  define AVE_DISPLAY_ROTATION DISPLAY_ROTATION_0
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void bl616cl_ave_v10_display_pins(void);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* U10 8080 x8 signals muxed to the DBI controller. CS stays a software
 * GPIO, as in the SDK board.
 */

static const uint8_t g_dbi_b8_pins[] =
{
  BL616CL_AVE_V10_LCD_WR_PIN, BL616CL_AVE_V10_LCD_DC_PIN,
  BL616CL_AVE_V10_LCD_D0_PIN, BL616CL_AVE_V10_LCD_D0_PIN + 1,
  BL616CL_AVE_V10_LCD_D0_PIN + 2, BL616CL_AVE_V10_LCD_D0_PIN + 3,
  BL616CL_AVE_V10_LCD_D0_PIN + 4, BL616CL_AVE_V10_LCD_D0_PIN + 5,
  BL616CL_AVE_V10_LCD_D0_PIN + 6, BL616CL_AVE_V10_LCD_D0_PIN + 7,
};

static const display_dbi_iface_t g_dbi_ifaces[] =
{
  { DISPLAY_IFACE_DBI_B8, bl616cl_ave_v10_display_pins, NULL },
  { DISPLAY_IFACE_NONE, NULL, NULL },
};

static display_dbi_bus_t g_dbi = DISPLAY_DBI_BUS(.ifaces = g_dbi_ifaces);

static display_io_t g_io = DISPLAY_IO(
  &g_dbi, .cs = DISPLAY_PIN_LOW(BL616CL_AVE_V10_LCD_CS_PIN),
  .iface = DISPLAY_IFACE_DBI_B8,
  .freq_hz = CONFIG_BL616CL_AVE_V10_DISPLAY_FREQ,
  .format = DISPLAY_FMT_RGB565);

/* U10 wires no power, reset or backlight control */

static display_board_gpio_t g_board;

static display_dcs_panel_t g_panel = DISPLAY_DCS_PANEL(
  &g_io, AVE_DISPLAY_MODULE, DISPLAY_BOARD_GPIO(&g_board));

static display_t g_display = DISPLAY_GRAM(&g_panel);

static struct lcd_dev_s *g_lcd;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bl616cl_ave_v10_display_pins(void)
{
  bl616cl_dbi_gpio_init(DISPLAY_IFACE_DBI_B8, g_dbi_b8_pins,
                        nitems(g_dbi_b8_pins));
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int board_lcd_initialize(void)
{
  int ret;

  /* display_init() does nothing for a display that is up already */

  ret = display_init(&g_display);
  if (ret < 0)
    {
      return ret;
    }

  ret = display_set_rotation(&g_display, AVE_DISPLAY_ROTATION);
  if (ret < 0)
    {
      return ret;
    }

  if (g_lcd == NULL)
    {
      g_lcd = display_lcddev_initialize(&g_display);
    }

  return g_lcd != NULL ? OK : -ENOMEM;
}

struct lcd_dev_s *board_lcd_getdev(int devno)
{
  return devno == 0 ? g_lcd : NULL;
}

void board_lcd_uninitialize(void)
{
  display_deinit(&g_display);
}

int bl616cl_ave_v10_display_initialize(void)
{
  int ret;

  ret = board_lcd_initialize();
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_LCD_DEV
  ret = lcddev_register(0);
  if (ret < 0)
    {
      return ret;
    }
#endif

#if defined(CONFIG_VIDEO_FB) && defined(CONFIG_LCD_FRAMEBUFFER)
  ret = fb_register(0, 0);
#endif

  return ret;
}
