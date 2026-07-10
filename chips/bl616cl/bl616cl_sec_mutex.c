/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_sec_mutex.c
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

#include <nuttx/mutex.h>

#include "bl616cl_sec_mutex.h"

#include "bflb_sec_mutex.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static mutex_t g_aes_mutex = NXMUTEX_INITIALIZER;
static mutex_t g_sha_mutex = NXMUTEX_INITIALIZER;
static mutex_t g_pka_mutex = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bflb_sec_mutex_init
 ****************************************************************************/

void bflb_sec_mutex_init(void)
{
  /* The mutexes are ready through static initialization. */
}

/****************************************************************************
 * Name: bflb_sec_aes_mutex_take
 ****************************************************************************/

int bflb_sec_aes_mutex_take(void)
{
  return nxmutex_lock(&g_aes_mutex);
}

/****************************************************************************
 * Name: bflb_sec_aes_mutex_give
 ****************************************************************************/

int bflb_sec_aes_mutex_give(void)
{
  return nxmutex_unlock(&g_aes_mutex);
}

/****************************************************************************
 * Name: bflb_sec_sha_mutex_take
 ****************************************************************************/

int bflb_sec_sha_mutex_take(void)
{
  return nxmutex_lock(&g_sha_mutex);
}

/****************************************************************************
 * Name: bflb_sec_sha_mutex_give
 ****************************************************************************/

int bflb_sec_sha_mutex_give(void)
{
  return nxmutex_unlock(&g_sha_mutex);
}

/****************************************************************************
 * Name: bflb_sec_pka_mutex_take
 ****************************************************************************/

int bflb_sec_pka_mutex_take(void)
{
  return nxmutex_lock(&g_pka_mutex);
}

/****************************************************************************
 * Name: bflb_sec_pka_mutex_give
 ****************************************************************************/

int bflb_sec_pka_mutex_give(void)
{
  return nxmutex_unlock(&g_pka_mutex);
}

/****************************************************************************
 * Name: bl616cl_sec_mutex_init
 *
 * Description:
 *   Initialize the BouffaloLab security-engine mutex adapter.
 *
 ****************************************************************************/

void bl616cl_sec_mutex_init(void)
{
  bflb_sec_mutex_init();
}
