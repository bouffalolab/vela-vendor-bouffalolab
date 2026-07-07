/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_head.S
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

#include "chip.h"

/****************************************************************************
 * Public Symbols
 ****************************************************************************/

  .section .text.entry
  .global __start
  .type   __start, %function

/****************************************************************************
 * Name: __start
 ****************************************************************************/

__start:
  .option push
  .option norelax

  la      gp, __global_pointer$
  la      sp, __StackTop
  csrw    mscratch, sp

  .option pop

  csrw    mstatus, zero
  csrw    mie, zero

  /* Use numeric T-Head CSR encodings so the current GCC can assemble this. */

  csrr    t0, 0x7c5
  lui     t1, 0x100
  or      t0, t0, t1
  csrw    0x7c5, t0

  la      t0, exception_common
  csrw    mtvec, t0

  la      t0, __bl616cl_start
  jr      t0

  .size __start, . - __start

  .global _init
  .global _fini

_init:
_fini:
  ret
