/****************************************************************************
 * components/wireless/wifi/wl80211/glue/bl616cl_wifi_glb.h
 *
 * Minimal BL616CL GLB (global register) API declarations used by the
 * wl80211 platform glue. The GLB_* implementations are provided by the
 * chip support library (libbl_std.a); the declarations are duplicated
 * here instead of including the full bl616cl_glb.h register header to
 * avoid its BL_Err_Type enumerator colliding with the enum in
 * sys/types.h pulled in by the NuttX environment.
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

#ifndef __WL80211_GLUE_BL616CL_WIFI_GLB_H
#define __WL80211_GLUE_BL616CL_WIFI_GLB_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* AHB clock reset types (bl616cl_glb.h) */

#define GLB_AHB_MCU_SW_WIFI            (4)

/* Peripheral clock IP indices (bl616cl_glb.h) */

#define GLB_AHB_CLOCK_IP_WIFI_PHY      (40)
#define GLB_AHB_CLOCK_IP_WIFI_MAC_PHY  (41)
#define GLB_AHB_CLOCK_IP_WIFI_PLATFORM (42)

/* Peripheral clock IP bit masks, the argument type of
 * GLB_PER_Clock_UnGate() */

#define GLB_AHB_CLOCK_WIFI_PHY         (1ULL << GLB_AHB_CLOCK_IP_WIFI_PHY)
#define GLB_AHB_CLOCK_WIFI_MAC_PHY     (1ULL << GLB_AHB_CLOCK_IP_WIFI_MAC_PHY)
#define GLB_AHB_CLOCK_WIFI_PLATFORM    (1ULL << GLB_AHB_CLOCK_IP_WIFI_PLATFORM)

/* Function return status (subset of BL_Err_Type) */

#define BL_SUCCESS 0

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void GLB_AHB_MCU_Software_Reset(uint8_t swrst);
int GLB_PER_Clock_UnGate(uint64_t ips);

#endif /* __WL80211_GLUE_BL616CL_WIFI_GLB_H */
