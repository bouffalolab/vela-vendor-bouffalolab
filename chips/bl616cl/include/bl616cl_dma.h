/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/include/bl616cl_dma.h
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

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BL616CL_DMA0
/* DMA0 currently supports one-shot memory-to-memory transfers with equal
 * 1, 2, or 4-byte widths and source/destination steps of zero or one width.
 * Clients own all cache clean and invalidate operations for their buffers.
 */

FAR struct dma_dev_s *bl616cl_dma0_device(void);
#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_DMA_H */
