/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_bod.c
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

#include <assert.h>
#include <errno.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include "bl616cl_sdk.h"
#include "bl616cl_hbn.h"
#include "bl616cl_bod.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_bod_interrupt
 ****************************************************************************/

static int bl616cl_bod_interrupt(int irq, void *context, void *arg)
{
  UNUSED(irq);
  UNUSED(context);
  UNUSED(arg);

  PANIC();
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_bod_initialize
 *
 * Description:
 *   Enable the BL616CL brown-out detector using the SDK HBN hook.
 *
 ****************************************************************************/

int bl616cl_bod_initialize(void)
{
  HBN_BOD_CFG_Type cfg =
  {
    .enableBod = ENABLE,
    .enableBodInt = ENABLE,
    .bodThreshold = HBN_BOD_THRES_2P4,
    .enablePorInBod = HBN_BOD_MODE_POR_INDEPENDENT,
  };

  int ret;

  ret = irq_attach(BL616CL_IRQ_BOD, bl616cl_bod_interrupt, NULL);
  if (ret < 0)
    {
      return ret;
    }

  ret = HBN_Set_BOD_Cfg(&cfg);
  if (ret != SUCCESS)
    {
      return -EIO;
    }

  up_enable_irq(BL616CL_IRQ_BOD);
  return OK;
}
