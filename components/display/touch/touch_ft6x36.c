/****************************************************************************
 * vendor/bouffalolab/components/display/touch/touch_ft6x36.c
 *
 * FT6x36 touch controller. The register use is the SDK's
 * bsp/common/touch/ft6x36_i2c.c (identification registers printed at
 * init, point count from TD_STATUS, up to two points); only the bus
 * accesses go through NuttX I2C.
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

#include <stdint.h>
#include <syslog.h>
#include <sys/param.h>

#include <nuttx/i2c/i2c_master.h>

#include "touch_poll.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FT6236_I2C_SLAVE_ADDR   0x38
/* The SDK runs the bus at 200 kHz; the BL616CL I2C driver supports 100 and
 * 400 kHz, so take the lower one.
 */

#define FT6X36_I2C_FREQ             100000

#define FT6X36_DEV_MODE_REG     0x00
#define FT6X36_TD_STAT_REG      0x02 /* Number of active touch points */
#define FT6X36_TD_STAT_MASK     0x0f
#define FT6X36_P1_XH_REG        0x03
#define FT6X36_P2_XH_REG        0x09
#define FT6X36_MSB_MASK         0x0f
#define FT6X36_LSB_MASK         0xff
#define FT6X36_CHIPSELECT_REG   0xa3
#define FT6X36_FIRMWARE_ID_REG  0xa6
#define FT6X36_RELEASECODE_REG  0xaf
#define FT6X36_PANEL_ID_REG     0xa8

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct ft6x36_reg_s
{
  uint8_t reg;
  const char *name;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Identification registers printed at init, as the SDK */

static const struct ft6x36_reg_s g_ft6x36_id_regs[] =
{
  { FT6X36_PANEL_ID_REG, "Device ID" },
  { FT6X36_CHIPSELECT_REG, "Chip ID" },
  { FT6X36_DEV_MODE_REG, "Device mode" },
  { FT6X36_FIRMWARE_ID_REG, "Firmware ID" },
  { FT6X36_RELEASECODE_REG, "Release code" },
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int ft6x36_i2c_read_byte(struct i2c_master_s *i2c,
                                uint8_t register_addr, uint8_t *data_buf,
                                uint16_t len)
{
  struct i2c_msg_s msg[2] =
  {
    {
      .frequency = FT6X36_I2C_FREQ,
      .addr = FT6236_I2C_SLAVE_ADDR,
      .buffer = &register_addr,
      .length = 1,
    },
    {
      .frequency = FT6X36_I2C_FREQ,
      .addr = FT6236_I2C_SLAVE_ADDR,
      .flags = I2C_M_READ,
      .buffer = data_buf,
      .length = len,
    },
  };

  return I2C_TRANSFER(i2c, msg, 2) < 0 ? -1 : 0;
}

static int ft6x36_i2c_init(struct i2c_master_s *i2c)
{
  uint8_t data_buf;
  int i;

  for (i = 0; i < nitems(g_ft6x36_id_regs); i++)
    {
      if (ft6x36_i2c_read_byte(i2c, g_ft6x36_id_regs[i].reg, &data_buf, 1))
        {
          return -1;
        }

      syslog(LOG_INFO, "Touch %s: 0x%02x\n", g_ft6x36_id_regs[i].name,
             data_buf);
    }

  return 0;
}

static int ft6x36_i2c_read(struct i2c_master_s *i2c, uint8_t *point_num,
                           touch_coord_t *touch_coord, uint8_t max_num)
{
  uint8_t data_buf[5];

  if (point_num == NULL || touch_coord == NULL || max_num == 0)
    {
      return -1;
    }

  *point_num = 0;
  if (ft6x36_i2c_read_byte(i2c, FT6X36_TD_STAT_REG, &data_buf[0], 1))
    {
      return -1;
    }

  data_buf[0] &= FT6X36_TD_STAT_MASK;

  /* No touch, or an error */

  if (data_buf[0] == 0 || data_buf[0] > 2)
    {
      return data_buf[0] > 2 ? -2 : 0;
    }

  /* The first point. Vela: unlike the SDK, a failed read is an error, not
   * a point.
   */

  if (ft6x36_i2c_read_byte(i2c, FT6X36_P1_XH_REG, &data_buf[1], 4))
    {
      return -1;
    }

  touch_coord[0].coord_x = (data_buf[1] & FT6X36_MSB_MASK) << 8 |
                           (data_buf[2] & FT6X36_LSB_MASK);
  touch_coord[0].coord_y = (data_buf[3] & FT6X36_MSB_MASK) << 8 |
                           (data_buf[4] & FT6X36_LSB_MASK);
  *point_num = 1;

  /* The second point */

  if (data_buf[0] > 1 && max_num > 1)
    {
      if (ft6x36_i2c_read_byte(i2c, FT6X36_P2_XH_REG, &data_buf[1], 4))
        {
          return -1;
        }

      touch_coord[1].coord_x = (data_buf[1] & FT6X36_MSB_MASK) << 8 |
                               (data_buf[2] & FT6X36_LSB_MASK);
      touch_coord[1].coord_y = (data_buf[3] & FT6X36_MSB_MASK) << 8 |
                               (data_buf[4] & FT6X36_LSB_MASK);
      *point_num = 2;
    }

  return 0;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

const struct touch_chip_s g_touch_ft6x36 =
{
  .name = "ft6x36",
  .init = ft6x36_i2c_init,
  .read = ft6x36_i2c_read,
};
