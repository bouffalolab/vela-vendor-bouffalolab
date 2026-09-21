/****************************************************************************
 * components/wireless/wifi/wl80211/glue/include/bl616cl_soc_preinc.h
 *
 * Forced pre-include for TU units that must see the BL616CL SoC headers
 * (rfparam adapter) while the NuttX environment already defines an enum
 * member named ERROR in sys/types.h. Rename the BL_Err_Type ERROR
 * enumerator at include time exactly like chips/bl616cl/bl616cl_sdk.h
 * does, without changing BL_Err_Type or its ABI.
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
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

/* 1) Pull the lhal core first: it transitively includes the NuttX std
 *    headers and defines the enum member named ERROR in sys/types.h.
 *    (Same ordering as chips/bl616cl/bl616cl_sdk.h.)
 */
#include "bflb_core.h"

/* 2) Define BL_Err_Type while ERROR is renamed, so its enumerator does
 *    not collide with the one above.
 */
#pragma push_macro("ERROR")
#undef ERROR
#define ERROR BL616CL_GLUE_ERROR

#include "bl616cl_common.h"

#pragma pop_macro("ERROR")

/* 3) The remaining SoC headers reuse the guarded definitions above. */
#include "bl616cl_aon.h"
#include "bl616cl_hbn.h"
#include "bl616cl_mfg_media.h"
