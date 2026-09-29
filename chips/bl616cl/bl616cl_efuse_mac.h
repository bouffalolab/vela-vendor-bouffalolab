/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_efuse_mac.h
 *
 * BL616CL efuse STA MAC-address resolution for the wl80211 host port.
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_EFUSE_MAC_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_EFUSE_MAC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616_efuse_read_mac_address
 *
 * Description:
 *   Resolve the STA MAC address for platform_get_mac(): the factory MAC
 *   from mfg media (RF-parameter flash area, then efuse MAC slots 2..0),
 *   then a fixed locally administered fallback so the interface is always
 *   usable on boards without a provisioned address.
 *
 *   The bl616_ name is the wl80211 host port contract (rtos_al_nuttx.c).
 *
 * Input Parameters:
 *   mac - Buffer that receives the 6-byte MAC address.
 *
 * Returned Value:
 *   Always 0; mac is filled with the factory or the fallback address.
 *
 ****************************************************************************/

int bl616_efuse_read_mac_address(uint8_t mac[6]);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_EFUSE_MAC_H */
