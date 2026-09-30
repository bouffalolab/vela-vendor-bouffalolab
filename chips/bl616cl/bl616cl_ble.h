/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_ble.h
 *
 * BL616CL BLE controller: HCI transport and controller glue.
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_BLE_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_BLE_H

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_ble_initialize
 *
 * Description:
 *   Load the RF parameters and register the on-chip controller as the
 *   Bluetooth HCI device (/dev/ttyHCI<CONFIG_BLUETOOTH_DEVICE_ID>).  The
 *   controller itself starts when the host opens the device.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int bl616cl_ble_initialize(void);

/****************************************************************************
 * Name: bl616cl_ble_controller_start
 *
 * Description:
 *   Start the controller library: its thread, queue and interrupts.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int bl616cl_ble_controller_start(void);

/****************************************************************************
 * Name: bl616cl_ble_controller_stop
 *
 * Description:
 *   Stop the controller library and release what start created.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_ble_controller_stop(void);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_BLE_H */
