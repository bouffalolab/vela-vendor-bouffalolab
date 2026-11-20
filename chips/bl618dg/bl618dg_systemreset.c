/****************************************************************************
 * vendor/bouffalolab/chips/bl618dg/bl618dg_systemreset.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include "bl618dg_sdk.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include "bl618dg_glb.h"
#pragma GCC diagnostic pop

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_systemreset
 *
 * Description:
 *   Disable interrupts and request the DG software power-on reset.
 *   The SDK reset routine executes from RAM while changing reset controls.
 *
 * Returned Value:
 *   Does not return.
 *
 ****************************************************************************/

void up_systemreset(void)
{
  up_irq_save();
  GLB_SW_POR_Reset();

  for (; ; )
    {
    }
}
