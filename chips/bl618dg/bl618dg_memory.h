/****************************************************************************
 * vendor/bouffalolab/chips/bl618dg/bl618dg_memory.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL618DG_BL618DG_MEMORY_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL618DG_BL618DG_MEMORY_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl618dg_section_load
 *
 * Description:
 *   Initialize RAM sections at startup: zero every entry of the
 *   __mem_setz_sections table. The table ends with a sentinel and entries
 *   with NULL pointers are skipped.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl618dg_section_load(void);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL618DG_BL618DG_MEMORY_H */
