/****************************************************************************
 * vendor/bouffalolab/components/wireless/rfparam/include/sysinit.h
 *
 * SYSINIT shim for the rfparam adapter sources compiled into the
 * bl_rfparam library. NuttX has no SYSINIT table: the BL616CL Wi-Fi and
 * BLE drivers call _rfparam_init() when they start, so the registration
 * in rfparam_adapter.c expands to nothing.
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

#ifndef __BL_RFPARAM_SYSINIT_SHIM_H
#define __BL_RFPARAM_SYSINIT_SHIM_H

#define SYSINIT(name, subsystem, order, func, arg)

/* IS_ENABLED() normally comes from the Bouffalo SDK autoconf.h: 1 when the
 * option is defined as 1, otherwise 0.
 */

#ifndef IS_ENABLED
#  define _ZERO_WITH_COMMA_1                0,
#  define _second_arg(__ignored, val, ...)  val
#  define _is_enabled(x)                    __is_enabled(x)
#  define __is_enabled(val)                 ___is_enabled(_ZERO_WITH_COMMA_##val)
#  define ___is_enabled(junk_or_comma)      _second_arg(junk_or_comma 1, 0)
#  define IS_ENABLED(config)                _is_enabled(config)
#endif

#endif /* __BL_RFPARAM_SYSINIT_SHIM_H */
