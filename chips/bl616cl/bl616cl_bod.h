/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_bod.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_BOD_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_BOD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_bod_initialize
 *
 * Description:
 *   Enable the BL616CL brown-out detector using the SDK HBN hook. The
 *   threshold is 2.4 V, the interrupt is enabled and POR is independent of
 *   BOD. The interrupt handler panics the system.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *   -EIO - HBN_Set_BOD_Cfg() failed.
 *   Other errors are returned by irq_attach().
 *
 ****************************************************************************/

int bl616cl_bod_initialize(void);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_BOD_H */
