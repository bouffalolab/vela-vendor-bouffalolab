/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_gpio.h
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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_GPIO_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_GPIO_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/ioexpander/ioexpander.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_gpio_initialize
 *
 * Description:
 *   Initialize the BL616CL GPIO controller as an I/O expander device.
 *   Board code uses gpio_lower_half() on the returned instance to expose
 *   individual pins as /dev/gpioN character devices.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   A pointer to the I/O expander instance on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct ioexpander_dev_s *bl616cl_gpio_initialize(void);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_GPIO_H */
