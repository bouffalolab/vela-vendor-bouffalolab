/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_sec_eng.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SEC_ENG_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SEC_ENG_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Engine descriptors and bounce buffers live in the non-cacheable RAM
 * alias: the engine does not snoop the CPU data cache.
 */

#define BL616CL_SEC_ENG_NOCACHE \
  locate_data(".nocache_noinit_ram") aligned_data(32)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* The SEC_ENG engines that the drivers serialize independently. */

enum bl616cl_sec_engine_e
{
  BL616CL_SEC_ENGINE_TRNG,
  BL616CL_SEC_ENGINE_AES,
  BL616CL_SEC_ENGINE_NUM
};

/* Run the engine once on len bytes at src, writing to dst (NULL for engines
 * that only consume data). len never exceeds the bounce buffer size. Return
 * zero (OK) or a negated errno value.
 */

typedef CODE int (*bl616cl_sec_eng_run_t)(FAR void *arg,
                                          FAR const uint8_t *src,
                                          FAR uint8_t *dst, size_t len);

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_sec_eng_initialize
 *
 * Description:
 *   Enable the SEC peripheral clock. The clock stays on afterward because
 *   the engines share it. Safe to call more than once.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_sec_eng_initialize(void);

/****************************************************************************
 * Name: bl616cl_sec_eng_lock
 *
 * Description:
 *   Take exclusive use of one SEC_ENG engine and of its bounce buffer.
 *
 * Input Parameters:
 *   engine - The engine to lock.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure, as returned by
 *   nxmutex_lock().
 *
 ****************************************************************************/

int bl616cl_sec_eng_lock(enum bl616cl_sec_engine_e engine);

/****************************************************************************
 * Name: bl616cl_sec_eng_unlock
 *
 * Description:
 *   Release an engine taken with bl616cl_sec_eng_lock().
 *
 * Input Parameters:
 *   engine - The engine to unlock.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_sec_eng_unlock(enum bl616cl_sec_engine_e engine);

/****************************************************************************
 * Name: bl616cl_sec_eng_transfer
 *
 * Description:
 *   Process a buffer of any memory type with one engine. The data is
 *   copied through the engine's non-cacheable bounce buffer in chunks of at
 *   most CONFIG_BL616CL_SEC_ENG_BOUNCE_SIZE bytes, and run() is called once
 *   per chunk with the chunk in the bounce buffer (in place). The caller
 *   holds the engine lock and keeps any chaining state in its engine
 *   descriptor between chunks.
 *
 * Input Parameters:
 *   engine - An engine with a bounce buffer (AES).
 *   src - Input data.
 *   dst - Output buffer of len bytes, or NULL if the engine has no output.
 *   len - Number of bytes; a multiple of the engine block size.
 *   run - Engine operation called for each chunk.
 *   arg - Argument passed to run().
 *
 * Returned Value:
 *   Zero (OK) on success; the first error returned by run() otherwise. The
 *   output is incomplete after an error.
 *
 ****************************************************************************/

int bl616cl_sec_eng_transfer(enum bl616cl_sec_engine_e engine,
                             FAR const uint8_t *src, FAR uint8_t *dst,
                             size_t len, bl616cl_sec_eng_run_t run,
                             FAR void *arg);

#endif
