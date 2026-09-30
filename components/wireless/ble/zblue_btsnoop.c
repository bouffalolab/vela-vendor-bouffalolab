/****************************************************************************
 * vendor/bouffalolab/components/wireless/ble/zblue_btsnoop.c
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

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <stdint.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: btsnoop_log_capture
 *
 * Description:
 *   zblue's H4 driver hands every HCI packet to this hook.  The openvela
 *   Bluetooth service, which is not part of this SDK, implements it; here
 *   packets are not captured.
 *
 ****************************************************************************/

weak_function void btsnoop_log_capture(uint8_t is_receive,
                                       uint8_t *hci_pkt,
                                       uint32_t hci_pkt_size)
{
}
