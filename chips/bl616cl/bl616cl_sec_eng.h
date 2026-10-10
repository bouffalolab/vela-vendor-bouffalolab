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
 * Public Types
 ****************************************************************************/

/* The SEC_ENG engines that the drivers serialize independently. */

enum bl616cl_sec_engine_e
{
  BL616CL_SEC_ENGINE_TRNG,
};

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
 *   Take exclusive use of one SEC_ENG engine.
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

#endif
