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

#include "bl616cl_bod.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_BOD_ENABLE          1
#define BL616CL_BOD_IRQ_ENABLE      1
#define BL616CL_BOD_THRESHOLD_2P4   2
#define BL616CL_BOD_POR_INDEPENDENT 0

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl616cl_sdk_hbn_bod_cfg_s
{
  uint8_t enable_bod;
  uint8_t enable_bod_int;
  uint8_t bod_threshold;
  uint8_t enable_por_in_bod;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

extern int bl616cl_sdk_hbn_set_bod_cfg(
  struct bl616cl_sdk_hbn_bod_cfg_s *cfg) __asm__("HBN_Set_BOD_Cfg");

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
  struct bl616cl_sdk_hbn_bod_cfg_s cfg =
  {
    .enable_bod = BL616CL_BOD_ENABLE,
    .enable_bod_int = BL616CL_BOD_IRQ_ENABLE,
    .bod_threshold = BL616CL_BOD_THRESHOLD_2P4,
    .enable_por_in_bod = BL616CL_BOD_POR_INDEPENDENT,
  };

  int ret;

  ret = irq_attach(BL616CL_IRQ_BOD, bl616cl_bod_interrupt, NULL);
  if (ret < 0)
    {
      return ret;
    }

  ret = bl616cl_sdk_hbn_set_bod_cfg(&cfg);
  if (ret != OK)
    {
      return -EIO;
    }

  up_enable_irq(BL616CL_IRQ_BOD);
  return OK;
}
