/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/include/bl616cl_dma_test.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_DMA_TEST_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_DMA_TEST_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

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

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BL616CL_DMA0_TEST
void bl616cl_dma_test_inject_irq(uint8_t tc_status, uint8_t error_status);
void bl616cl_dma_test_set_hold_before_enable(bool hold);
void bl616cl_dma_test_suppress_put_assert(bool suppress);
void bl616cl_dma_test_release_hold(void);
void bl616cl_dma_test_get_status(
  FAR struct bl616cl_dma_test_status_s *status);
#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_DMA_TEST_H */
