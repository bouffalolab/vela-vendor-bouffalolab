/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_sec_sha.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SEC_SHA_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SEC_SHA_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bl616cl_sha_alg_e
{
  BL616CL_SHA1,
  BL616CL_SHA224,
  BL616CL_SHA256,
  BL616CL_SHA384,
  BL616CL_SHA512,
};

/* A SHA context: algorithm and running hash state of one session. */

struct bl616cl_sha_s;

/****************************************************************************
 * Public Function Prototypes
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

int bl616cl_sec_sha_initialize(void);

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
                        enum bl616cl_sha_alg_e alg);

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

void bl616cl_sec_sha_free(FAR struct bl616cl_sha_s *sha);

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
                           FAR const uint8_t *data, size_t len);

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
                           FAR uint8_t *digest);

#endif
