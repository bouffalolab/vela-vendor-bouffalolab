/****************************************************************************
 * vendor/bouffalolab/chips/bl618dg/bl618dg_allocateheap.c
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

#include <stdint.h>

#include <nuttx/arch.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

extern char g_heapbase[] asm("__HeapBase");
extern char g_heaplimit[] asm("__HeapLimit");

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_allocate_heap
 *
 * Description:
 *   Set aside the single heap for the flat build. It starts after the idle
 *   stack and extends to the linker-defined limit of cacheable OCRAM.
 *
 * Input Parameters:
 *   heap_start - Location to return the start address of the heap.
 *   heap_size  - Location to return the size of the heap in bytes.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void up_allocate_heap(void **heap_start, size_t *heap_size)
{
  *heap_start = g_heapbase;
  *heap_size = (uintptr_t)g_heaplimit - (uintptr_t)g_heapbase;
}
