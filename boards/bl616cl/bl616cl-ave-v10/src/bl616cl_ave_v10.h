/****************************************************************************
 * vendor/bouffalolab/boards/bl616cl/bl616cl-ave-v10/src/bl616cl_ave_v10.h
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

#ifndef __VENDOR_BOUFFALOLAB_BOARDS_BL616CL_BL616CL_AVE_V10_BL616CL_AVE_V10_H
#define __VENDOR_BOUFFALOLAB_BOARDS_BL616CL_BL616CL_AVE_V10_BL616CL_AVE_V10_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* I2C0 is wired to the ES8388 codec (7-bit address 0x10) and to the camera
 * connector SCCB, with 4.7 kOhm pull-ups to 3.3 V on the board.
 */

#define BL616CL_AVE_V10_I2C0_SCL_PIN 20
#define BL616CL_AVE_V10_I2C0_SDA_PIN 11

/* ES8388 on I2C0 and I2S0. MCLK is CLKOUT1 on GPIO1. */

#define BL616CL_AVE_V10_ES8388_ADDR      0x10
#define BL616CL_AVE_V10_ES8388_I2C_FREQ  400000
#define BL616CL_AVE_V10_I2S_MCLK_PIN     1
#define BL616CL_AVE_V10_I2S_BCLK_PIN     4
#define BL616CL_AVE_V10_I2S_FS_PIN       5
#define BL616CL_AVE_V10_I2S_DI_PIN       6
#define BL616CL_AVE_V10_I2S_DO_PIN       7

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BL616CL_AVE_V10_I2C0

/****************************************************************************
 * Name: bl616cl_ave_v10_i2c_initialize
 *
 * Description:
 *   Register I2C0 as /dev/i2c0 on BL616CL_AVE_V10_I2C0_SCL_PIN (GPIO20) and
 *   BL616CL_AVE_V10_I2C0_SDA_PIN (GPIO11).
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero (OK) on success; -ENODEV if the bus cannot be initialized, or the
 *   negated errno value from i2c_register().
 *
 ****************************************************************************/

int bl616cl_ave_v10_i2c_initialize(void);
#endif

#ifdef CONFIG_BL616CL_AVE_V10_AUDIO

/****************************************************************************
 * Name: bl616cl_ave_v10_audio_initialize
 *
 * Description:
 *   Register the ES8388 codec on I2C0 and I2S0 as /dev/audio/pcm0
 *   (playback) and /dev/audio/pcm_in0 (recording from the on-board
 *   microphone on LIN2/RIN2).
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero (OK) on success; -ENODEV if the I2C bus or I2S cannot be
 *   initialized, -ENOMEM if a codec instance cannot be allocated, or the
 *   negated errno value from audio_register(). The codec is not probed, so
 *   a codec that does not answer on I2C is not reported here. If only the
 *   recording device fails, /dev/audio/pcm0 stays registered.
 *
 ****************************************************************************/

int bl616cl_ave_v10_audio_initialize(void);
#endif

#endif /* __VENDOR_BOUFFALOLAB_BOARDS_BL616CL_BL616CL_AVE_V10_BL616CL_AVE_V10_H */
