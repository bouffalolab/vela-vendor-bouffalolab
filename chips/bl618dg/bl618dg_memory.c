/****************************************************************************
 * vendor/bouffalolab/chips/bl618dg/bl618dg_memory.c
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

#include <stddef.h>
#include <stdint.h>

#include "bl618dg_memory.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL618DG_SECTION_SENTINEL           0xffffffff

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl618dg_mem_section_s
{
  uint32_t *start;
  uint32_t *end;
};

struct bl618dg_mem_load_section_s
{
  uint32_t *start;
  uint32_t *end;
  uint32_t *load;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

extern struct bl618dg_mem_section_s __mem_setz_sections[];
extern struct bl618dg_mem_load_section_s __mem_copy_sections[];

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl618dg_section_load
 *
 * Description:
 *   Copy initialized RAM sections from their load images, then clear the
 *   zero-table entries. Both tables end with a sentinel. The linker keeps
 *   no-init data and the active idle stack outside these ranges.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl618dg_section_load(void)
{
  struct bl618dg_mem_load_section_s *copy = __mem_copy_sections;
  struct bl618dg_mem_section_s *setz = __mem_setz_sections;
  uint32_t *src;
  uint32_t *dest;
  int i;

  for (i = 0; (uintptr_t)copy[i].start != BL618DG_SECTION_SENTINEL; i++)
    {
      if (copy[i].start == NULL || copy[i].end == NULL ||
          copy[i].load == NULL)
        {
          continue;
        }

      src = copy[i].load;
      dest = copy[i].start;
      while (dest < copy[i].end)
        {
          *dest++ = *src++;
        }
    }

  for (i = 0; (uintptr_t)setz[i].start != BL618DG_SECTION_SENTINEL; i++)
    {
      if (setz[i].start == NULL || setz[i].end == NULL)
        {
          continue;
        }

      dest = setz[i].start;
      while (dest < setz[i].end)
        {
          *dest++ = 0;
        }
    }
}
