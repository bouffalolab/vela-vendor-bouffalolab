/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_timerisr.c
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

#include <nuttx/timers/arch_alarm.h>

#include <arch/irq.h>

#include "riscv_mtimer.h"

#include "bl616cl_clock.h"
#include "chip.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_timer_initialize
 *
 * Description:
 *   This function is called during start-up to initialize the timer
 *   interrupt.
 *
 ****************************************************************************/

void up_timer_initialize(void)
{
  struct oneshot_lowerhalf_s *lower;

  bl616cl_timer_clock_init();

  lower = riscv_mtimer_initialize(BL616CL_CORET_MTIME,
                                  BL616CL_CORET_MTIMECMP,
                                  RISCV_IRQ_MTIMER,
                                  BL616CL_MTIMER_FREQ);

  DEBUGASSERT(lower != NULL);
  up_alarm_set_lowerhalf(lower);
}
