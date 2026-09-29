/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_wifi_adapter.h
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

#define BL616CL_WLAN_STA_DEVNO    0
#define BL616CL_WLAN_DEVS         1

#define MAC_LEN      (6)
#define SSID_MAX_LEN (32)
#define PWD_MAX_LEN  (64)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Wi-Fi event callback function */

typedef void (*wifi_evt_cb_t)(void *p);

/* Wi-Fi TX done callback function */

typedef void (*wifi_txdone_cb_t)(void *arg);

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_wifi_adapter_init
 *
 * Description:
 *   Initialize Wi-Fi adapter
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Zero (OK); initialization failures are not reported.
 *
 ****************************************************************************/

int bl616cl_wifi_adapter_init(void);

/****************************************************************************
 * Name: bl616cl_wifi_sta_start
 *
 * Description:
 *   Start Wi-Fi station.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_start(void);

/****************************************************************************
 * Name: bl616cl_wifi_sta_stop
 *
 * Description:
 *   Stop Wi-Fi station.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_stop(void);

/****************************************************************************
 * Name: bl616cl_wifi_sta_send_data
 *
 * Description:
 *   Send data to Wi-Fi station.
 *
 * Input Parameters:
 *   iob - The IOB containing the packet to be sent
 *   llhdrlen - The length of the link layer header
 *   offset - The offset of the data in the IOB
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_send_data(struct iob_s *iob,
                             uint16_t llhdrlen,
                             uint16_t offset);

/****************************************************************************
 * Name: bl616cl_wifi_sta_register_recv_cb
 *
 * Description:
 *   Register Wi-Fi station receive packet callback function
 *
 * Input Parameters:
 *   recv_cb - Receive callback function
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_register_recv_cb(int (*recv_cb)(void *net,
                                                   void *buffer,
                                                   uint16_t len,
                                                   void *eb));

/****************************************************************************
 * Name: bl616cl_wifi_sta_register_txdone_cb
 *
 * Description:
 *   Register the station TX done callback function.
 *
 * Input Parameters:
 *   cb - The callback function
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_wifi_sta_register_txdone_cb(wifi_txdone_cb_t cb);

/****************************************************************************
 * Name: bl616cl_wifi_sta_txdone
 *
 * Description:
 *   TX done notification passed to the macsw core by
 *   bl616cl_wifi_sta_register_txdone_cb().  Invokes the registered
 *   callback with a NULL argument, if any.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_wifi_sta_txdone(void);

/****************************************************************************
 * Name: bl616cl_wifi_sta_read_mac
 *
 * Description:
 *   Read the station MAC address through wifi_mgmr_sta_mac_get().
 *
 * Input Parameters:
 *   mac - Buffer that receives the 6-byte MAC address.
 *
 * Returned Value:
 *   0 on success; nonzero on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_read_mac(uint8_t *mac);

/****************************************************************************
 * Name: bl616cl_wifi_sta_password
 *
 * Description:
 *   Set/Get Wi-Fi station password.  Serialized with
 *   bl616cl_wifi_sta_connect() by the adapter lock.  Setting with
 *   IW_ENCODE_ALG_NONE clears the password.  Getting reports
 *   IW_ENCODE_ALG_CCMP, or NONE if no password is set.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *   -EINVAL if the key is longer than PWD_MAX_LEN; other errors come from
 *   the adapter lock.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_password(struct iwreq *iwr, bool set);

/****************************************************************************
 * Name: bl616cl_wifi_sta_essid
 *
 * Description:
 *   Set/Get Wi-Fi station ESSID
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_essid(struct iwreq *iwr, bool set);

/****************************************************************************
 * Name: bl616cl_wifi_sta_bssid
 *
 * Description:
 *   Set/Get Wi-Fi station BSSID
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_bssid(struct iwreq *iwr, bool set);

/****************************************************************************
 * Name: bl616cl_wifi_sta_connect
 *
 * Description:
 *   Trigger Wi-Fi station connection action
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_connect(void);

/****************************************************************************
 * Name: bl616cl_wifi_sta_disconnect
 *
 * Description:
 *   Trigger Wi-Fi station disconnection action
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_disconnect(void);

/****************************************************************************
 * Name: bl616cl_wifi_sta_scan
 *
 * Description:
 *   Scan APs.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_scan(struct iwreq *iwr);

/****************************************************************************
 * Name: bl616cl_wifi_sta_scan_result
 *
 * Description:
 *   Get scan result.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_scan_result(struct iwreq *iwr);

/****************************************************************************
 * Name: bl616cl_wifi_sta_mode
 *
 * Description:
 *   Set/Get Wi-Fi Station mode code.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_mode(struct iwreq *iwr, bool set);

/****************************************************************************
 * Name: bl616cl_wifi_sta_auth
 *
 * Description:
 *   Set/Get station authentication mode params.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_auth(struct iwreq *iwr, bool set);

/****************************************************************************
 * Name: bl616cl_wifi_sta_freq
 *
 * Description:
 *   Set/Get station frequency.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_freq(struct iwreq *iwr, bool set);

/****************************************************************************
 * Name: bl616cl_wifi_sta_bitrate
 *
 * Description:
 *   Get station default bit rate (Mbps).
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_bitrate(struct iwreq *iwr, bool set);

/****************************************************************************
 * Name: bl616cl_wifi_sta_txpower
 *
 * Description:
 *   Get or set station transmit power (dBm). Not supported: the BL616CL
 *   RF parameter flow exposes no transmit power control or readback.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   -ENOSYS always.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_txpower(struct iwreq *iwr, bool set);

/****************************************************************************
 * Name: bl616cl_wifi_sta_channel
 *
 * Description:
 *   Get station range of channel parameters.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_channel(struct iwreq *iwr, bool set);

/****************************************************************************
 * Name: bl616cl_wifi_sta_country
 *
 * Description:
 *   Configure country info.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_country(struct iwreq *iwr, bool set);

/****************************************************************************
 * Name: bl616cl_wifi_sta_rssi
 *
 * Description:
 *   Get Wi-Fi sensitivity (dBm).
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_rssi(struct iwreq *iwr, bool set);

/****************************************************************************
 * Name: bl616cl_wifi_sta_pta
 *
 * Description:
 *   Get station PTA priority parameters.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK on success (positive non-zero values are cmd-specific)
 *   Negated errno returned on failure.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_pta(struct iwreq *iwr, bool set);

/****************************************************************************
 * Name: bl616cl_wifi_sta_dtim
 *
 * Description:
 *   Set/Get DTIM interval.  The value is only stored in the adapter
 *   configuration.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK (zero) always.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_dtim(struct iwreq *iwr, bool set);

/****************************************************************************
 * Name: bl616cl_wifi_sta_powersave
 *
 * Description:
 *   Set/Get power save mode.  Setting enters or exits station power save
 *   and then saves the mode in the adapter configuration.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   OK (zero) always.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_powersave(struct iwreq *iwr, bool set);

/****************************************************************************
 * Name: bl616cl_wifi_sta_pmksa
 *
 * Description:
 *   Set/Get PMKSA cache.  Setting stores the given PMK.  Getting derives
 *   the PMK from the saved password and SSID with PBKDF2-SHA1 and returns
 *   it as a hex string; only WPA-PSK and WPA2-PSK connections support it.
 *
 * Input Parameters:
 *   iwr - The argument of the ioctl cmd
 *   set - true: set data; false: get data
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *   -EINVAL if the PMK length is not PWD_MAX_LEN; -EIO if no password is
 *   set or the key derivation fails; -ENOSYS if the security mode is not
 *   supported.
 *
 ****************************************************************************/

int bl616cl_wifi_sta_pmksa(struct iwreq *iwr, bool set);

#ifdef __cplusplus
}
#endif
#undef EXTERN

#endif /* __ASSEMBLY__ */
#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_WIFI_ADAPTER_H */
