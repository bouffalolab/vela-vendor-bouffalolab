/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_wdt.h
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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_WDT_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_WDT_H

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifndef __ASSEMBLY__

/****************************************************************************
 * Name: bl616cl_wdt_initialize
 *
 * Description:
 *   Initialize the watchdog timer and register it as devpath. The watchdog
 *   is left stopped after initialization; the upper half controls the
 *   start/stop/timeout lifecycle through the lower half operations.
 *
 * Input Parameters:
 *   devpath - The full path to the watchdog device, e.g. /dev/watchdog0.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int bl616cl_wdt_initialize(FAR const char *devpath);

#endif /* __ASSEMBLY__ */

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_WDT_H */
