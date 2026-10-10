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
#include <nuttx/nuttx.h>

#include "bl616cl_lhal.h"
#include "bflb_core.h"
#include "bflb_sec_aes.h"
#include "bl616cl_sec_eng.h"
#include "bl616cl_sec_aes.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* XTS multiplies the tweak by alpha in GF(2^128) with this polynomial. */

#define BL616CL_AES_XTS_ALPHA 0x87

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl616cl_aes_s
{
  struct bflb_aes_link_s desc;  /* Key (key 1 for XTS), engine mode and
                                 * chaining value; the engine fields are
                                 * set for each operation */
  enum bl616cl_aes_mode_e mode; /* Block cipher mode */
  int error;                    /* Sticky error after an engine failure */

  /* XTS only */

  uint32_t key2[8];             /* Key 2, encrypts the IV into the tweak */
  bool restart;                 /* Compute the tweak from the IV first */

  /* Tweak of the next block */

  uint8_t tweak[BL616CL_AES_BLOCK_SIZE];
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

/* One block for computing the XTS tweak. */

static uint8_t g_bl616cl_aes_block[BL616CL_AES_BLOCK_SIZE]
  BL616CL_SEC_ENG_NOCACHE;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_sec_aes_update
 *
 * Description:
 *   Run the working descriptor once.
 *
 * Input Parameters:
 *   src - Input in engine-visible memory.
 *   dst - Output in engine-visible memory.
 *   len - Number of bytes, a multiple of the block size.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value from
 *   bflb_aes_link_update().
 *
 ****************************************************************************/

static int bl616cl_sec_aes_update(FAR const uint8_t *src, FAR uint8_t *dst,
                                  size_t len)
{
  return bflb_aes_link_update(g_bl616cl_aes,
                              (uint32_t)(uintptr_t)&g_bl616cl_aes_desc,
                              src, dst, len);
}

/****************************************************************************
 * Name: bl616cl_sec_aes_ctr_carry
 *
 * Description:
 *   Carry into the upper 96 bits of the working descriptor's counter
 *   block. The engine increments only the low 32 bits and wraps them
 *   without a carry.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_sec_aes_ctr_carry(void)
{
  uint32_t words[3];
  uint8_t *ctr = (uint8_t *)words;
  int i;

  words[0] = g_bl616cl_aes_desc.aes_iv0;
  words[1] = g_bl616cl_aes_desc.aes_iv1;
  words[2] = g_bl616cl_aes_desc.aes_iv2;

  for (i = sizeof(words) - 1; i >= 0; i--)
    {
      if (++ctr[i] != 0)
        {
          break;
        }
    }

  g_bl616cl_aes_desc.aes_iv0 = words[0];
  g_bl616cl_aes_desc.aes_iv1 = words[1];
  g_bl616cl_aes_desc.aes_iv2 = words[2];
}

/****************************************************************************
 * Name: bl616cl_sec_aes_ctr
 *
 * Description:
 *   Run CTR blocks, split where the low 32 bits of the counter wrap.
 *
 * Input Parameters:
 *   src - Input in engine-visible memory.
 *   dst - Output in engine-visible memory.
 *   len - Number of bytes, a multiple of the block size.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_sec_aes_ctr(FAR const uint8_t *src, FAR uint8_t *dst,
                               size_t len)
{
  uint64_t room;
  size_t seg;
  int ret;

  while (len > 0)
    {
      /* The low 32 bits are bytes 12..15 of the counter, big-endian. */

      room = (UINT64_C(1) << 32) -
             __builtin_bswap32(g_bl616cl_aes_desc.aes_iv3);
      seg = len / BL616CL_AES_BLOCK_SIZE < room ?
            len : (size_t)room * BL616CL_AES_BLOCK_SIZE;

      ret = bl616cl_sec_aes_update(src, dst, seg);
      if (ret < 0)
        {
          return ret;
        }

      if (seg / BL616CL_AES_BLOCK_SIZE == room)
        {
          bl616cl_sec_aes_ctr_carry();
        }

      src += seg;
      dst += seg;
      len -= seg;
    }

  return OK;
}

/****************************************************************************
 * Name: bl616cl_sec_aes_xts_mask
 *
 * Description:
 *   XOR each block with its XTS tweak, advancing the tweak by one block
 *   each time (multiplication by alpha, as in the crypto framework).
 *
 * Input Parameters:
 *   buf - Blocks to mask in place.
 *   len - Number of bytes, a multiple of the block size.
 *   tweak - Tweak of the first block; advanced past the last one.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_sec_aes_xts_mask(FAR uint8_t *buf, size_t len,
                                     FAR uint8_t *tweak)
{
  unsigned int carry;
  unsigned int next;
  size_t off;
  int i;

  for (off = 0; off < len; off += BL616CL_AES_BLOCK_SIZE)
    {
      carry = 0;
      for (i = 0; i < BL616CL_AES_BLOCK_SIZE; i++)
        {
          buf[off + i] ^= tweak[i];
          next = tweak[i] >> 7;
          tweak[i] = (uint8_t)((tweak[i] << 1) | carry);
          carry = next;
        }

      tweak[0] ^= BL616CL_AES_XTS_ALPHA & -carry;
    }
}

/****************************************************************************
 * Name: bl616cl_sec_aes_xts
 *
 * Description:
 *   Run XTS blocks: mask with the tweak, run AES-ECB with key 1, mask
 *   again.
 *
 * Input Parameters:
 *   aes - The context; its tweak is advanced.
 *   buf - Blocks in engine-visible memory, processed in place.
 *   len - Number of bytes, a multiple of the block size.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_sec_aes_xts(FAR struct bl616cl_aes_s *aes,
                               FAR uint8_t *buf, size_t len)
{
  uint8_t tweak[BL616CL_AES_BLOCK_SIZE];
  int ret;

  memcpy(tweak, aes->tweak, sizeof(tweak));
  bl616cl_sec_aes_xts_mask(buf, len, tweak);
  explicit_bzero(tweak, sizeof(tweak));

  ret = bl616cl_sec_aes_update(buf, buf, len);
  if (ret < 0)
    {
      return ret;
    }

  bl616cl_sec_aes_xts_mask(buf, len, aes->tweak);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_sec_aes_xts_start
 *
 * Description:
 *   Compute the first tweak by encrypting the IV with key 2.
 *
 * Input Parameters:
 *   aes - The context; the IV is in its descriptor.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_sec_aes_xts_start(FAR struct bl616cl_aes_s *aes)
{
  uint32_t iv[4];
  int ret;

  g_bl616cl_aes_desc = aes->desc;
  g_bl616cl_aes_desc.aes_dec_en = 0;
  g_bl616cl_aes_desc.aes_key0 = aes->key2[0];
  g_bl616cl_aes_desc.aes_key1 = aes->key2[1];
  g_bl616cl_aes_desc.aes_key2 = aes->key2[2];
  g_bl616cl_aes_desc.aes_key3 = aes->key2[3];
  g_bl616cl_aes_desc.aes_key4 = aes->key2[4];
  g_bl616cl_aes_desc.aes_key5 = aes->key2[5];
  g_bl616cl_aes_desc.aes_key6 = aes->key2[6];
  g_bl616cl_aes_desc.aes_key7 = aes->key2[7];

  iv[0] = aes->desc.aes_iv0;
  iv[1] = aes->desc.aes_iv1;
  iv[2] = aes->desc.aes_iv2;
  iv[3] = aes->desc.aes_iv3;
  memcpy(g_bl616cl_aes_block, iv, sizeof(iv));

  ret = bl616cl_sec_aes_update(g_bl616cl_aes_block, g_bl616cl_aes_block,
                               BL616CL_AES_BLOCK_SIZE);
  if (ret >= 0)
    {
      memcpy(aes->tweak, g_bl616cl_aes_block, sizeof(aes->tweak));
      aes->restart = false;
    }

  explicit_bzero(g_bl616cl_aes_block, sizeof(g_bl616cl_aes_block));
  return ret;
}

/****************************************************************************
 * Name: bl616cl_sec_aes_run
 *
 * Description:
 *   Process one chunk. Called by bl616cl_sec_eng_transfer() with the AES
 *   engine locked. A partial last CTR block is run as a whole block in the
 *   bounce buffer; only len bytes are copied out. XTS chunks are always in
 *   the bounce buffer, in place.
 *
 * Input Parameters:
 *   arg - The context.
 *   src - Input, in the bounce buffer or the caller's buffer.
 *   dst - Output, in the bounce buffer or the caller's buffer.
 *   len - Number of bytes.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_sec_aes_run(FAR void *arg, FAR const uint8_t *src,
                               FAR uint8_t *dst, size_t len)
{
  FAR struct bl616cl_aes_s *aes = arg;

  len = ALIGN_UP(len, BL616CL_AES_BLOCK_SIZE);

  switch (aes->mode)
    {
      case BL616CL_AES_CTR:
        return bl616cl_sec_aes_ctr(src, dst, len);

      case BL616CL_AES_XTS:
        return bl616cl_sec_aes_xts(aes, dst, len);

      default:
        return bl616cl_sec_aes_update(src, dst, len);
    }
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
 *   Allocate an AES context. The chaining state starts at zero.
 *
 * Input Parameters:
 *   aes - Receives the new context.
 *   mode - Block cipher mode.
 *   key - Key bytes; only read during this call. For XTS, key 1 followed
 *     by key 2.
 *   keylen - Key length in bytes: 16, 24 or 32; for XTS 32 or 64.
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

  if (mode == BL616CL_AES_XTS)
    {
      if (keylen != 32 && keylen != 64)
        {
          return -EINVAL;
        }

      keylen /= 2;
    }

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

  ctx = kmm_zalloc(sizeof(*ctx));
  if (ctx == NULL)
    {
      return -ENOMEM;
    }

  ctx->mode = mode;
  ctx->desc.aes_key = keysize;
  switch (mode)
    {
      case BL616CL_AES_CBC:
        ctx->desc.aes_mode = AES_MODE_CBC;
        break;

      case BL616CL_AES_CTR:
        ctx->desc.aes_mode = AES_MODE_CTR;
        break;

      case BL616CL_AES_XTS:
        ctx->desc.aes_mode = AES_MODE_ECB;
        ctx->restart = true;
        memcpy(ctx->key2, key + keylen, keylen);
        break;
    }

  /* The descriptor holds the key bytes in memory order. */

  memset(words, 0, sizeof(words));
  memcpy(words, key, keylen);
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
 *   Restart the chaining state from an IV at the next operation.
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
  aes->restart = aes->mode == BL616CL_AES_XTS;
}

/****************************************************************************
 * Name: bl616cl_sec_aes_crypt
 *
 * Description:
 *   Encrypt or decrypt with the SEC AES engine in link mode. The operation
 *   continues from the chaining state left by the previous one, so a
 *   message can be processed in several calls. The CTR counter carries
 *   over all 128 bits. XTS runs the engine in ECB mode and applies the
 *   tweak on the CPU, because the engine cannot resume a data unit. After
 *   an engine failure the context is unusable and every later call returns
 *   the same error.
 *
 * Input Parameters:
 *   aes - The context.
 *   encrypt - True to encrypt, false to decrypt.
 *   src - Input data.
 *   dst - Output buffer of len bytes; may equal src.
 *   len - Number of bytes, a multiple of BL616CL_AES_BLOCK_SIZE except
 *     for CTR, where a partial last block still uses a whole counter.
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

  if (aes->mode != BL616CL_AES_CTR && len % BL616CL_AES_BLOCK_SIZE != 0)
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

  /* Set EN and LINK_MODE again on every use: another user may have left
   * the engine in non-link mode, and a link operation would then write to
   * a stale buffer.
   */

  bflb_aes_link_init(g_bl616cl_aes);

  if (aes->restart)
    {
      ret = bl616cl_sec_aes_xts_start(aes);
    }

  if (ret >= 0)
    {
      /* CTR decryption is the same keystream as encryption. */

      aes->desc.aes_dec_en = !encrypt && aes->mode != BL616CL_AES_CTR;
      g_bl616cl_aes_desc = aes->desc;

      /* XTS masks the data on the CPU, so it always uses the bounce
       * buffer.
       */

      ret = bl616cl_sec_eng_transfer(BL616CL_SEC_ENGINE_AES, src, dst, len,
                                     aes->mode != BL616CL_AES_XTS,
                                     bl616cl_sec_aes_run, aes);
    }

  if (ret < 0)
    {
      /* Stop the engine. The chaining state is unknown now, so the
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
