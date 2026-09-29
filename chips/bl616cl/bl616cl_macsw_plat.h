/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_macsw_plat.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_MACSW_PLAT_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_MACSW_PLAT_H

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_macsw_wait_suspended
 *
 * Description:
 *   Wait until the Wi-Fi task first blocks in wifi_task_suspend(), that is,
 *   until g_wifi_notify_sem has a waiter.  Polling keeps
 *   wifi_task_suspend(), which is on the hot path, unchanged.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_macsw_wait_suspended(void);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_MACSW_PLAT_H */
