/****************************************************************************
 * vendor/bouffalolab/components/display/nuttx/display_lcddev.h
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

#ifndef __VENDOR_BOUFFALOLAB_COMPONENTS_DISPLAY_NUTTX_DISPLAY_LCDDEV_H
#define __VENDOR_BOUFFALOLAB_COMPONENTS_DISPLAY_NUTTX_DISPLAY_LCDDEV_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/lcd/lcd.h>

#include "display.h"

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: display_lcddev_initialize
 *
 * Description:
 *   Wrap an initialized GRAM display of the display framework as a NuttX
 *   LCD device, for board_lcd_getdev(). putrun and putarea send the pixels
 *   with display_draw(): a contiguous buffer that meets the display's
 *   buffer alignment goes to the bus as it is (by DMA when the bus has
 *   it), in pieces of at most max_draw_bytes; other buffers are copied row
 *   by row through an SRAM bounce buffer. Reading pixels back is not
 *   supported. Power maps to the board backlight.
 *
 * Input Parameters:
 *   display - Display that display_init() has brought up, in its final
 *             rotation.
 *
 * Returned Value:
 *   The LCD device, or NULL if the display is not an initialized GRAM
 *   display, its format has no NuttX equivalent or memory is short.
 *
 ****************************************************************************/

struct lcd_dev_s *display_lcddev_initialize(display_t *display);

#endif /* __VENDOR_BOUFFALOLAB_COMPONENTS_DISPLAY_NUTTX_DISPLAY_LCDDEV_H */
