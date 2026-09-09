/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_systemreset.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SYSTEMRESET_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SYSTEMRESET_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bl616cl_reset_reason_e
{
  BL616CL_RESET_POWER_ON = 0,
  BL616CL_RESET_WATCHDOG,
  BL616CL_RESET_FATAL,
  BL616CL_RESET_SOFTWARE,
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BOARDCTL_RESET_CAUSE
void bl616cl_reset_reason_initialize(void);
void bl616cl_reset_reason_set(enum bl616cl_reset_reason_e reason);
enum bl616cl_reset_reason_e bl616cl_reset_reason_get(void);
#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SYSTEMRESET_H */
