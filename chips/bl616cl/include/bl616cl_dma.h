/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/include/bl616cl_dma.h
 *
 * SPDX-License-Identifier: Apache-2.0
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
/* This public accessor is the chip discovery contract for generic DMA
 * clients; the standard DMA API does not provide device discovery.
 */

/* DMA0 currently supports one-shot memory-to-memory transfers with equal
 * 1, 2, or 4-byte widths and source/destination steps of zero or one width.
 * Clients own all cache clean and invalidate operations for their buffers.
 */

FAR struct dma_dev_s *bl616cl_dma0_device(void);
#endif

#ifdef CONFIG_BL616CL_DMA0_TEST
/* The DMA test application uses this public, configuration-gated extension
 * because the generic DMA API cannot inject or observe interrupt races.
 */

void bl616cl_dma_test_inject_irq(uint8_t tc_status, uint8_t error_status);
void bl616cl_dma_test_set_hold_before_enable(bool hold);
void bl616cl_dma_test_suppress_put_assert(bool suppress);
void bl616cl_dma_test_release_hold(void);
void bl616cl_dma_test_get_status(
  FAR struct bl616cl_dma_test_status_s *status);
#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_DMA_H */
