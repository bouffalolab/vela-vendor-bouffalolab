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

#include <nuttx/init.h>

#include "bl618dg_memory.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __bl618dg_start
 *
 * Description:
 *   C entry point of the BL618DG chip. Initialize RAM sections and call
 *   nx_start().
 *   Code and initialized data must be loaded at their linked RAM addresses
 *   before entry.
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
  bl618dg_section_load();
  nx_start();
  for (; ; )
    {
    }
}
