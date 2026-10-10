/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_sec_aes.c
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
#include <errno.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/kmalloc.h>

#include "bl616cl_lhal.h"
#include "bflb_core.h"
#include "bflb_sec_aes.h"
#include "bl616cl_sec_eng.h"
#include "bl616cl_sec_aes.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl616cl_aes_s
{
  struct bflb_aes_link_s desc; /* Key, mode and chaining value; the engine
                                * fields are set for each operation */
  int error;                   /* Sticky error after an engine failure */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bflb_device_s *g_bl616cl_aes;

/* The descriptor the engine runs. A session descriptor is copied in under
 * the engine lock and its chaining value, written back by the engine, is
 * copied out afterward.
 */

static struct bflb_aes_link_s g_bl616cl_aes_desc BL616CL_SEC_ENG_NOCACHE;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_sec_aes_run
 *
 * Description:
 *   Run the working descriptor on one chunk. Called by
 *   bl616cl_sec_eng_transfer() with the AES engine locked.
 *
 * Input Parameters:
 *   arg - Unused.
 *   src - Input in engine-visible memory.
 *   dst - Output in engine-visible memory.
 *   len - Number of bytes, a multiple of the block size.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value from
 *   bflb_aes_link_update().
 *
 ****************************************************************************/

static int bl616cl_sec_aes_run(FAR void *arg, FAR const uint8_t *src,
                               FAR uint8_t *dst, size_t len)
{
  UNUSED(arg);

  return bflb_aes_link_update(g_bl616cl_aes,
                              (uint32_t)(uintptr_t)&g_bl616cl_aes_desc,
                              src, dst, len);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_sec_aes_initialize
 *
 * Description:
 *   Obtain the SEC AES device and enable the SEC clock.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Zero (OK) on success; -ENODEV if the AES device is unavailable.
 *
 ****************************************************************************/

int bl616cl_sec_aes_initialize(void)
{
  bl616cl_sec_eng_initialize();

  g_bl616cl_aes = bflb_device_get_by_name(BFLB_NAME_SEC_AES);
  if (g_bl616cl_aes == NULL)
    {
      syslog(LOG_ERR, "BL616CL AES device is unavailable\n");
      return -ENODEV;
    }

  return OK;
}

/****************************************************************************
 * Name: bl616cl_sec_aes_new
 *
 * Description:
 *   Allocate an AES context. The chaining value starts at zero.
 *
 * Input Parameters:
 *   aes - Receives the new context.
 *   mode - Block cipher mode.
 *   key - Key bytes; only read during this call.
 *   keylen - Key length in bytes: 16, 24 or 32.
 *
 * Returned Value:
 *   Zero (OK) on success; -EINVAL for an unsupported key length; -ENOMEM
 *   if memory is exhausted.
 *
 ****************************************************************************/

int bl616cl_sec_aes_new(FAR struct bl616cl_aes_s **aes,
                        enum bl616cl_aes_mode_e mode,
                        FAR const uint8_t *key, size_t keylen)
{
  FAR struct bl616cl_aes_s *ctx;
  uint32_t words[8];
  uint8_t keysize;

  switch (keylen)
    {
      case 16:
        keysize = AES_LINK_KEY_128BITS;
        break;

      case 24:
        keysize = AES_LINK_KEY_192BITS;
        break;

      case 32:
        keysize = AES_LINK_KEY_256BITS;
        break;

      default:
        return -EINVAL;
    }

  DEBUGASSERT(mode == BL616CL_AES_CBC);

  ctx = kmm_zalloc(sizeof(*ctx));
  if (ctx == NULL)
    {
      return -ENOMEM;
    }

  /* The descriptor holds the key bytes in memory order. */

  memset(words, 0, sizeof(words));
  memcpy(words, key, keylen);

  ctx->desc.aes_key = keysize;
  ctx->desc.aes_mode = AES_MODE_CBC;
  ctx->desc.aes_key0 = words[0];
  ctx->desc.aes_key1 = words[1];
  ctx->desc.aes_key2 = words[2];
  ctx->desc.aes_key3 = words[3];
  ctx->desc.aes_key4 = words[4];
  ctx->desc.aes_key5 = words[5];
  ctx->desc.aes_key6 = words[6];
  ctx->desc.aes_key7 = words[7];
  explicit_bzero(words, sizeof(words));

  *aes = ctx;
  return OK;
}

/****************************************************************************
 * Name: bl616cl_sec_aes_free
 *
 * Description:
 *   Wipe and free an AES context.
 *
 * Input Parameters:
 *   aes - The context, or NULL.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_sec_aes_free(FAR struct bl616cl_aes_s *aes)
{
  if (aes != NULL)
    {
      explicit_bzero(aes, sizeof(*aes));
      kmm_free(aes);
    }
}

/****************************************************************************
 * Name: bl616cl_sec_aes_setiv
 *
 * Description:
 *   Set the chaining value (CBC IV) for the next operation.
 *
 * Input Parameters:
 *   aes - The context.
 *   iv - BL616CL_AES_BLOCK_SIZE bytes.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_sec_aes_setiv(FAR struct bl616cl_aes_s *aes,
                           FAR const uint8_t *iv)
{
  uint32_t words[4];

  memcpy(words, iv, sizeof(words));
  aes->desc.aes_iv0 = words[0];
  aes->desc.aes_iv1 = words[1];
  aes->desc.aes_iv2 = words[2];
  aes->desc.aes_iv3 = words[3];
}

/****************************************************************************
 * Name: bl616cl_sec_aes_crypt
 *
 * Description:
 *   Encrypt or decrypt with the SEC AES engine in link mode. The operation
 *   continues from the chaining value left by the previous one, so a
 *   message can be processed in several calls. After an engine failure
 *   the context is unusable and every later call returns the same error.
 *
 * Input Parameters:
 *   aes - The context.
 *   encrypt - True to encrypt, false to decrypt.
 *   src - Input data.
 *   dst - Output buffer of len bytes; may equal src.
 *   len - Number of bytes, a multiple of BL616CL_AES_BLOCK_SIZE.
 *
 * Returned Value:
 *   Zero (OK) on success; -EINVAL if len is not a multiple of the block
 *   size; -ETIMEDOUT if the engine does not finish; another negated errno
 *   value if the engine lock fails.
 *
 ****************************************************************************/

int bl616cl_sec_aes_crypt(FAR struct bl616cl_aes_s *aes, bool encrypt,
                          FAR const uint8_t *src, FAR uint8_t *dst,
                          size_t len)
{
  int ret;

  if (aes->error < 0)
    {
      return aes->error;
    }

  if (len % BL616CL_AES_BLOCK_SIZE != 0)
    {
      return -EINVAL;
    }

  if (len == 0)
    {
      return OK;
    }

  ret = bl616cl_sec_eng_lock(BL616CL_SEC_ENGINE_AES);
  if (ret < 0)
    {
      return ret;
    }

  aes->desc.aes_dec_en = !encrypt;
  g_bl616cl_aes_desc = aes->desc;

  /* Set EN and LINK_MODE again on every use: another user may have left
   * the engine in non-link mode, and a link operation would then write to
   * a stale buffer.
   */

  bflb_aes_link_init(g_bl616cl_aes);

  ret = bl616cl_sec_eng_transfer(BL616CL_SEC_ENGINE_AES, src, dst, len,
                                 bl616cl_sec_aes_run, NULL);
  if (ret < 0)
    {
      /* Stop the engine. The chaining value is unknown now, so the
       * session cannot continue.
       */

      bflb_aes_link_deinit(g_bl616cl_aes);
      aes->error = ret;
    }
  else
    {
      /* The engine wrote the next chaining value into the descriptor. */

      aes->desc.aes_iv0 = g_bl616cl_aes_desc.aes_iv0;
      aes->desc.aes_iv1 = g_bl616cl_aes_desc.aes_iv1;
      aes->desc.aes_iv2 = g_bl616cl_aes_desc.aes_iv2;
      aes->desc.aes_iv3 = g_bl616cl_aes_desc.aes_iv3;
    }

  explicit_bzero(&g_bl616cl_aes_desc, sizeof(g_bl616cl_aes_desc));
  bl616cl_sec_eng_unlock(BL616CL_SEC_ENGINE_AES);
  return ret;
}
