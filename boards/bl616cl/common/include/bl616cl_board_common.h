/****************************************************************************
 * vendor/bouffalolab/boards/bl616cl/common/include/bl616cl_board_common.h
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

#ifndef __VENDOR_BOUFFALOLAB_BOARDS_BL616CL_COMMON_BL616CL_BOARD_COMMON_H
#define __VENDOR_BOUFFALOLAB_BOARDS_BL616CL_COMMON_BL616CL_BOARD_COMMON_H

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_bringup
 *
 * Description:
 *   Perform common BL616CL board initialization after nx_start() has entered
 *   the NuttX initialization path.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero (OK). A step that fails is logged and the remaining steps still
 *   run, so that one device does not keep the others from registering.
 *
 ****************************************************************************/

int bl616cl_bringup(void);

/****************************************************************************
 * Name: bl616cl_board_initialize
 *
 * Description:
 *   Initialize peripherals whose registration depends on this board's
 *   wiring.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   None. A peripheral that fails is logged and the others are still
 *   initialized.
 *
 ****************************************************************************/

void bl616cl_board_initialize(void);

#endif /* __VENDOR_BOUFFALOLAB_BOARDS_BL616CL_COMMON_BL616CL_BOARD_COMMON_H */
