/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/include/bl616cl_dma.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_DMA_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_DMA_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/dma/dma.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BL616CL_DMA0

/****************************************************************************
 * Name: bl616cl_dma0_device
 *
 * Description:
 *   Return the DMA0 controller device, for clients to call get_chan and
 *   put_chan on.
 *
 *   This accessor is the chip discovery contract for generic DMA clients;
 *   the standard DMA API does not provide device discovery. DMA0 currently
 *   supports one-shot memory-to-memory, memory-to-peripheral and
 *   peripheral-to-memory transfers with equal 1, 2, or 4-byte widths and
 *   source/destination steps of zero or one width. Peripheral transfers
 *   take the LHAL DMA_REQUEST_* number as dst_drq (to the device) or
 *   src_drq (from the device). Clients own all cache clean and invalidate
 *   operations for their buffers.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The DMA device if riscv_dma_initialize() succeeded; NULL otherwise.
 *
 ****************************************************************************/

struct dma_dev_s *bl616cl_dma0_device(void);

#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_DMA_H */
