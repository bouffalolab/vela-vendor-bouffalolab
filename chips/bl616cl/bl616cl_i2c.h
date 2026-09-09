/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_i2c.h
 *
 * SPDX-License-Identifier: Apache-2.0
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
