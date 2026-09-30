/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_wl80211_port.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_WL80211_PORT_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_WL80211_PORT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>

/* The wl80211 NuttX host port (components/wireless/wifi/wl80211/wl80211/
 * nuttx.c) defines the STA TX interface below without declaring it in any
 * wl80211 header, and no declared wl80211 API is equivalent: they are the
 * zero-copy IOB output, its in-flight limit and the TX done notification.
 * Keep these prototypes identical to the definitions in nuttx.c; the
 * compiler cannot check them across the two repositories.
 */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

struct iob_s;

/****************************************************************************
 * Name: internal_register_txdone_cb
 *
 * Description:
 *   Register the function wl80211 calls each time the MAC releases a STA
 *   frame sent by wl80211_output().
 *
 * Input Parameters:
 *   cb - TX done notification.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void internal_register_txdone_cb(void (*cb)(void));

/****************************************************************************
 * Name: wl80211_output
 *
 * Description:
 *   Queue an IOB chain for transmission on the STA interface without
 *   copying it. The TX descriptor is written into the guard area in front
 *   of the first IOB.
 *
 * Input Parameters:
 *   buf - IOB chain holding the Ethernet frame.
 *
 * Returned Value:
 *   Zero on success; wl80211 frees the IOB chain once the MAC releases it
 *   and then calls the TX done notification. -EAGAIN while too many frames
 *   are in flight; the IOB chain still belongs to the caller. ERROR on any
 *   other failure, after the IOB chain has been freed.
 *
 ****************************************************************************/

int wl80211_output(struct iob_s *buf);

/****************************************************************************
 * Name: wl80211_output_ready
 *
 * Description:
 *   Report whether wl80211_output() can accept another frame, that is,
 *   whether the number of STA frames in flight is below the limit.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   True if another frame can be queued; false otherwise.
 *
 ****************************************************************************/

bool wl80211_output_ready(void);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_WL80211_PORT_H */
