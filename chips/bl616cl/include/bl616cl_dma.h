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
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_DMA0_NCHANNELS  8

/* DMA0 channel assignment, as set in the "DMA0 channel assignment" menu. A
 * user takes its channel at initialization and keeps it; channels are
 * never shared.
 *
 * BL616CL_DMA0_ASSIGNMENTS(X, arg) expands X(arg, channel, name) once for
 * each channel of an enabled SDK driver. A new SDK driver adds its entries
 * here; the reserved mask, the build-time duplicate check and the conflict
 * log are derived from this list.
 */

#ifdef CONFIG_BL616CL_I2S
#  define BL616CL_DMA0_I2S(X, arg) \
     X(arg, CONFIG_BL616CL_I2S_TX_DMA_CHANNEL, "I2S0 TX") \
     X(arg, CONFIG_BL616CL_I2S_RX_DMA_CHANNEL, "I2S0 RX")
#else
#  define BL616CL_DMA0_I2S(X, arg)
#endif

#define BL616CL_DMA0_ASSIGNMENTS(X, arg) \
  BL616CL_DMA0_I2S(X, arg)

/* Channels declared for application code, one bit per channel */

#ifdef CONFIG_BL616CL_DMA0_APP_CHANNELS
#  define BL616CL_DMA0_APP      CONFIG_BL616CL_DMA0_APP_CHANNELS
#else
#  define BL616CL_DMA0_APP      0
#endif

/* All channels assigned in the menu, one bit per channel. Code that is not
 * in the menu, such as tests, may only use the other channels.
 */

#define BL616CL_DMA0_BIT(arg, channel, name) | (1u << (channel))
#define BL616CL_DMA0_RESERVED \
  (BL616CL_DMA0_APP BL616CL_DMA0_ASSIGNMENTS(BL616CL_DMA0_BIT, 0))

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
 *   the standard DMA API does not provide device discovery. DMA0 supports
 *   memory-to-memory, memory-to-peripheral and peripheral-to-memory
 *   transfers with equal 1, 2, or 4-byte widths and source/destination
 *   steps of zero or one width. Peripheral transfers take the LHAL
 *   DMA_REQUEST_* number as dst_drq (to the device) or src_drq (from the
 *   device). One-shot transfers longer than 4095 units, cyclic transfers
 *   and pause/resume of cyclic transfers need
 *   CONFIG_BL616CL_DMA0_LLI_NODES. Clients own all cache clean and
 *   invalidate operations for their buffers.
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
