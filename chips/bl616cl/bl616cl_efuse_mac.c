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

#include <bflb_ef_ctrl.h>

#include "bl616cl_mfg_media.h"
#include "bl616cl_efuse_mac.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* chip-id / default WiFi MAC slot (zone 00, same words the soc ef_cfg
 * driver uses for bflb_efuse_get_chipid()). */

#define EF_DATA_EF_WIFI_MAC_LOW_OFFSET 0x14
#define EF_DATA_EF_WIFI_MAC_HIGH_OFFSET 0x18

/* Locally administered fallback prefix used when no factory MAC is
 * provisioned.  02 is the IEEE 802 locally-administered OUI
 * half; the rest is filled from efuse words (or a fixed pattern when the
 * efuse reads back empty). */

#define EF_MAC_FALLBACK_PREFIX "\x02\xE0\x4C"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616_efuse_read_mac_address
 *
 * Description:
 *   Resolve the STA MAC address for platform_get_mac(): the factory MAC
 *   from mfg media (RF-parameter flash area, then efuse MAC slots 2..0),
 *   then a deterministic locally administered fallback (fixed prefix +
 *   efuse words) so the interface is always usable on boards without a
 *   provisioned address.
 *
 * Returned Value:
 *   0 on success (mac filled), negative errno otherwise.
 *
 ****************************************************************************/

int bl616_efuse_read_mac_address(uint8_t mac[6])
{
  uint32_t low = 0;
  uint32_t high = 0;

  if (mfg_media_read_macaddr_with_lock(mac, 1) == 0)
    {
      return 0;
    }

  /* Fallback: locally administered address, bottom bytes mixed with the
   * chip-id words so multiple boards differ when efuse has content. */

  bflb_ef_ctrl_read_direct(NULL, EF_DATA_EF_WIFI_MAC_LOW_OFFSET,
                           &low, 1, 1);
  bflb_ef_ctrl_read_direct(NULL, EF_DATA_EF_WIFI_MAC_HIGH_OFFSET,
                           &high, 1, 1);

  mac[0] = (uint8_t)EF_MAC_FALLBACK_PREFIX[0];
  mac[1] = (uint8_t)EF_MAC_FALLBACK_PREFIX[1];
  mac[2] = (uint8_t)EF_MAC_FALLBACK_PREFIX[2];
  mac[3] = (uint8_t)(low >> 24);
  mac[4] = (uint8_t)(low >> 16);
  mac[5] = (uint8_t)((low >> 8) ^ high);

  if (mac[3] == 0 && mac[4] == 0 && mac[5] == 0)
    {
      mac[3] = 0x00;
      mac[4] = 0x01;
      mac[5] = 0x02;
    }

  return 0;
}
