/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_efuse_mac.c
 *
 * BL616CL STA MAC-address read for the wl80211 host port: the factory
 * MAC from the SoC mfg media layer (RF-parameter flash area, then efuse
 * MAC slots), as the Bouffalo SDK platform_get_mac() does.
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <string.h>

#include "bl616cl_sdk.h"
#include "bl616cl_mfg_media.h"
#include "bl616cl_efuse_mac.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Locally administered address used when no factory MAC is provisioned.
 * mfg media already tried every BL616CL efuse MAC slot, and the chip ID
 * (bflb_efuse_get_chipid()) is efuse MAC slot 0, so no other per-chip
 * value is left to make this address unique.
 */

static const uint8_t g_fallback_mac[6] =
{
  0x02, 0xe0, 0x4c, 0x00, 0x01, 0x02
};

/****************************************************************************
 * Public Functions
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
 * Input Parameters:
 *   mac - Buffer that receives the 6-byte MAC address.
 *
 * Returned Value:
 *   Always 0; mac is filled with the factory or the fallback address.
 *
 ****************************************************************************/

int bl616_efuse_read_mac_address(uint8_t mac[6])
{
  if (mfg_media_read_macaddr_with_lock(mac, 1) != 0)
    {
      memcpy(mac, g_fallback_mac, sizeof(g_fallback_mac));
    }

  return 0;
}
