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

#endif /* __VENDOR_BOUFFALOLAB_BOARDS_BL616CL_BL616CL_AVE_V10_BL616CL_AVE_V10_H */
