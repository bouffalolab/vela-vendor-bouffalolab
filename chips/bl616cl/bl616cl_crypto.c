/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_crypto.c
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
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <crypto/cryptodev.h>
#include <nuttx/kmalloc.h>

#include "bl616cl_sec_aes.h"
#include "bl616cl_sec_sha.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* A session holds one algorithm, a cipher or a hash. Its address is the
 * driver-local session id.
 */

struct bl616cl_crypto_session_s
{
  int alg;                       /* CRYPTO_* algorithm, or 0 */
  FAR struct bl616cl_aes_s *aes; /* Cipher context */
  FAR struct bl616cl_sha_s *sha; /* Hash context */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_crypto_session
 *
 * Description:
 *   Convert a session id from the crypto framework into the session.
 *
 * Input Parameters:
 *   sid - Session id; the driver-local id is the low 32 bits.
 *
 * Returned Value:
 *   The session.
 *
 ****************************************************************************/

static FAR struct bl616cl_crypto_session_s *
bl616cl_crypto_session(uint64_t sid)
{
  return (FAR struct bl616cl_crypto_session_s *)(uintptr_t)(uint32_t)sid;
}

/****************************************************************************
 * Name: bl616cl_crypto_release
 *
 * Description:
 *   Wipe and free a session and its contexts.
 *
 * Input Parameters:
 *   ses - The session.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void bl616cl_crypto_release(FAR struct bl616cl_crypto_session_s *ses)
{
#ifdef CONFIG_BL616CL_CRYPTO_AES
  bl616cl_sec_aes_free(ses->aes);
#endif
#ifdef CONFIG_BL616CL_CRYPTO_SHA
  bl616cl_sec_sha_free(ses->sha);
#endif
  kmm_free(ses);
}

#ifdef CONFIG_BL616CL_CRYPTO_AES
/****************************************************************************
 * Name: bl616cl_crypto_aes_mode
 *
 * Description:
 *   Map a CRYPTO_AES_* algorithm to the AES mode.
 *
 * Input Parameters:
 *   alg - A supported AES algorithm.
 *
 * Returned Value:
 *   The AES mode.
 *
 ****************************************************************************/

static enum bl616cl_aes_mode_e bl616cl_crypto_aes_mode(int alg)
{
  switch (alg)
    {
      case CRYPTO_AES_CTR:
        return BL616CL_AES_CTR;

      case CRYPTO_AES_XTS:
        return BL616CL_AES_XTS;

      default:
        return BL616CL_AES_CBC;
    }
}

/****************************************************************************
 * Name: bl616cl_crypto_cipher
 *
 * Description:
 *   Run one cipher descriptor. Without CRD_F_UPDATE the operation starts
 *   from the IV in crp_iv; with it, it continues from where the previous
 *   operation on the session stopped. crp_iv is not written back, as in
 *   the upstream software driver.
 *
 * Input Parameters:
 *   ses - The session.
 *   crp - The request.
 *   crd - The cipher descriptor.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_crypto_cipher(FAR struct bl616cl_crypto_session_s *ses,
                                 FAR struct cryptop *crp,
                                 FAR struct cryptodesc *crd)
{
  if (crp->crp_dst == NULL || crd->crd_len < 0)
    {
      return -EINVAL;
    }

  if ((crd->crd_flags & CRD_F_UPDATE) == 0)
    {
      if (crp->crp_iv == NULL)
        {
          return -EINVAL;
        }

      bl616cl_sec_aes_setiv(ses->aes, (FAR const uint8_t *)crp->crp_iv);
    }

  return bl616cl_sec_aes_crypt(ses->aes,
                               (crd->crd_flags & CRD_F_ENCRYPT) != 0,
                               (FAR const uint8_t *)crp->crp_buf +
                               crd->crd_skip,
                               (FAR uint8_t *)crp->crp_dst, crd->crd_len);
}
#endif

#ifdef CONFIG_BL616CL_CRYPTO_SHA
/****************************************************************************
 * Name: bl616cl_crypto_sha_alg
 *
 * Description:
 *   Map a CRYPTO_SHA* algorithm to the SHA algorithm.
 *
 * Input Parameters:
 *   alg - A supported hash algorithm.
 *
 * Returned Value:
 *   The SHA algorithm.
 *
 ****************************************************************************/

static enum bl616cl_sha_alg_e bl616cl_crypto_sha_alg(int alg)
{
  switch (alg)
    {
      case CRYPTO_SHA1:
        return BL616CL_SHA1;

      case CRYPTO_SHA2_224:
        return BL616CL_SHA224;

      case CRYPTO_SHA2_256:
        return BL616CL_SHA256;

      case CRYPTO_SHA2_384:
        return BL616CL_SHA384;

      default:
        return BL616CL_SHA512;
    }
}

/****************************************************************************
 * Name: bl616cl_crypto_hash
 *
 * Description:
 *   Run one hash descriptor, as the upstream software driver does: with
 *   CRD_F_UPDATE the data is added and crp_mac is not written; without it
 *   the message is finished and the whole digest of the algorithm is
 *   written to crp_mac, whatever crp_olen says. The data of a finishing
 *   request is not hashed.
 *
 * Input Parameters:
 *   ses - The session.
 *   crp - The request.
 *   crd - The hash descriptor.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_crypto_hash(FAR struct bl616cl_crypto_session_s *ses,
                               FAR struct cryptop *crp,
                               FAR struct cryptodesc *crd)
{
  if ((crd->crd_flags & CRD_F_UPDATE) != 0)
    {
      if (crd->crd_len < 0 || (crp->crp_buf == NULL && crd->crd_len > 0))
        {
          return -EINVAL;
        }

      return bl616cl_sec_sha_update(ses->sha,
                                    (FAR const uint8_t *)crp->crp_buf +
                                    crd->crd_skip, crd->crd_len);
    }

  if (crp->crp_mac == NULL)
    {
      return -EINVAL;
    }

  return bl616cl_sec_sha_finish(ses->sha, (FAR uint8_t *)crp->crp_mac);
}
#endif

/****************************************************************************
 * Name: bl616cl_crypto_freesession
 *
 * Description:
 *   Free a session created by bl616cl_crypto_newsession().
 *
 * Input Parameters:
 *   sid - Session id.
 *
 * Returned Value:
 *   Zero (OK) on success; -EINVAL for a null session id.
 *
 ****************************************************************************/

static int bl616cl_crypto_freesession(uint64_t sid)
{
  FAR struct bl616cl_crypto_session_s *ses = bl616cl_crypto_session(sid);

  if (ses == NULL)
    {
      return -EINVAL;
    }

  bl616cl_crypto_release(ses);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_crypto_newsession
 *
 * Description:
 *   Create a session for the algorithms in the cryptoini chain. Key state
 *   is derived from cri_key here; cri_key is not kept or modified.
 *
 * Input Parameters:
 *   sid - Receives the driver-local session id.
 *   cri - Algorithms and keys.
 *
 * Returned Value:
 *   Zero (OK) on success; -EINVAL for an unsupported algorithm, key or
 *   combination; -ENOTSUP for key ids; -ENOMEM if memory is exhausted.
 *
 ****************************************************************************/

static int bl616cl_crypto_newsession(FAR uint32_t *sid,
                                     FAR struct cryptoini *cri)
{
  FAR struct bl616cl_crypto_session_s *ses;
  int ret = OK;

  if (sid == NULL || cri == NULL)
    {
      return -EINVAL;
    }

  ses = kmm_zalloc(sizeof(*ses));
  if (ses == NULL)
    {
      return -ENOMEM;
    }

  for (; cri != NULL && ret >= 0; cri = cri->cri_next)
    {
      if ((cri->cri_flags & CRD_F_KEYID) != 0)
        {
          ret = -ENOTSUP;
          break;
        }

      switch (cri->cri_alg)
        {
#ifdef CONFIG_BL616CL_CRYPTO_AES
          case CRYPTO_AES_CBC:
          case CRYPTO_AES_192_CBC:
          case CRYPTO_AES_256_CBC:
          case CRYPTO_AES_CTR:
          case CRYPTO_AES_XTS:
            if (ses->alg != 0 || cri->cri_klen % 8 != 0)
              {
                ret = -EINVAL;
                break;
              }

            ret = bl616cl_sec_aes_new(&ses->aes,
                                      bl616cl_crypto_aes_mode(cri->cri_alg),
                                      (FAR const uint8_t *)cri->cri_key,
                                      cri->cri_klen / 8);
            ses->alg = cri->cri_alg;
            break;
#endif

#ifdef CONFIG_BL616CL_CRYPTO_SHA
          case CRYPTO_SHA1:
          case CRYPTO_SHA2_224:
          case CRYPTO_SHA2_256:
          case CRYPTO_SHA2_384:
          case CRYPTO_SHA2_512:
            if (ses->alg != 0)
              {
                ret = -EINVAL;
                break;
              }

            ret = bl616cl_sec_sha_new(&ses->sha,
                                      bl616cl_crypto_sha_alg(cri->cri_alg));
            ses->alg = cri->cri_alg;
            break;
#endif

          default:
            ret = -EINVAL;
            break;
        }
    }

  if (ret < 0)
    {
      bl616cl_crypto_release(ses);
      return ret;
    }

  *sid = (uint32_t)(uintptr_t)ses;
  return OK;
}

/****************************************************************************
 * Name: bl616cl_crypto_process
 *
 * Description:
 *   Run the descriptors of a request in order. Errors are returned
 *   directly, not through crp_etype.
 *
 * Input Parameters:
 *   crp - The request.
 *
 * Returned Value:
 *   Zero (OK) on success; -ENOTSUP for asynchronous requests; another
 *   negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_crypto_process(FAR struct cryptop *crp)
{
  FAR struct bl616cl_crypto_session_s *ses;
  FAR struct cryptodesc *crd;
  int ret = OK;

  if (crp == NULL || crp->crp_desc == NULL)
    {
      return -EINVAL;
    }

  /* This driver completes requests synchronously. */

  if ((crp->crp_flags & CRYPTO_F_CBIMM) != 0)
    {
      return -ENOTSUP;
    }

  ses = bl616cl_crypto_session(crp->crp_sid);
  if (ses == NULL)
    {
      return -EINVAL;
    }

  for (crd = crp->crp_desc; crd != NULL && ret >= 0; crd = crd->crd_next)
    {
      if (ses->alg == 0 || crd->crd_alg != ses->alg)
        {
          ret = -EINVAL;
        }
#ifdef CONFIG_BL616CL_CRYPTO_AES
      else if (ses->aes != NULL)
        {
          ret = bl616cl_crypto_cipher(ses, crp, crd);
        }
#endif
#ifdef CONFIG_BL616CL_CRYPTO_SHA
      else if (ses->sha != NULL)
        {
          ret = bl616cl_crypto_hash(ses, crp, crd);
        }
#endif
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: hwcr_init
 *
 * Description:
 *   Register the SEC_ENG engines as the hardware crypto driver. Called by
 *   the crypto framework when /dev/crypto is registered.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void hwcr_init(void)
{
  int algs[CRYPTO_ALGORITHM_MAX + 1];
  int hwcr_id;

  memset(algs, 0, sizeof(algs));

#ifdef CONFIG_BL616CL_CRYPTO_AES
  if (bl616cl_sec_aes_initialize() >= 0)
    {
      algs[CRYPTO_AES_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;
      algs[CRYPTO_AES_192_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;
      algs[CRYPTO_AES_256_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;
      algs[CRYPTO_AES_CTR] = CRYPTO_ALG_FLAG_SUPPORTED;
      algs[CRYPTO_AES_XTS] = CRYPTO_ALG_FLAG_SUPPORTED;
    }
#endif

#ifdef CONFIG_BL616CL_CRYPTO_SHA
  if (bl616cl_sec_sha_initialize() >= 0)
    {
      algs[CRYPTO_SHA1] = CRYPTO_ALG_FLAG_SUPPORTED;
      algs[CRYPTO_SHA2_224] = CRYPTO_ALG_FLAG_SUPPORTED;
      algs[CRYPTO_SHA2_256] = CRYPTO_ALG_FLAG_SUPPORTED;
      algs[CRYPTO_SHA2_384] = CRYPTO_ALG_FLAG_SUPPORTED;
      algs[CRYPTO_SHA2_512] = CRYPTO_ALG_FLAG_SUPPORTED;
    }
#endif

  hwcr_id = crypto_get_driverid(0);
  if (hwcr_id < 0)
    {
      syslog(LOG_ERR, "BL616CL crypto: no driver id: %d\n", hwcr_id);
      return;
    }

  crypto_register(hwcr_id, algs, bl616cl_crypto_newsession,
                  bl616cl_crypto_freesession, bl616cl_crypto_process);
}
