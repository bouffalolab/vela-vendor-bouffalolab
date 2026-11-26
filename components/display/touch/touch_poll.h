/****************************************************************************
 * vendor/bouffalolab/components/display/touch/touch_poll.h
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

#ifndef __VENDOR_BOUFFALOLAB_COMPONENTS_DISPLAY_TOUCH_TOUCH_POLL_H
#define __VENDOR_BOUFFALOLAB_COMPONENTS_DISPLAY_TOUCH_TOUCH_POLL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/i2c/i2c_master.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Touch point in controller coordinates, as on the SDK */

typedef struct
{
  uint16_t coord_x;
  uint16_t coord_y;
} touch_coord_t;

/* I2C touch controller driver, the SDK's <chip>_i2c_init/_read pair */

struct touch_chip_s
{
  const char *name;

  /* Check the controller and print its identification. Returns 0, or a
   * negative value if it does not answer as expected.
   */

  int (*init)(struct i2c_master_s *i2c);

  /* Read up to max_num points into coord and their number into point_num
   * (0: no contact). Returns 0, or a negative value when there is no new
   * frame or the read failed; the contact state is then left as it was.
   */

  int (*read)(struct i2c_master_s *i2c, uint8_t *point_num,
              touch_coord_t *coord, uint8_t max_num);
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef CONFIG_BL_COMPONENT_TOUCH_GT911
extern const struct touch_chip_s g_touch_gt911;
#endif

#ifdef CONFIG_BL_COMPONENT_TOUCH_FT6X36
extern const struct touch_chip_s g_touch_ft6x36;
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: touch_poll_register
 *
 * Description:
 *   Initialize a touch controller and register it as a NuttX touchscreen.
 *   A low-priority work reads one point every period_ms, as the SDK's LVGL
 *   input port does, and reports TOUCH_DOWN when a contact appears,
 *   TOUCH_MOVE when its position changes and TOUCH_UP when it is lost.
 *   Coordinates are the controller's, unscaled.
 *
 * Input Parameters:
 *   i2c       - I2C bus of the controller.
 *   chip      - Controller driver.
 *   path      - Device path, such as "/dev/input0".
 *   period_ms - Poll period in milliseconds.
 *
 * Returned Value:
 *   Zero (OK) on success; -ENODEV if the controller does not answer,
 *   -ENOMEM, or the negated errno value from touch_register().
 *
 ****************************************************************************/

int touch_poll_register(struct i2c_master_s *i2c,
                        const struct touch_chip_s *chip, const char *path,
                        uint32_t period_ms);

#endif /* __VENDOR_BOUFFALOLAB_COMPONENTS_DISPLAY_TOUCH_TOUCH_POLL_H */
