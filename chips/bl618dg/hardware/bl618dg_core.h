/****************************************************************************
 * vendor/bouffalolab/chips/bl618dg/hardware/bl618dg_core.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL618DG_HARDWARE_BL618DG_CORE_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL618DG_HARDWARE_BL618DG_CORE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stddef.h>

#include "nmsis_core.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Reuse the Nuclei register layout from drivers/lhal. This header is
 * private and C-only.
 */

#define BL618DG_SYSTIMER_MTIMECMP \
  (SysTimer_BASE + offsetof(SysTimer_Type, MTIMERCMP))
#define BL618DG_SYSTIMER_MTIME \
  (SysTimer_BASE + offsetof(SysTimer_Type, MTIMER))

#define BL618DG_ECLIC_CFG_OFFSET         offsetof(CLIC_Type, CFG)
#define BL618DG_ECLIC_INFO_OFFSET        offsetof(CLIC_Type, INFO)
#define BL618DG_ECLIC_INT_OFFSET         offsetof(CLIC_Type, CTRL)
#define BL618DG_ECLIC_INT_STRIDE         sizeof(CLIC_CTRL_Type)
#define BL618DG_ECLIC_INT_IP_OFFSET      offsetof(CLIC_CTRL_Type, INTIP)
#define BL618DG_ECLIC_INT_IE_OFFSET      offsetof(CLIC_CTRL_Type, INTIE)
#define BL618DG_ECLIC_INT_ATTR_OFFSET    offsetof(CLIC_CTRL_Type, INTATTR)
#define BL618DG_ECLIC_INT_CTL_OFFSET     offsetof(CLIC_CTRL_Type, INTCTRL)

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL618DG_HARDWARE_BL618DG_CORE_H */
