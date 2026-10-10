/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_sec_sha.c
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

#include <errno.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/kmalloc.h>

#include "bl616cl_lhal.h"
#include "bflb_core.h"
#include "bflb_sec_sha.h"
#include "bl616cl_sec_eng.h"
#include "bl616cl_sec_sha.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* The LHAL link context: descriptor, hash state, buffered tail and
 * padding. The descriptor holds its own absolute address.
 */

union bl616cl_sha_ctx_u
{
  struct bflb_sha1_link_ctx_s sha1;     /* SHA-1 */
  struct bflb_sha256_link_ctx_s sha256; /* SHA-224, SHA-256 */
  struct bflb_sha512_link_ctx_s sha512; /* SHA-384, SHA-512 */
};

struct bl616cl_sha_s
{
  union bl616cl_sha_ctx_u ctx;  /* State between operations */
  enum bl616cl_sha_alg_e alg;   /* Hash algorithm */
  int error;                    /* Sticky error after an engine failure */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bflb_device_s *g_bl616cl_sha;

/* The context the engine runs. The engine reads the descriptor, the tail
 * and the padding from it and writes the hash state into it, so a session
 * context is copied in under the engine lock and copied out afterward.
 */

static union bl616cl_sha_ctx_u g_bl616cl_sha_ctx BL616CL_SEC_ENG_NOCACHE;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_sec_sha_start
 *
 * Description:
 *   Reset a session context for a new message. Touches no registers.
 *
 * Input Parameters:
 *   sha - The context.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_sec_sha_start(FAR struct bl616cl_sha_s *sha)
{
  switch (sha->alg)
    {
      case BL616CL_SHA1:
        bflb_sha1_link_start(g_bl616cl_sha, &sha->ctx.sha1);
        break;

      case BL616CL_SHA224:
      case BL616CL_SHA256:
        bflb_sha256_link_start(g_bl616cl_sha, &sha->ctx.sha256,
                               sha->alg == BL616CL_SHA224);
        break;

      default:
        bflb_sha512_link_start(g_bl616cl_sha, &sha->ctx.sha512,
                               sha->alg == BL616CL_SHA384);
        break;
    }
}

/****************************************************************************
 * Name: bl616cl_sec_sha_load
 *
 * Description:
 *   Copy a session context into the engine context and point the
 *   descriptor address at the engine copy. Called with the engine locked.
 *
 * Input Parameters:
 *   sha - The context.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_sec_sha_load(FAR struct bl616cl_sha_s *sha)
{
  FAR union bl616cl_sha_ctx_u *ctx = &g_bl616cl_sha_ctx;

  *ctx = sha->ctx;
  switch (sha->alg)
    {
      case BL616CL_SHA1:
        ctx->sha1.link_addr = (uint32_t)(uintptr_t)&ctx->sha1.link_cfg;
        break;

      case BL616CL_SHA224:
      case BL616CL_SHA256:
        ctx->sha256.link_addr = (uint32_t)(uintptr_t)&ctx->sha256.link_cfg;
        break;

      default:
        ctx->sha512.link_addr = (uint32_t)(uintptr_t)&ctx->sha512.link_cfg;
        break;
    }
}

/****************************************************************************
 * Name: bl616cl_sec_sha_run
 *
 * Description:
 *   Hash one chunk. Called by bl616cl_sec_eng_transfer() with the SHA
 *   engine locked.
 *
 * Input Parameters:
 *   arg - The context.
 *   src - Input, in the bounce buffer or the caller's buffer.
 *   dst - Unused (NULL).
 *   len - Number of bytes.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_sec_sha_run(FAR void *arg, FAR const uint8_t *src,
                               FAR uint8_t *dst, size_t len)
{
  FAR struct bl616cl_sha_s *sha = arg;
  FAR union bl616cl_sha_ctx_u *ctx = &g_bl616cl_sha_ctx;

  switch (sha->alg)
    {
      case BL616CL_SHA1:
        return bflb_sha1_link_update(g_bl616cl_sha, &ctx->sha1, src, len);

      case BL616CL_SHA224:
      case BL616CL_SHA256:
        return bflb_sha256_link_update(g_bl616cl_sha, &ctx->sha256, src,
                                       len);

      default:
        return bflb_sha512_link_update(g_bl616cl_sha, &ctx->sha512, src,
                                       len);
    }
}

/****************************************************************************
 * Name: bl616cl_sec_sha_final
 *
 * Description:
 *   Pad the message in the engine context and write the digest. Called
 *   with the engine locked.
 *
 * Input Parameters:
 *   sha - The context.
 *   digest - Receives the digest; cleared on failure.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_sec_sha_final(FAR struct bl616cl_sha_s *sha,
                                 FAR uint8_t *digest)
{
  FAR union bl616cl_sha_ctx_u *ctx = &g_bl616cl_sha_ctx;

  switch (sha->alg)
    {
      case BL616CL_SHA1:
        return bflb_sha1_link_finish(g_bl616cl_sha, &ctx->sha1, digest);

      case BL616CL_SHA224:
      case BL616CL_SHA256:
        return bflb_sha256_link_finish(g_bl616cl_sha, &ctx->sha256,
                                       digest);

      default:
        return bflb_sha512_link_finish(g_bl616cl_sha, &ctx->sha512,
                                       digest);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_sec_sha_initialize
 *
 * Description:
 *   Obtain the SEC SHA device and enable the SEC clock.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Zero (OK) on success; -ENODEV if the SHA device is unavailable.
 *
 ****************************************************************************/

int bl616cl_sec_sha_initialize(void)
{
  bl616cl_sec_eng_initialize();

  g_bl616cl_sha = bflb_device_get_by_name(BFLB_NAME_SEC_SHA);
  if (g_bl616cl_sha == NULL)
    {
      syslog(LOG_ERR, "BL616CL SHA device is unavailable\n");
      return -ENODEV;
    }

  return OK;
}

/****************************************************************************
 * Name: bl616cl_sec_sha_new
 *
 * Description:
 *   Allocate a SHA context, ready for a new message.
 *
 * Input Parameters:
 *   sha - Receives the new context.
 *   alg - Hash algorithm.
 *
 * Returned Value:
 *   Zero (OK) on success; -ENOMEM if memory is exhausted.
 *
 ****************************************************************************/

int bl616cl_sec_sha_new(FAR struct bl616cl_sha_s **sha,
                        enum bl616cl_sha_alg_e alg)
{
  FAR struct bl616cl_sha_s *ctx;

  /* The LHAL context types are 32-byte aligned. */

  ctx = kmm_memalign(32, sizeof(*ctx));
  if (ctx == NULL)
    {
      return -ENOMEM;
    }

  memset(ctx, 0, sizeof(*ctx));
  ctx->alg = alg;
  bl616cl_sec_sha_start(ctx);

  *sha = ctx;
  return OK;
}

/****************************************************************************
 * Name: bl616cl_sec_sha_free
 *
 * Description:
 *   Wipe and free a SHA context.
 *
 * Input Parameters:
 *   sha - The context, or NULL.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_sec_sha_free(FAR struct bl616cl_sha_s *sha)
{
  if (sha != NULL)
    {
      explicit_bzero(sha, sizeof(*sha));
      kmm_free(sha);
    }
}

/****************************************************************************
 * Name: bl616cl_sec_sha_update
 *
 * Description:
 *   Add data to the message. After an engine failure the context is
 *   unusable and every later call returns the same error.
 *
 * Input Parameters:
 *   sha - The context.
 *   data - Message bytes.
 *   len - Number of bytes; may be zero.
 *
 * Returned Value:
 *   Zero (OK) on success; -ETIMEDOUT if the engine does not finish;
 *   another negated errno value if the engine lock fails.
 *
 ****************************************************************************/

int bl616cl_sec_sha_update(FAR struct bl616cl_sha_s *sha,
                           FAR const uint8_t *data, size_t len)
{
  int ret;

  if (sha->error < 0)
    {
      return sha->error;
    }

  if (len == 0)
    {
      return OK;
    }

  ret = bl616cl_sec_eng_lock(BL616CL_SEC_ENGINE_SHA);
  if (ret < 0)
    {
      return ret;
    }

  /* Set EN and LINK_MODE again on every use, as for AES. */

  bflb_sha_link_init(g_bl616cl_sha);
  bl616cl_sec_sha_load(sha);

  ret = bl616cl_sec_eng_transfer(BL616CL_SEC_ENGINE_SHA, data, NULL, len,
                                 true, bl616cl_sec_sha_run, sha);
  if (ret < 0)
    {
      bflb_sha_link_deinit(g_bl616cl_sha);
      sha->error = ret;
    }
  else
    {
      sha->ctx = g_bl616cl_sha_ctx;
    }

  explicit_bzero(&g_bl616cl_sha_ctx, sizeof(g_bl616cl_sha_ctx));
  bl616cl_sec_eng_unlock(BL616CL_SEC_ENGINE_SHA);
  return ret;
}

/****************************************************************************
 * Name: bl616cl_sec_sha_finish
 *
 * Description:
 *   Finish the message, write its digest and start a new message. If the
 *   engine fails in this call, the digest is cleared; after any engine
 *   failure the context is unusable, as for bl616cl_sec_sha_update().
 *
 * Input Parameters:
 *   sha - The context.
 *   digest - Receives the digest: 20, 28, 32, 48 or 64 bytes by
 *     algorithm.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure, as for
 *   bl616cl_sec_sha_update().
 *
 ****************************************************************************/

int bl616cl_sec_sha_finish(FAR struct bl616cl_sha_s *sha,
                           FAR uint8_t *digest)
{
  int ret;

  if (sha->error < 0)
    {
      return sha->error;
    }

  ret = bl616cl_sec_eng_lock(BL616CL_SEC_ENGINE_SHA);
  if (ret < 0)
    {
      return ret;
    }

  bflb_sha_link_init(g_bl616cl_sha);
  bl616cl_sec_sha_load(sha);

  ret = bl616cl_sec_sha_final(sha, digest);
  if (ret < 0)
    {
      bflb_sha_link_deinit(g_bl616cl_sha);
      sha->error = ret;
    }
  else
    {
      bl616cl_sec_sha_start(sha);
    }

  explicit_bzero(&g_bl616cl_sha_ctx, sizeof(g_bl616cl_sha_ctx));
  bl616cl_sec_eng_unlock(BL616CL_SEC_ENGINE_SHA);
  return ret;
}
