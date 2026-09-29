/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_sdk.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SDK_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SDK_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bl616cl_lhal.h"

/* bflb_sf_ctrl.h has one legacy non-prototype declaration. Import it once
 * under a narrow diagnostic guard before std headers include it transitively.
 */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include "bflb_sf_ctrl.h"
#pragma GCC diagnostic pop

/* Import the upstream types once, before including any std API header.
 * Rename its ERROR enumerator without changing BL_Err_Type or its ABI,
 * after the LHAL compatibility header has imported bflb_core.h.
 */

#pragma push_macro("ERROR")
#undef ERROR
#define ERROR BL616CL_SDK_ERROR
#include "bl616cl_common.h"
#pragma pop_macro("ERROR")

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SDK_H */
