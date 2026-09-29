/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_flash.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_FLASH_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_FLASH_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_flash_early_init
 *
 * Description:
 *   Enable the second serial flash interface bank (BK2 enable and mode) in
 *   SF_CTRL_2 during early startup.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_flash_early_init(void);

/****************************************************************************
 * Name: bl616cl_flash_initialize
 *
 * Description:
 *   Initialize the SDK LHAL flash state after RAM-safe sections are loaded
 *   and before the system clock is changed, then calibrate the flash sample
 *   delay and switch the flash clock to 80 MHz like SDK board_init(). Placed
 *   in TCM because it runs with XIP disabled.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Zero on success; a nonzero value returned by bflb_flash_init() on
 *   failure, in which case the clock is not changed.
 *
 ****************************************************************************/

int bl616cl_flash_initialize(void);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_FLASH_H */
