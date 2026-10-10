/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_sec_aes.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SEC_AES_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SEC_AES_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_AES_BLOCK_SIZE 16

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bl616cl_aes_mode_e
{
  BL616CL_AES_CBC, /* IV is the first chaining value */
  BL616CL_AES_CTR, /* IV is the 128-bit big-endian counter block */
  BL616CL_AES_XTS, /* IV is the tweak input, encrypted with key 2 */
};

/* An AES context: keys, mode and chaining state of one session. */

struct bl616cl_aes_s;

/****************************************************************************
 * Public Function Prototypes
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

int bl616cl_sec_aes_initialize(void);

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
                        FAR const uint8_t *key, size_t keylen);

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

void bl616cl_sec_aes_free(FAR struct bl616cl_aes_s *aes);

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
                           FAR const uint8_t *iv);

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
                          size_t len);

#endif
