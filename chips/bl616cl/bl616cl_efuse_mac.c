/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_efuse_mac.c
 *
 * BL616CL efuse STA MAC-address read for the wl80211 host port.
 * Logic ported from the BL4 vendor tree (chip/bl616/bl616_efuse.c,
 * bl616_efuse_read_mac_address): read ef_zone_01 words W2/W3 through the
 * lhal ef_ctrl direct interface, verify the stored zero-bit parity and
 * return the address in network order.
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
#include <errno.h>

#include <bflb_ef_ctrl.h>

#include "bl616cl_efuse_mac.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* ef_zone_01 word offsets (from the BL616 ef_data register map; the
 * BL616CL shares the same efuse zone layout). */

#define EF_DATA_EF_ZONE_01_W2_OFFSET 0x118
#define EF_DATA_EF_ZONE_01_W3_OFFSET 0x11c

/* chip-id / default WiFi MAC slot (zone 00, same words the soc ef_cfg
 * driver uses for bflb_efuse_get_chipid()). */

#define EF_DATA_EF_WIFI_MAC_LOW_OFFSET 0x14
#define EF_DATA_EF_WIFI_MAC_HIGH_OFFSET 0x18

/* Locally administered fallback prefix used when no provisioned MAC
 * matches its parity bits.  02 is the IEEE 802 locally-administered OUI
 * half; the rest is filled from efuse words (or a fixed pattern when the
 * efuse reads back empty). */

#define EF_MAC_FALLBACK_PREFIX "\\x02\\xE0\\x4C"

#define WRWD_TO_BYTEP(p, val)  \
  {                            \
    p[0] = val & 0xff;         \
    p[1] = (val >> 8) & 0xff;  \
    p[2] = (val >> 16) & 0xff; \
    p[3] = (val >> 24) & 0xff; \
  }

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uint32_t count_zero_bits_in_byte(uint8_t val)
{
  uint32_t cnt = 0;
  uint32_t i = 0;

  for (i = 0; i < 8; i++)
    {
      if ((val & (1 << i)) == 0)
        {
          cnt += 1;
        }
    }

  return cnt;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_efuse_read_slot
 *
 * Description:
 *   Read one 6-byte MAC slot (two consecutive efuse words) and verify
 *   its stored zero-bit parity.
 *
 * Returned Value:
 *   0 on success, -ENODATA when the parity does not match.
 *
 ****************************************************************************/

static int bl616cl_efuse_read_slot(uint32_t low_off, uint32_t high_off,
                                   uint8_t mac[6])
{
  uint32_t tmpval = 0;
  uint32_t i;
  uint32_t cnt = 0;

  bflb_ef_ctrl_read_direct(NULL, low_off, &tmpval, 1, 1);
  WRWD_TO_BYTEP(mac, tmpval);

  bflb_ef_ctrl_read_direct(NULL, high_off, &tmpval, 1, 1);
  mac[4] = tmpval & 0xff;
  mac[5] = (tmpval >> 8) & 0xff;

  for (i = 0; i < 6; i++)
    {
      cnt += count_zero_bits_in_byte(mac[i]);
    }

  if ((cnt & 0x3f) != ((tmpval >> 16) & 0x3f))
    {
      return -ENODATA;
    }

  /* Change to network order (first byte is the OUI MSB). */

  for (i = 0; i < 3; i++)
    {
      uint8_t t = mac[i];
      mac[i] = mac[5 - i];
      mac[5 - i] = t;
    }

  return 0;
}

/****************************************************************************
 * Name: bl616_efuse_read_mac_address
 *
 * Description:
 *   Resolve the STA MAC address for platform_get_mac(): provisioned
 *   ef_zone_01 first, then the
 *   chip-id WiFi-MAC slot, then a deterministic locally administered
 *   fallback (fixed prefix + efuse chip-id words) so the interface is
 *   always usable on boards without a provisioned address.
 *
 * Returned Value:
 *   0 on success (mac filled), negative errno otherwise.
 *
 ****************************************************************************/

int bl616_efuse_read_mac_address(uint8_t mac[6])
{
  uint32_t low = 0;
  uint32_t high = 0;

  if (bl616cl_efuse_read_slot(EF_DATA_EF_ZONE_01_W2_OFFSET,
                              EF_DATA_EF_ZONE_01_W3_OFFSET, mac) == 0)
    {
      return 0;
    }

  if (bl616cl_efuse_read_slot(EF_DATA_EF_WIFI_MAC_LOW_OFFSET,
                              EF_DATA_EF_WIFI_MAC_HIGH_OFFSET, mac) == 0)
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
