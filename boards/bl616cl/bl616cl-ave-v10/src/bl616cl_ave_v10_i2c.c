/****************************************************************************
 * vendor/bouffalolab/boards/bl616cl/bl616cl-ave-v10/src/bl616cl_ave_v10_i2c.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <nuttx/i2c/i2c_master.h>

#include "bl616cl_i2c.h"
#include "bl616cl_ave_v10.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_ave_v10_i2c_initialize
 *
 * Description:
 *   Register I2C0 as /dev/i2c0 on BL616CL_AVE_V10_I2C0_SCL_PIN (GPIO20) and
 *   BL616CL_AVE_V10_I2C0_SDA_PIN (GPIO11). The bus is uninitialized again
 *   if registration fails.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero (OK) on success; -ENODEV if the bus cannot be initialized, or the
 *   negated errno value from i2c_register().
 *
 ****************************************************************************/

int bl616cl_ave_v10_i2c_initialize(void)
{
  struct i2c_master_s *i2c;
  int ret;

  i2c = bl616cl_i2cbus_initialize(0, BL616CL_AVE_V10_I2C0_SCL_PIN,
                                  BL616CL_AVE_V10_I2C0_SDA_PIN);
  if (i2c == NULL)
    {
      return -ENODEV;
    }

  ret = i2c_register(i2c, 0);
  if (ret < 0)
    {
      (void)bl616cl_i2cbus_uninitialize(i2c);
    }

  return ret;
}
