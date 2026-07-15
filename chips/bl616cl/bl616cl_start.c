/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_start.c
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

#include "bl616cl_cache.h"
#include "bl616cl_clock.h"
#include "bl616cl_cpu.h"
#include "bl616cl_flash.h"
#include "bl616cl_memory.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __bl616cl_start
 ****************************************************************************/

void __bl616cl_start(void)
{
  bl616cl_thead_cpu_init();
  bl616cl_memory_early_init();
  bl616cl_flash_early_init();
  bl616cl_pmp_init();
  bl616cl_cache_early_init();
  bl616cl_section_load();
  bl616cl_cache_after_load();

  (void)bl616cl_flash_initialize();

  bl616cl_clock_early_init();
  bl616cl_pinmux_early_uart();
  riscv_earlyserialinit();
  nx_start();

  for (; ; )
    {
    }
}
