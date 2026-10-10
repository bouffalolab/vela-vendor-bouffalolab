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

#include <nuttx/mutex.h>

#include "bl616cl_lhal.h"
#include "bflb_clock.h"
#include "bl616cl_sec_eng.h"

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
 *   per chunk with the chunk in the bounce buffer (in place). The caller
 *   holds the engine lock and keeps any chaining state in its engine
 *   descriptor between chunks.
 *
 * Input Parameters:
 *   engine - An engine with a bounce buffer (AES or SHA).
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
                             FAR void *arg)
{
  FAR uint8_t *bounce;
  size_t chunk;
  int ret;

  DEBUGASSERT(engine < BL616CL_SEC_ENGINE_NUM);
  bounce = g_bl616cl_sec_engines[engine].bounce;
  DEBUGASSERT(bounce != NULL);

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
