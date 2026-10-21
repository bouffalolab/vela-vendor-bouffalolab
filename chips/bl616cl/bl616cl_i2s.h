/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_i2s.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_I2S_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_I2S_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/audio/i2s.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* I2S0 pins. The I2S function of GPIO n is fixed by n % 4 (0 BCLK, 1 FS,
 * 2 DI, 3 DO). MCLK is the I2S reference clock routed through CLKOUT
 * n % 4 of the MCLK pin, so that CLKOUT channel must not be used for
 * anything else.
 */

struct bl616cl_i2s_pins_s
{
  uint8_t mclk;
  uint8_t bclk;
  uint8_t fs;
  uint8_t di;
  uint8_t dout;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_i2sbus_initialize
 *
 * Description:
 *   Initialize I2S0 as an I2S master: configure the pins, start MCLK at
 *   about 12.288 MHz and set up 48 kHz, 16-bit stereo Philips I2S.
 *   Transfers use DMA0; the channel is taken when a stream starts and
 *   given back when the driver runs out of buffers. A later call with the
 *   same pins returns the same device.
 *
 * Input Parameters:
 *   pins - I2S0 pin assignment.
 *
 * Returned Value:
 *   An I2S device on success; NULL if a pin is invalid, the pins differ
 *   from an earlier call, DMA0 is not available, or a device lookup fails.
 *
 ****************************************************************************/

struct i2s_dev_s *bl616cl_i2sbus_initialize(
  const struct bl616cl_i2s_pins_s *pins);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_I2S_H */
