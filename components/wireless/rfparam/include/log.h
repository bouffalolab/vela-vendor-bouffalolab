/****************************************************************************
 * apps/vendor/bouffalolab/components/wireless/rfparam/include/log.h
 *
 * Minimal log shim for the rfparam adapter sources compiled into the
 * bl_rfparam library. The bouffalo SDK log framework is not part of this
 * SDK; the few LOG_I/LOG_W/LOG_E call sites of rfparam map to the NuttX
 * wireless log macros.
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

#ifndef __BL_RFPARAM_LOG_SHIM_H
#define __BL_RFPARAM_LOG_SHIM_H

#include <nuttx/wireless/wireless.h>
#include <debug.h>

#ifndef LOG_LEVEL
#define LOG_LEVEL 3
#endif

#define LOG_I(...) wlinfo(__VA_ARGS__)
#define LOG_W(...) wlwarn(__VA_ARGS__)
#define LOG_E(...) wlerr(__VA_ARGS__)

/* Tag bookkeeping kept as no-ops; rfparam only declares one tag. */

#define BFLB_LOG_DEFINE_TAG(...)
#define BFLB_LOG_GET_TAG(tag)   tag
#define BFLB_LOG_TAG            BFLB_LOG_GET_TAG(rfparam)
#define BFLB_LOG_TAG_TYPE       unsigned int

#endif /* __BL_RFPARAM_LOG_SHIM_H */
