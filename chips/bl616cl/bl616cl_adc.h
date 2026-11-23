/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_adc.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_ADC_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_ADC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/analog/adc.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* External GPADC channels. Channels 0 to 7 are on GPIO0 to GPIO7, channel 8
 * on GPIO12, channel 9 on GPIO13, channel 10 on GPIO32 and channel 11 on
 * GPIO33.
 */

#define BL616CL_ADC_NCHANNELS 12

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_adc_initialize
 *
 * Description:
 *   Set up the GPADC to scan the given external channels in the given
 *   order and return its ADC lower half for adc_register(). This switches
 *   the channel pins to analog input, enables the ADC clock and runs the
 *   one-time calibration (about 7 ms). Every sample is a single-ended
 *   16-bit conversion against the internal reference; read() returns the
 *   channel number in am_channel and the calibrated input in millivolts
 *   in am_data.
 *
 *   With CONFIG_BL616CL_ADC_POLL each ANIOC_TRIGGER converts every channel
 *   once and returns after queueing the group. With CONFIG_BL616CL_ADC_IRQ
 *   ANIOC_TRIGGER starts a continuous scan that runs until the last close
 *   or until samples are lost.
 *
 *   Call this once, before registering the device.
 *
 * Input Parameters:
 *   channels  - External channels to scan, each 0 to 11 and at most once
 *   nchannels - Number of entries in channels, 1 to BL616CL_ADC_NCHANNELS
 *
 * Returned Value:
 *   A pointer to the ADC lower half on success; NULL if an entry is
 *   invalid or repeated, the number of entries is out of range, or the
 *   GPADC cannot be set up.
 *
 ****************************************************************************/

struct adc_dev_s *bl616cl_adc_initialize(const uint8_t *channels,
                                         unsigned int nchannels);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_ADC_H */
