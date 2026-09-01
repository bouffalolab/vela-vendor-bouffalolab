/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/include/bl616cl_cache_test.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIP_BL616CL_INCLUDE_BL616CL_CACHE_TEST_H
#define __VENDOR_BOUFFALOLAB_CHIP_BL616CL_INCLUDE_BL616CL_CACHE_TEST_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_BL616CL_CACHE_TEST
enum bl616cl_cache_test_event_e
{
  BL616CL_CACHE_TEST_EVENT_OPERATION,
  BL616CL_CACHE_TEST_EVENT_NOOP,
  BL616CL_CACHE_TEST_EVENT_REJECT
};

enum bl616cl_cache_test_operation_e
{
  BL616CL_CACHE_TEST_ICACHE_ENABLE,
  BL616CL_CACHE_TEST_ICACHE_DISABLE,
  BL616CL_CACHE_TEST_ICACHE_INVALIDATE,
  BL616CL_CACHE_TEST_ICACHE_INVALIDATE_ALL,
  BL616CL_CACHE_TEST_DCACHE_ENABLE,
  BL616CL_CACHE_TEST_DCACHE_DISABLE,
  BL616CL_CACHE_TEST_DCACHE_CLEAN,
  BL616CL_CACHE_TEST_DCACHE_INVALIDATE,
  BL616CL_CACHE_TEST_DCACHE_FLUSH,
  BL616CL_CACHE_TEST_DCACHE_CLEAN_ALL,
  BL616CL_CACHE_TEST_DCACHE_INVALIDATE_ALL,
  BL616CL_CACHE_TEST_DCACHE_FLUSH_ALL,
  BL616CL_CACHE_TEST_COHERENT
};

struct bl616cl_cache_test_event_s
{
  enum bl616cl_cache_test_event_e event;
  enum bl616cl_cache_test_operation_e operation;
  uintptr_t addr;
  uintptr_t size;
};

typedef void (*bl616cl_cache_test_hook_t)(
  const struct bl616cl_cache_test_event_s *event, void *arg);

void bl616cl_cache_test_configure(bl616cl_cache_test_hook_t hook, void *arg,
                                  uintptr_t chunk_limit, bool bypass);
uint32_t bl616cl_cache_test_mhcr(void);
#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIP_BL616CL_INCLUDE_BL616CL_CACHE_TEST_H */
