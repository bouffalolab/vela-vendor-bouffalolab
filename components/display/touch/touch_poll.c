/****************************************************************************
 * vendor/bouffalolab/components/display/touch/touch_poll.c
 *
 * Polled NuttX touchscreen lower half for the I2C touch controllers. The
 * controllers have no interrupt line on the boards that use them, so a
 * low-priority work reads one point periodically, like the SDK's LVGL
 * input port (lv_port_indev.c touchpad_read).
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
#include <stdbool.h>
#include <string.h>
#include <sys/param.h>

#include <nuttx/clock.h>
#include <nuttx/input/touchscreen.h>
#include <nuttx/kmalloc.h>
#include <nuttx/wqueue.h>

#include "touch_poll.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_SCHED_LPWORK
#  error "The touch poll work needs CONFIG_SCHED_LPWORK"
#endif

/* Samples each reader of the device can queue */

#define TOUCH_POLL_NSAMPLES 8

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct touch_poll_s
{
  struct touch_lowerhalf_s lower;  /* NuttX lower half, must be first */
  struct i2c_master_s *i2c;        /* Bus of the controller */
  const struct touch_chip_s *chip; /* Controller driver */
  struct work_s work;              /* Poll work */
  clock_t period;                  /* Poll period in ticks */
  touch_coord_t last;              /* Last reported position */
  bool down;                       /* A contact is reported */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void touch_poll_report(struct touch_poll_s *priv, uint8_t flags)
{
  struct touch_sample_s sample;

  memset(&sample, 0, sizeof(sample));
  sample.npoints = 1;
  sample.point[0].flags = flags | TOUCH_ID_VALID | TOUCH_POS_VALID;
  sample.point[0].x = priv->last.coord_x;
  sample.point[0].y = priv->last.coord_y;
  sample.point[0].timestamp = touch_get_time();
  touch_event(priv->lower.priv, &sample);
}

static void touch_poll_worker(void *arg)
{
  struct touch_poll_s *priv = arg;
  touch_coord_t coord;
  uint8_t num = 0;

  if (priv->chip->read(priv->i2c, &num, &coord, 1) >= 0)
    {
      if (num > 0)
        {
          if (!priv->down || coord.coord_x != priv->last.coord_x ||
              coord.coord_y != priv->last.coord_y)
            {
              priv->last = coord;
              touch_poll_report(priv, priv->down ? TOUCH_MOVE : TOUCH_DOWN);
              priv->down = true;
            }
        }
      else if (priv->down)
        {
          touch_poll_report(priv, TOUCH_UP);
          priv->down = false;
        }
    }

  work_queue(LPWORK, &priv->work, touch_poll_worker, priv, priv->period);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int touch_poll_register(struct i2c_master_s *i2c,
                        const struct touch_chip_s *chip, const char *path,
                        uint32_t period_ms)
{
  struct touch_poll_s *priv;
  int ret;

  if (chip->init(i2c) < 0)
    {
      return -ENODEV;
    }

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  priv->lower.maxpoint = 1;
  priv->i2c = i2c;
  priv->chip = chip;
  priv->period = MAX(MSEC2TICK(period_ms), 1);

  ret = touch_register(&priv->lower, path, TOUCH_POLL_NSAMPLES);
  if (ret < 0)
    {
      kmm_free(priv);
      return ret;
    }

  return work_queue(LPWORK, &priv->work, touch_poll_worker, priv,
                    priv->period);
}
