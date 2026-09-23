/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_wlan.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_WLAN_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_WLAN_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/wdog.h>
#include <nuttx/wqueue.h>

#include "bl616cl_wifi_adapter.h"

#ifndef __ASSEMBLY__

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

#ifdef CONFIG_BL_COMPONENT_WL80211

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616_wlan_sta_set_linkstatus
 *
 * Description:
 *   Set Wi-Fi station link status
 *
 * Parameters:
 *   linkstatus - true Notifies the networking layer about an available
 *                carrier, false Notifies the networking layer about an
 *                disappeared carrier.
 *
 * Returned Value:
 *   OK on success; Negated errno on failure.
 *
 ****************************************************************************/

int bl616_wlan_sta_set_linkstatus(bool linkstatus);

/****************************************************************************
 * Name: bl616_wlan_sta_initialize
 *
 * Description:
 *   Initialize the WLAN station netcard driver
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK on success; Negated errno on failure.
 *
 ****************************************************************************/

int bl616_wlan_sta_initialize(void);

/****************************************************************************
 * Name: bl616_wlan_sta_get_netdev
 *
 * Description:
 *   Get Wi-Fi station netcard driver
 *
 * Parameters:
 *   None
 *
 * Returned Value:
 *   Pointer to Wi-Fi station netcard driver
 *
 ****************************************************************************/

struct net_driver_s *bl616_wlan_sta_get_netdev(void);

#endif /* CONFIG_BL_COMPONENT_WL80211 */
#ifdef __cplusplus
}
#endif
#undef EXTERN

#endif /* __ASSEMBLY__ */
#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_WLAN_H */
