/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/include/bl616cl_psram.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_PSRAM_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_PSRAM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stddef.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_PSRAM_BASE          (0x88000000)
#define BL616CL_PSRAM_NOCACHE_BASE  0x18000000u

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BL616CL_PSRAM
/* Returns the usable heap region size, or zero if initialization failed.
 * The uncached alias accesses the same storage. Clients must synchronize
 * caches before accessing an allocation through that alias.
 */

size_t bl616cl_psram_size_get(void);
#endif

#endif
