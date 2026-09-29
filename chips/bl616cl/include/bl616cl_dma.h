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

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

#ifdef CONFIG_BL616CL_DMA0_TEST
struct bl616cl_dma_test_status_s
{
  uint8_t tc_status;
  uint8_t error_status;
  uint8_t tc_clear_status;
  uint8_t error_clear_status;
  uint32_t rejected_puts;
  uint32_t irq_count;
  uint32_t software_injection_count;
  uint32_t callback_count;
};
#endif

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
 *   This accessor is the chip discovery contract for generic DMA clients; the
 *   standard DMA API does not provide device discovery. DMA0 currently
 *   supports one-shot memory-to-memory transfers with equal 1, 2, or 4-byte
 *   widths and source/destination steps of zero or one width. Clients own
 *   all cache clean and invalidate operations for their buffers.
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

#ifdef CONFIG_BL616CL_DMA0_TEST

/****************************************************************************
 * Name: bl616cl_dma_test_inject_irq
 *
 * Description:
 *   Test hook: feed software terminal-count and error status into the
 *   interrupt processing path, marking the caller as running in callback
 *   context.
 *
 *   The DMA test application uses these configuration-gated test hooks
 *   because the generic DMA API cannot inject or observe interrupt races.
 *
 * Input Parameters:
 *   tc_status - Terminal-count status bit mask.
 *   error_status - Error status bit mask.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_dma_test_inject_irq(uint8_t tc_status, uint8_t error_status);

/****************************************************************************
 * Name: bl616cl_dma_test_set_hold_before_enable
 *
 * Description:
 *   Test hook: when set, later transfers are programmed but not enabled
 *   until bl616cl_dma_test_release_hold() is called.
 *
 * Input Parameters:
 *   hold - True to hold new transfers before enable.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_dma_test_set_hold_before_enable(bool hold);

/****************************************************************************
 * Name: bl616cl_dma_test_suppress_put_assert
 *
 * Description:
 *   Test hook: control whether put_chan called from a callback context
 *   triggers DEBUGASSERT. Rejected calls are counted either way.
 *
 * Input Parameters:
 *   suppress - True to suppress the assertion.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_dma_test_suppress_put_assert(bool suppress);

/****************************************************************************
 * Name: bl616cl_dma_test_release_hold
 *
 * Description:
 *   Test hook: enable all running channels that are being held before
 *   enable.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_dma_test_release_hold(void);

/****************************************************************************
 * Name: bl616cl_dma_test_get_status
 *
 * Description:
 *   Test hook: copy the interrupt and callback statistics collected by
 *   the driver.
 *
 * Input Parameters:
 *   status - Location that receives the statistics; nothing is done if
 *     NULL.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_dma_test_get_status(
  struct bl616cl_dma_test_status_s *status);

#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_DMA_H */
