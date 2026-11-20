/****************************************************************************
 * vendor/bouffalolab/chips/bl618dg/bl618dg_start.c
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
#include <nuttx/init.h>
#include <arch/board/board.h>

#include "riscv_internal.h"

#include "bl618dg_cpu.h"
#include "bl618dg_cache.h"
#include "bl618dg_clockconfig.h"
#include "bl618dg_lowputc.h"
#include "bl618dg_memory.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __bl618dg_start
 *
 * Description:
 *   C entry point of the BL618DG AP. Configure access permissions and
 *   caches, copy initialized RAM sections from Flash and clear BSS, then
 *   synchronize caches and initialize clocks before calling nx_start().
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None. This function does not return.
 *
 ****************************************************************************/

void __bl618dg_start(void)
{
  if (bl618dg_cpu_init() < 0)
    {
      for (; ; )
        {
        }
    }

  bl618dg_cache_early_init();
  bl618dg_section_load();
  bl618dg_cache_after_load();
  if (bl618dg_clock_early_init() < 0)
    {
      /* Do not start the scheduler with an unverified timer clock. */

      for (; ; )
        {
        }
    }

  bl618dg_pinmux_early_uart();
  riscv_earlyserialinit();

  nx_start();
  for (; ; )
    {
    }
}
