/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_i2c.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_I2C_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_I2C_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/i2c/i2c_master.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_i2cbus_initialize
 *
 * Description:
 *   Initialize an I2C port. The first call sets up clocks and the SCL/SDA
 *   pins and starts the controller at 100 kHz. Later calls with the same pins
 *   only take a reference.
 *
 * Input Parameters:
 *   port    - I2C port number (0 or 1)
 *   scl_pin - SCL GPIO number (must be even)
 *   sda_pin - SDA GPIO number (must be odd, different from scl_pin)
 *
 * Returned Value:
 *   An I2C master handle on success; NULL if the pins or port are invalid,
 *   the port is not enabled, the pins differ from an existing user, or a
 *   device lookup fails.
 *
 ****************************************************************************/

struct i2c_master_s *bl616cl_i2cbus_initialize(int port, uint8_t scl_pin,
                                                uint8_t sda_pin);

/****************************************************************************
 * Name: bl616cl_i2cbus_uninitialize
 *
 * Description:
 *   Drop one reference to an I2C port. The last reference de-initializes the
 *   controller, releases the pins and gates the peripheral clock.
 *
 * Input Parameters:
 *   dev - Device structure as returned by bl616cl_i2cbus_initialize()
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. Errors: -EINVAL
 *   for a NULL or unknown handle or a port that is not initialized; -ENODEV
 *   if no I2C port is enabled.
 *
 ****************************************************************************/

int bl616cl_i2cbus_uninitialize(struct i2c_master_s *dev);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_I2C_H */
