/****************************************************************************
 * vendor/bouffalolab/components/display/touch/touch_gt911.c
 *
 * GT911 touch controller. The register use is the SDK's
 * bsp/common/touch/gt911_i2c.c (configuration not overwritten, one status
 * read, a point read from the status register, status cleared after each
 * read); only the bus accesses go through NuttX I2C.
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

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <sys/param.h>
#include <syslog.h>

#include <nuttx/i2c/i2c_master.h>

#include "touch_poll.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define GT911_I2C_SLAVE_ADDR       (0xba >> 1)
/* The SDK runs the bus at 200 kHz; the BL616CL I2C driver supports 100 and
 * 400 kHz, so take the lower one.
 */

#define GT911_I2C_FREQ             100000

#define GT911_READ_XY_REG          0x814e  /* Coordinate status register */
#define GT911_STATUS_READY         0x80    /* Status: a new frame is ready */
#define GT911_STATUS_POINTS        0x0f    /* Status: number of points */
#define GT911_PRODUCT_ID_REG       0x8140  /* Chip ID */
#define GT911_FIRMWARE_VERSION_REG 0x8144  /* Firmware version number */
#define GT911_X_RESOLUTION         0x8146  /* X/Y coordinate resolution */

#define GT911_PRODUCT_ID           0x00313139 /* "911" */
#define GT911_I2C_MAX_POINT        5

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int gt911_i2c_read_byte(struct i2c_master_s *i2c,
                               uint16_t register_addr, uint8_t *data_buf,
                               uint16_t len)
{
  uint8_t addr[2] =
  {
    register_addr >> 8, register_addr & 0xff
  };

  struct i2c_msg_s msg[2] =
  {
    {
      .frequency = GT911_I2C_FREQ,
      .addr = GT911_I2C_SLAVE_ADDR,
      .buffer = addr,
      .length = sizeof(addr),
    },
    {
      .frequency = GT911_I2C_FREQ,
      .addr = GT911_I2C_SLAVE_ADDR,
      .flags = I2C_M_READ,
      .buffer = data_buf,
      .length = len,
    },
  };

  return I2C_TRANSFER(i2c, msg, 2) < 0 ? -1 : 0;
}

static int gt911_i2c_write_byte(struct i2c_master_s *i2c,
                                uint16_t register_addr, uint8_t data)
{
  uint8_t buf[3] =
  {
    register_addr >> 8, register_addr & 0xff, data
  };

  struct i2c_msg_s msg =
  {
    .frequency = GT911_I2C_FREQ,
    .addr = GT911_I2C_SLAVE_ADDR,
    .buffer = buf,
    .length = sizeof(buf),
  };

  return I2C_TRANSFER(i2c, &msg, 1) < 0 ? -1 : 0;
}

static int gt911_i2c_init(struct i2c_master_s *i2c)
{
  uint8_t data_buf[5] =
  {
    0
  };

  uint32_t product_id;
  uint16_t x_max;
  uint16_t y_max;

  if (gt911_i2c_read_byte(i2c, GT911_PRODUCT_ID_REG, data_buf, 4))
    {
      syslog(LOG_ERR, "touch i2c read error\n");
      return -1;
    }

  product_id = (uint32_t)data_buf[3] << 24 | (uint32_t)data_buf[2] << 16 |
               (uint32_t)data_buf[1] << 8 | data_buf[0];
  syslog(LOG_INFO, "Touch Product ID: 0x%08" PRIx32 "\n", product_id);

  /* The SDK keeps the configuration of the controller
   * (GT911_CFG_OVER_WRITE 0).
   */

  if (product_id != GT911_PRODUCT_ID)
    {
      syslog(LOG_ERR, "Touch Product ID read fail!\n");
      return -1;
    }

  if (gt911_i2c_read_byte(i2c, GT911_FIRMWARE_VERSION_REG, data_buf, 2))
    {
      return -1;
    }

  syslog(LOG_INFO, "Touch Firmware Version: 0x%04x\n",
         (unsigned int)(data_buf[1] << 8 | data_buf[0]));

  if (gt911_i2c_read_byte(i2c, GT911_X_RESOLUTION, data_buf, 4))
    {
      return -1;
    }

  x_max = (uint16_t)data_buf[1] << 8 | data_buf[0];
  y_max = (uint16_t)data_buf[3] << 8 | data_buf[2];
  syslog(LOG_INFO, "Touch Resolution %ux%u\n", x_max, y_max);
  return 0;
}

static int gt911_i2c_read(struct i2c_master_s *i2c, uint8_t *point_num,
                          touch_coord_t *touch_coord, uint8_t max_num)
{
  uint8_t data_buf[8 * GT911_I2C_MAX_POINT] =
  {
    0
  };

  uint8_t read_num;
  uint8_t i;

  if (point_num == NULL || touch_coord == NULL || max_num == 0)
    {
      return -1;
    }

  *point_num = 0;
  if (gt911_i2c_read_byte(i2c, GT911_READ_XY_REG, data_buf, 1))
    {
      return -EIO;
    }

  /* Vela: the SDK takes the whole status byte as the number of points and
   * does not check the point read, so a frame of zero points (the finger
   * lifted) or a failed read gives a stale or (0,0) point. Only a ready
   * frame counts; its low bits are the number of points.
   */

  if ((data_buf[0] & GT911_STATUS_READY) == 0)
    {
      return -EAGAIN;
    }

  read_num = MIN(data_buf[0] & GT911_STATUS_POINTS, GT911_I2C_MAX_POINT);
  read_num = MIN(read_num, max_num);

  /* Read from the status register, points 8 bytes apart, as the SDK */

  if (read_num > 0 &&
      gt911_i2c_read_byte(i2c, GT911_READ_XY_REG, data_buf, 8 * read_num))
    {
      return -EIO;
    }

  for (i = 0; i < read_num; i++)
    {
      uint8_t *p_data = &data_buf[i * 8];

      touch_coord[i].coord_x = (uint16_t)p_data[3] << 8 | p_data[2];
      touch_coord[i].coord_y = (uint16_t)p_data[5] << 8 | p_data[4];
    }

  *point_num = read_num;
  gt911_i2c_write_byte(i2c, GT911_READ_XY_REG, 0);
  return 0;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

const struct touch_chip_s g_touch_gt911 =
{
  .name = "gt911",
  .init = gt911_i2c_init,
  .read = gt911_i2c_read,
};
