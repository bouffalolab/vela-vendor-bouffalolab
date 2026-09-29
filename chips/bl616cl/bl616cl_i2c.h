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
 * Public Types
 ****************************************************************************/

#ifdef CONFIG_BL616CL_I2C_TEST
#  define BL616CL_I2C_TEST_STATUS_NACK    (1 << 3)
#  define BL616CL_I2C_TEST_STATUS_FER     (1 << 5)
#  define BL616CL_I2C_TEST_STATUS_TIMEOUT (1 << 6)

struct bl616cl_i2c_test_msg_s
{
  uint16_t addr;
  uint16_t flags;
  uint8_t *buffer;
  uint16_t length;
};

struct bl616cl_i2c_test_ops_s
{
  int (*configure)(void *arg, uint32_t frequency);
  int (*transfer)(void *arg,
                  const struct bl616cl_i2c_test_msg_s *msgs, int count);
  uint32_t (*status)(void *arg);
  void (*cleanup)(void *arg);
};
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

struct i2c_master_s *bl616cl_i2cbus_initialize(int port, uint8_t scl_pin,
                                                uint8_t sda_pin);
int bl616cl_i2cbus_uninitialize(struct i2c_master_s *dev);

#ifdef CONFIG_BL616CL_I2C_TEST
int bl616cl_i2c_test_install(int port,
                             const struct bl616cl_i2c_test_ops_s *ops,
                             void *arg);
struct i2c_master_s *bl616cl_i2c_test_device(int port);
uint32_t bl616cl_i2c_test_last_status(int port);
#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_I2C_H */
