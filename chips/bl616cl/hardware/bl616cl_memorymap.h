/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/hardware/bl616cl_memorymap.h
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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 ****************************************************************************/

#ifndef __VENDOR_BOUFFALOLAB_CHIP_BL616CL_HARDWARE_BL616CL_MEMORYMAP_H
#define __VENDOR_BOUFFALOLAB_CHIP_BL616CL_HARDWARE_BL616CL_MEMORYMAP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

/* Reuse the memory maps from drivers/soc/bl616cl/std/include/hardware and
 * drivers/lhal/config/bl616cl.
 * Peripheral register headers are included by their consumers separately.
 * This header is C-only because the upstream map also defines IRQn_Type.
 *
 * bl616cl.h gates IRQ_NUM_BASE and the SDK IRQ enum behind ARCH_RISCV,
 * the SDK context macro that NuttX never defines; provide it here so the
 * hardware layer compiles. SDK IRQ numbers are raw CLIC indices; convert
 * them with bl616cl_irq_raw_to_nuttx() before calling NuttX IRQ APIs.
 *
 * Only the register/memory map layer is included. The SDK driver API
 * headers such as bl616cl_glb.h pull BL_Err_Type::ERROR and register
 * access helpers that conflict with NuttX. Do not include them here.
 */

#ifndef ARCH_RISCV
#  define ARCH_RISCV 1
#endif

#include "bl616cl.h"
#include "../../../drivers/lhal/config/bl616cl/bl616cl_memorymap.h"

#endif /* __VENDOR_BOUFFALOLAB_CHIP_BL616CL_HARDWARE_BL616CL_MEMORYMAP_H */
