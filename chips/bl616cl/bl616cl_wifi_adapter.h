/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_wifi_adapter.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_WIFI_ADAPTER_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_WIFI_ADAPTER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <nuttx/net/netdev.h>

#ifndef __ASSEMBLY__

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CONFIG_BL616CL_WIFI_STATION

#if defined(CONFIG_BL616CL_WIFI_STATION)
#  define BL616_WLAN_HAS_STA
#  define BL616_WLAN_STA_DEVNO    0
#  define BL616_WLAN_DEVS         1
#elif defined(CONFIG_BL616CL_WIFI_SOFTAP)
#  define BL616_WLAN_HAS_SOFTAP
#  define BL616_WLAN_SOFTAP_DEVNO 0
#  define BL616_WLAN_DEVS         1
#elif defined(CONFIG_BL616CL_WIFI_STATION_SOFTAP_COEXISTENCE)
#  define BL616_WLAN_HAS_STA
#  define BL616_WLAN_HAS_SOFTAP
#  define BL616_WLAN_STA_DEVNO    0
#  define BL616_WLAN_SOFTAP_DEVNO 1
#  define BL616_WLAN_DEVS         2
#endif

#define MAC_LEN      (6)
#define SSID_MAX_LEN (32)
#define PWD_MAX_LEN  (64)

/************************************************************************************
 * Public Types
 ************************************************************************************/

/* Wi-Fi event callback function */

typedef void (*wifi_evt_cb_t)(void *p);

/* Wi-Fi TX done callback function */

typedef void (*wifi_txdone_cb_t)(void *arg);

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bl616_wifi_adapter_init(void);

#ifdef BL616_WLAN_HAS_STA

int bl616_wifi_sta_start(void);

int bl616_wifi_sta_stop(void);

int bl616_wifi_sta_send_data(struct iob_s *iob,
                             uint16_t llhdrlen,
                             uint16_t offset);

int bl616_wifi_sta_register_recv_cb(int (*recv_cb)(void *net,
                                                   void *buffer,
                                                   uint16_t len,
                                                   void *eb));

void bl616_wifi_sta_register_txdone_cb(wifi_txdone_cb_t cb);

void bl616_wifi_sta_txdone(void);

int bl616_wifi_sta_read_mac(uint8_t *mac);

int bl616_wifi_sta_password(struct iwreq *iwr, bool set);

int bl616_wifi_sta_essid(struct iwreq *iwr, bool set);

int bl616_wifi_sta_bssid(struct iwreq *iwr, bool set);

int bl616_wifi_sta_connect(void);

int bl616_wifi_sta_disconnect(void);

int bl616_wifi_sta_scan(struct iwreq *iwr);

int bl616_wifi_sta_scan_result(struct iwreq *iwr);

int bl616_wifi_sta_mode(struct iwreq *iwr, bool set);

int bl616_wifi_sta_auth(struct iwreq *iwr, bool set);

int bl616_wifi_sta_freq(struct iwreq *iwr, bool set);

int bl616_wifi_sta_bitrate(struct iwreq *iwr, bool set);

int bl616_wifi_sta_txpower(struct iwreq *iwr, bool set);

int bl616_wifi_sta_channel(struct iwreq *iwr, bool set);

int bl616_wifi_sta_country(struct iwreq *iwr, bool set);

int bl616_wifi_sta_rssi(struct iwreq *iwr, bool set);

int bl616_wifi_sta_pta(struct iwreq *iwr, bool set);

int bl616_wifi_sta_dtim(struct iwreq *iwr, bool set);

int bl616_wifi_sta_powersave(struct iwreq *iwr, bool set);

int bl616_wifi_sta_pmksa(struct iwreq *iwr, bool set);
#endif

void bl616_wifi_stop_callback(void);


#ifdef __cplusplus
}
#endif
#undef EXTERN

#endif /* __ASSEMBLY__ */
#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_WIFI_ADAPTER_H */
