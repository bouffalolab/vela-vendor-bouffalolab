/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_cache.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_CACHE_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_CACHE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_cache_early_init
 *
 * Description:
 *   Enable the D-cache and I-cache with csi_dcache_enable() and
 *   csi_icache_enable(). Called from __bl616cl_start() after
 *   bl616cl_pmp_init() and before start_load().
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void bl616cl_cache_early_init(void);

/****************************************************************************
 * Name: bl616cl_cache_after_load
 *
 * Description:
 *   Make the caches coherent with memory written while loading sections:
 *   clean the whole D-cache (__DCACHE_CALL) and invalidate the whole I-cache
 *   (__ICACHE_IALL), with barriers around each step. Called from
 *   __bl616cl_start() after start_load().
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void bl616cl_cache_after_load(void);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_CACHE_H */
