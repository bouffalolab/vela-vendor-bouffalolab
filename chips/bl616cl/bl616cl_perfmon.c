/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_perfmon.c
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

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/irq.h>
#include <nuttx/sched.h>

#include "riscv_internal.h"

#include "bl616cl_perfmon_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The T-Head helpers in LHAL rv_hpm.h pair event E with mhpmcounter(E+2)
 * on RV32; E907 implements mhpmcounter3..17.
 */

#define PERFMON_SET_EVENT(csr, event) \
  __asm__ volatile("csrw " csr ", %0" :: "r"(event))

/* Read a 64-bit counter on RV32: retry until the high word is stable. */

#define PERFMON_READ64(csr, dst) \
  do \
    { \
      uint32_t hi_; \
      uint32_t lo_; \
      uint32_t hi2_; \
      do \
        { \
          __asm__ volatile("csrr %0, " csr "h" : "=r"(hi_)); \
          __asm__ volatile("csrr %0, " csr : "=r"(lo_)); \
          __asm__ volatile("csrr %0, " csr "h" : "=r"(hi2_)); \
        } \
      while (hi_ != hi2_); \
      (dst) = ((uint64_t)hi_ << 32) | lo_; \
    } \
  while (0)

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint32_t g_perfmon_irqcount[NR_IRQS];
static uint64_t g_perfmon_irqcycles[NR_IRQS];

#ifdef CONFIG_BL616CL_PERFMON_PCSAMPLE
extern uint8_t _stext[];
extern uint8_t __bl616cl_text_end[];

static FAR uint16_t *volatile g_perfmon_hist;
static uintptr_t g_perfmon_base;
static size_t g_perfmon_nbuckets;
static unsigned int g_perfmon_shift;
static struct bl616cl_perfmon_pcstat_s g_perfmon_pcstat;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_BL616CL_PERFMON_PCSAMPLE
static void bl616cl_perfmon_sample(uintptr_t pc)
{
  FAR uint16_t *hist = g_perfmon_hist;
  size_t bucket = (pc - g_perfmon_base) >> g_perfmon_shift;

  g_perfmon_pcstat.samples++;
  if (is_idle_task(this_task()))
    {
      g_perfmon_pcstat.idle++;
    }
  else if (pc >= g_perfmon_base && bucket < g_perfmon_nbuckets)
    {
      if (hist[bucket] != UINT16_MAX)
        {
          hist[bucket]++;
        }
    }
  else
    {
      g_perfmon_pcstat.outside++;
    }
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bl616cl_perfmon_initialize(void)
{
  /* Event numbers: see enum bl616cl_perfmon_event_e and rv_hpm.h. */

  PERFMON_SET_EVENT("mhpmevent3", 1);   /* I-cache access */
  PERFMON_SET_EVENT("mhpmevent4", 2);   /* I-cache miss */
  PERFMON_SET_EVENT("mhpmevent8", 6);   /* Conditional branch mispredict */
  PERFMON_SET_EVENT("mhpmevent9", 7);   /* Conditional branch */
  PERFMON_SET_EVENT("mhpmevent14", 12); /* D-cache read access */
  PERFMON_SET_EVENT("mhpmevent15", 13); /* D-cache read miss */
  PERFMON_SET_EVENT("mhpmevent16", 14); /* D-cache write access */
  PERFMON_SET_EVENT("mhpmevent17", 15); /* D-cache write miss */
}

FAR void *bl616cl_perfmon_dispatch(int irq, FAR uintreg_t *regs)
{
  FAR void *ret;
  uint32_t start;

#ifdef CONFIG_BL616CL_PERFMON_PCSAMPLE
  if (irq == RISCV_IRQ_MTIMER && g_perfmon_hist != NULL)
    {
      bl616cl_perfmon_sample(regs[REG_EPC]);
    }
#endif

  start = READ_CSR(CSR_MCYCLE);
  ret = riscv_doirq(irq, regs);

  if (irq < NR_IRQS)
    {
      g_perfmon_irqcount[irq]++;
      g_perfmon_irqcycles[irq] += (uint32_t)(READ_CSR(CSR_MCYCLE) - start);
    }

  return ret;
}

void bl616cl_perfmon_read(FAR struct bl616cl_perfmon_counters_s *counters)
{
  PERFMON_READ64("mcycle", counters->cycle);
  PERFMON_READ64("minstret", counters->instret);
  PERFMON_READ64("mhpmcounter3",
                 counters->event[BL616CL_PERFMON_ICACHE_ACCESS]);
  PERFMON_READ64("mhpmcounter4",
                 counters->event[BL616CL_PERFMON_ICACHE_MISS]);
  PERFMON_READ64("mhpmcounter8",
                 counters->event[BL616CL_PERFMON_BRANCH_MISPREDICT]);
  PERFMON_READ64("mhpmcounter9",
                 counters->event[BL616CL_PERFMON_BRANCH]);
  PERFMON_READ64("mhpmcounter14",
                 counters->event[BL616CL_PERFMON_DCACHE_READ]);
  PERFMON_READ64("mhpmcounter15",
                 counters->event[BL616CL_PERFMON_DCACHE_READ_MISS]);
  PERFMON_READ64("mhpmcounter16",
                 counters->event[BL616CL_PERFMON_DCACHE_WRITE]);
  PERFMON_READ64("mhpmcounter17",
                 counters->event[BL616CL_PERFMON_DCACHE_WRITE_MISS]);
}

int bl616cl_perfmon_irq_read(FAR struct bl616cl_perfmon_irq_s *irqs,
                             int nirqs)
{
  irqstate_t flags;
  int i;

  if (nirqs > NR_IRQS)
    {
      nirqs = NR_IRQS;
    }

  flags = up_irq_save();
  for (i = 0; i < nirqs; i++)
    {
      irqs[i].count = g_perfmon_irqcount[i];
      irqs[i].cycles = g_perfmon_irqcycles[i];
    }

  up_irq_restore(flags);
  return nirqs;
}

#ifdef CONFIG_BL616CL_PERFMON_PCSAMPLE
void bl616cl_perfmon_text_range(FAR uintptr_t *start, FAR size_t *size)
{
  *start = (uintptr_t)_stext;
  *size = __bl616cl_text_end - _stext;
}

int bl616cl_perfmon_pc_start(FAR uint16_t *hist, uintptr_t base,
                             size_t nbuckets, unsigned int shift)
{
  irqstate_t flags;

  if (hist == NULL || nbuckets == 0 || shift < 1 || shift > 16)
    {
      return -EINVAL;
    }

  flags = up_irq_save();
  if (g_perfmon_hist != NULL)
    {
      up_irq_restore(flags);
      return -EBUSY;
    }

  g_perfmon_base = base;
  g_perfmon_nbuckets = nbuckets;
  g_perfmon_shift = shift;
  memset(&g_perfmon_pcstat, 0, sizeof(g_perfmon_pcstat));
  g_perfmon_hist = hist;
  up_irq_restore(flags);
  return OK;
}

void bl616cl_perfmon_pc_stop(FAR struct bl616cl_perfmon_pcstat_s *stat)
{
  irqstate_t flags;

  flags = up_irq_save();
  g_perfmon_hist = NULL;
  *stat = g_perfmon_pcstat;
  up_irq_restore(flags);
}
#endif
