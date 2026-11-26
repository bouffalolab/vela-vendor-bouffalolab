/****************************************************************************
 * vendor/bouffalolab/components/display/nuttx/display_os_nuttx.c
 *
 * NuttX implementation of the display framework platform layer
 * (display_os.h): bus lock semaphore, critical section, data cache clean,
 * interrupt context check and delay. The GPIO functions need LHAL and are
 * implemented by the chip (chips/bl616cl/bl616cl_display.c).
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/clock.h>
#include <nuttx/init.h>
#include <nuttx/irq.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>

#include "display_os.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int display_os_sem_init(display_os_sem_t *sem)
{
  return nxsem_init(&sem->sem, 0, 1);
}

void display_os_sem_deinit(display_os_sem_t *sem)
{
  nxsem_destroy(&sem->sem);
}

int display_os_sem_take(display_os_sem_t *sem, uint32_t timeout_ms)
{
  if (timeout_ms == DISPLAY_OS_WAIT_FOREVER)
    {
      return nxsem_wait_uninterruptible(&sem->sem);
    }

  return nxsem_tickwait_uninterruptible(&sem->sem, MSEC2TICK(timeout_ms));
}

void display_os_sem_give(display_os_sem_t *sem)
{
  irqstate_t flags;
  int value;

  /* Binary semaphore, as on the SDK: giving an available one does
   * nothing.
   */

  flags = enter_critical_section();
  if (nxsem_get_value(&sem->sem, &value) >= 0 && value < 1)
    {
      nxsem_post(&sem->sem);
    }

  leave_critical_section(flags);
}

uintptr_t display_os_irq_save(void)
{
  return (uintptr_t)enter_critical_section();
}

void display_os_irq_restore(uintptr_t flags)
{
  leave_critical_section((irqstate_t)flags);
}

void display_os_dcache_clean(const void *addr, size_t len)
{
  up_clean_dcache((uintptr_t)addr, (uintptr_t)addr + len);
}

bool display_os_in_isr(void)
{
  return up_interrupt_context();
}

void display_os_delay_ms(uint32_t ms)
{
  if (!OSINIT_OS_READY() || up_interrupt_context())
    {
      up_mdelay(ms);
      return;
    }

  /* Never round a short delay down to zero, which would not wait */

  nxsig_usleep(ms > 0 ? ms * USEC_PER_MSEC : USEC_PER_TICK);
}
