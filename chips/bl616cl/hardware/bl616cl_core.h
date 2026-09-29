/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/hardware/bl616cl_core.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_HARDWARE_BL616CL_CORE_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_HARDWARE_BL616CL_CORE_H

#include <stddef.h>

#include "core_rv32.h"

/* Reuse the T-Head register layout from drivers/lhal. This header is
 * private and C-only. offsetof follows the actual CORET_Type layout:
 * MTIME is at 0x7ff8, despite the upstream field comment saying 0x7ffc.
 */

#define BL616CL_CORET_MTIMECMP \
  (CORET_BASE + offsetof(CORET_Type, MTIMECMP))
#define BL616CL_CORET_MTIME \
  (CORET_BASE + offsetof(CORET_Type, MTIME))

/* CLICCFG is the first member, a bitfield that cannot use offsetof. */

#define BL616CL_CLICCFG_OFFSET           0
#define BL616CL_CLICINFO_OFFSET          offsetof(CLIC_Type, CLICINFO)
#define BL616CL_CLICINT_OFFSET           offsetof(CLIC_Type, CLICINT)
#define BL616CL_CLICINT_STRIDE           sizeof(CLIC_INT_Control)
#define BL616CL_CLICINT_IP_OFFSET        offsetof(CLIC_INT_Control, IP)
#define BL616CL_CLICINT_IE_OFFSET        offsetof(CLIC_INT_Control, IE)
#define BL616CL_CLICINT_ATTR_OFFSET      offsetof(CLIC_INT_Control, ATTR)
#define BL616CL_CLICINT_CTL_OFFSET       offsetof(CLIC_INT_Control, CTL)

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_HARDWARE_BL616CL_CORE_H */
