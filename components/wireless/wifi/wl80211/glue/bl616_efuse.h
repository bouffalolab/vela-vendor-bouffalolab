/****************************************************************************
 * components/wireless/wifi/wl80211/glue/bl616_efuse.h
 *
 * Minimal efuse MAC-address interface for the BL616CL wireless port.
 * Adapted from the BL4 vendor tree (chip/bl616/bl616_efuse.h); only the
 * STA-facing MAC read is carried over, the NuttX efuse device framework
 * is not used here.
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

#ifndef __WL80211_GLUE_BL616_EFUSE_H
#define __WL80211_GLUE_BL616_EFUSE_H

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bl616_efuse_read_mac_address(uint8_t mac[6]);

#endif /* __WL80211_GLUE_BL616_EFUSE_H */
