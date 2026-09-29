/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_memory.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_MEMORY_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_MEMORY_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_memory_early_init
 *
 * Description:
 *   Early memory setup called at startup. If PSRAM has not been initialized
 *   yet, open the whole first 64 MiB of PSRAMB in TZC region 0 (not locked),
 *   then select the EM/WRAM split.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_memory_early_init(void);

/****************************************************************************
 * Name: start_load
 *
 * Description:
 *   Initialize RAM sections at startup: copy every entry of the
 *   __mem_copy_sections table from its load address to its run address and
 *   zero every entry of the __mem_setz_sections table. Both tables end with
 *   a sentinel and entries with NULL pointers are skipped.
 *
 *   Defined in drivers/soc/bl616cl/std/startup/start_load.c, which has no
 *   header of its own.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void start_load(void);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_MEMORY_H */
