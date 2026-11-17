/****************************************************************************
 * vendor/bouffalolab/chips/bl618dg/bl618dg_head.S
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

#include "riscv_encoding.h"

/****************************************************************************
 * Public Symbols
 ****************************************************************************/

  .section .text.entry
  .balign 64
  .global __start
  .type __start, %function

/****************************************************************************
 * Name: __start
 *
 * Description:
 *   Initialize the AP registers, idle stack, trap entry, NMI routing,
 *   counters and FPU before entering __bl618dg_start(). The image must
 *   be loaded at its linked addresses, with memory access permissions,
 *   cache coherency and clocks configured before entry.
 *
 ****************************************************************************/

__start:
  csrci mstatus, 8
  csrw mie, zero

  .option push
  .option norelax
  la gp, __global_pointer$
  la sp, __StackTop
  .option pop
  csrw mscratch, sp

  la t0, exception_common
  ori t0, t0, 3
  csrw mtvec, t0

  /* Route NMI through mtvec with cause 0xfff. */

  li t0, MMISC_CTL_NMI_CAUSE_FFF
  csrs CSR_MMISC_CTL, t0
  csrci CSR_MCOUNTINHIBIT, 0x5

#ifdef CONFIG_ARCH_FPU
  /* Reset FS before the NuttX helper sets its initial state and fcsr. */

  li t0, MSTATUS_FS
  csrc CSR_MSTATUS, t0
  call riscv_fpuconfig
#endif

  tail __bl618dg_start
  .size __start, . - __start

  .global _init
  .global _fini
/****************************************************************************
 * Name: _init / _fini
 *
 * Description:
 *   Provide empty initialization and finalization hooks for this image.
 *
 ****************************************************************************/

_init:
_fini:
  ret
