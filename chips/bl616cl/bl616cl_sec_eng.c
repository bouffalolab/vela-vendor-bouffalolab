/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_sec_eng.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <string.h>
#include <sys/param.h>

#include <nuttx/cache.h>
#include <nuttx/mutex.h>

#include "bl616cl_lhal.h"
#include "bflb_clock.h"
#include "bflb_l1c.h"
#include "bl616cl_sec_eng.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Largest chunk run on the caller's buffers at a time: far below the
 * engines' 16-bit block counts and their 100 ms limit, and it bounds how
 * long one request holds the engine.
 */

#define BL616CL_SEC_ENG_DIRECT_CHUNK (32 * 1024)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl616cl_sec_engine_s
{
  mutex_t lock;        /* Serializes the engine and its bounce buffer */
  FAR uint8_t *bounce; /* Non-cacheable bounce buffer, NULL if none */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_BL616CL_CRYPTO
static_assert(CONFIG_BL616CL_SEC_ENG_BOUNCE_SIZE % 128 == 0,
              "the bounce buffer holds whole SHA-512 blocks");
#endif

#ifdef CONFIG_BL616CL_CRYPTO_AES
static uint8_t g_bl616cl_sec_aes_bounce[CONFIG_BL616CL_SEC_ENG_BOUNCE_SIZE]
  BL616CL_SEC_ENG_NOCACHE;
#endif

#ifdef CONFIG_BL616CL_CRYPTO_SHA
static uint8_t g_bl616cl_sec_sha_bounce[CONFIG_BL616CL_SEC_ENG_BOUNCE_SIZE]
  BL616CL_SEC_ENG_NOCACHE;
#endif

static struct bl616cl_sec_engine_s g_bl616cl_sec_engines[] =
{
  [BL616CL_SEC_ENGINE_TRNG] =
    {
      NXMUTEX_INITIALIZER,
      NULL
    },

  [BL616CL_SEC_ENGINE_AES] =
    {
      NXMUTEX_INITIALIZER,
#ifdef CONFIG_BL616CL_CRYPTO_AES
      g_bl616cl_sec_aes_bounce
#else
      NULL
#endif
    },

  [BL616CL_SEC_ENGINE_SHA] =
    {
      NXMUTEX_INITIALIZER,
#ifdef CONFIG_BL616CL_CRYPTO_SHA
      g_bl616cl_sec_sha_bounce
#else
      NULL
#endif
    },
};

static_assert(sizeof(g_bl616cl_sec_engines) /
              sizeof(g_bl616cl_sec_engines[0]) == BL616CL_SEC_ENGINE_NUM,
              "one entry per SEC_ENG engine");

#ifdef CONFIG_BL616CL_SEC_ENG_DIRECT
/* Internal cacheable RAM, from the linker script. */

extern uint8_t __bl616cl_cache_ram_start;
extern uint8_t __bl616cl_cache_ram_end;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_BL616CL_SEC_ENG_DIRECT
/****************************************************************************
 * Name: bl616cl_sec_eng_in_ram
 *
 * Description:
 *   Check that a buffer lies in internal cacheable RAM. Flash and PSRAM
 *   are not used directly: the engine has not been verified on them.
 *
 * Input Parameters:
 *   buf - Start of the buffer.
 *   len - Length in bytes.
 *
 * Returned Value:
 *   true if the buffer is in internal RAM.
 *
 ****************************************************************************/

static bool bl616cl_sec_eng_in_ram(FAR const uint8_t *buf, size_t len)
{
  uintptr_t start = (uintptr_t)buf;

  return start >= (uintptr_t)&__bl616cl_cache_ram_start &&
         start <= (uintptr_t)&__bl616cl_cache_ram_end &&
         len <= (uintptr_t)&__bl616cl_cache_ram_end - start;
}

/****************************************************************************
 * Name: bl616cl_sec_eng_can_direct
 *
 * Description:
 *   Decide whether a request can run on the caller's buffers: it is at
 *   least CONFIG_BL616CL_SEC_ENG_DIRECT_MIN bytes, both buffers are in
 *   internal RAM, the input is word aligned, and the output covers whole
 *   cache lines only and does not partly overlap the input. A line shared
 *   with other data could be written back over the engine output.
 *
 * Input Parameters:
 *   src - Input data.
 *   dst - Output buffer, or NULL.
 *   len - Number of bytes.
 *
 * Returned Value:
 *   true to run the request in place.
 *
 ****************************************************************************/

static bool bl616cl_sec_eng_can_direct(FAR const uint8_t *src,
                                       FAR const uint8_t *dst, size_t len)
{
  if (len < CONFIG_BL616CL_SEC_ENG_DIRECT_MIN ||
      ((uintptr_t)src & 3) != 0 || !bl616cl_sec_eng_in_ram(src, len))
    {
      return false;
    }

  if (dst == NULL)
    {
      return true;
    }

  if ((((uintptr_t)dst | len) & (BFLB_CACHE_LINE_SIZE - 1)) != 0 ||
      !bl616cl_sec_eng_in_ram(dst, len))
    {
      return false;
    }

  return dst == src || dst + len <= src || src + len <= dst;
}

/****************************************************************************
 * Name: bl616cl_sec_eng_direct
 *
 * Description:
 *   Run a request on the caller's buffers. The input is cleaned so the
 *   engine reads current data; the output is flushed before the run so no
 *   dirty line is written back over it, and invalidated after it so the
 *   CPU reads what the engine wrote.
 *
 * Input Parameters:
 *   src - Input data.
 *   dst - Output buffer, or NULL.
 *   len - Number of bytes.
 *   run - Engine operation called for each chunk.
 *   arg - Argument passed to run().
 *
 * Returned Value:
 *   Zero (OK) on success; the first error returned by run() otherwise.
 *
 ****************************************************************************/

static int bl616cl_sec_eng_direct(FAR const uint8_t *src, FAR uint8_t *dst,
                                  size_t len, bl616cl_sec_eng_run_t run,
                                  FAR void *arg)
{
  uintptr_t out = (uintptr_t)dst;
  uintptr_t end = out + len;
  size_t chunk;
  int ret = OK;

  up_clean_dcache((uintptr_t)src, (uintptr_t)src + len);
  if (dst != NULL)
    {
      up_flush_dcache(out, end);
    }

  while (len > 0 && ret >= 0)
    {
      chunk = MIN(len, BL616CL_SEC_ENG_DIRECT_CHUNK);
      ret = run(arg, src, dst, chunk);

      src += chunk;
      if (dst != NULL)
        {
          dst += chunk;
        }

      len -= chunk;
    }

  if (out != 0)
    {
      up_invalidate_dcache(out, end);
    }

  return ret;
}
#endif

/****************************************************************************
 * Public Functions
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

void bl616cl_sec_eng_initialize(void)
{
  PERIPHERAL_CLOCK_SEC_ENABLE();
}

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

int bl616cl_sec_eng_lock(enum bl616cl_sec_engine_e engine)
{
  DEBUGASSERT(engine < BL616CL_SEC_ENGINE_NUM);
  return nxmutex_lock(&g_bl616cl_sec_engines[engine].lock);
}

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

void bl616cl_sec_eng_unlock(enum bl616cl_sec_engine_e engine)
{
  DEBUGASSERT(engine < BL616CL_SEC_ENGINE_NUM);
  nxmutex_unlock(&g_bl616cl_sec_engines[engine].lock);
}

#ifdef CONFIG_BL616CL_CRYPTO
/****************************************************************************
 * Name: bl616cl_sec_eng_transfer
 *
 * Description:
 *   Process a buffer of any memory type with one engine. The data is
 *   copied through the engine's non-cacheable bounce buffer in chunks of at
 *   most CONFIG_BL616CL_SEC_ENG_BOUNCE_SIZE bytes, and run() is called once
 *   per chunk with the chunk in the bounce buffer (in place). With
 *   CONFIG_BL616CL_SEC_ENG_DIRECT, a large request whose buffers the engine
 *   can use safely is instead run on the caller's buffers, with the data
 *   cache cleaned and invalidated around it. The caller holds the engine
 *   lock and keeps any chaining state in its engine descriptor between
 *   chunks.
 *
 * Input Parameters:
 *   engine - An engine with a bounce buffer (AES or SHA).
 *   src - Input data.
 *   dst - Output buffer of len bytes, or NULL if the engine has no output.
 *   len - Number of bytes; a multiple of the engine block size.
 *   direct - True if run() may be given the caller's buffers.
 *   run - Engine operation called for each chunk.
 *   arg - Argument passed to run().
 *
 * Returned Value:
 *   Zero (OK) on success; the first error returned by run() otherwise. The
 *   output is incomplete after an error. After a timeout the engine may
 *   still finish the chunk later and write its output, on the direct path
 *   into the caller's buffer.
 *
 ****************************************************************************/

int bl616cl_sec_eng_transfer(enum bl616cl_sec_engine_e engine,
                             FAR const uint8_t *src, FAR uint8_t *dst,
                             size_t len, bool direct,
                             bl616cl_sec_eng_run_t run, FAR void *arg)
{
  FAR uint8_t *bounce;
  size_t chunk;
  int ret;

  DEBUGASSERT(engine < BL616CL_SEC_ENGINE_NUM);
  bounce = g_bl616cl_sec_engines[engine].bounce;
  DEBUGASSERT(bounce != NULL);

#ifdef CONFIG_BL616CL_SEC_ENG_DIRECT
  if (direct && bl616cl_sec_eng_can_direct(src, dst, len))
    {
      return bl616cl_sec_eng_direct(src, dst, len, run, arg);
    }
#endif

  while (len > 0)
    {
      chunk = MIN(len, CONFIG_BL616CL_SEC_ENG_BOUNCE_SIZE);

      memcpy(bounce, src, chunk);
      ret = run(arg, bounce, dst != NULL ? bounce : NULL, chunk);
      if (ret < 0)
        {
          return ret;
        }

      if (dst != NULL)
        {
          memcpy(dst, bounce, chunk);
          dst += chunk;
        }

      src += chunk;
      len -= chunk;
    }

  return OK;
}
#endif
