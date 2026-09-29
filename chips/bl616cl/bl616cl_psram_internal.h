/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_psram_internal.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_PSRAM_INTERNAL_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_PSRAM_INTERNAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <arch/chip/bl616cl_psram.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_psram_initialize
 *
 * Description:
 *   Initialize the on-package Winbond PSRAM during early startup. Check that
 *   the chip has PSRAM, read the factory DQS trim from efuse (or calibrate
 *   when there is none), power and clock the PSRAM, configure its GPIOs,
 *   release TZC access, configure the device and record its size. Failures
 *   are reported on the early console.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *   -ENODEV - the chip has no PSRAM or GPIO is unavailable.
 *   -EINVAL - the factory trim is invalid.
 *   -EIO - power or clock setup, calibration or verification failed.
 *   Other errors are returned by the device configuration, for example -EFBIG
 *   for a device larger than 16 MiB.
 *
 ****************************************************************************/

int bl616cl_psram_initialize(void);

#endif
