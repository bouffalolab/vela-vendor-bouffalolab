/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/include/bl616cl_psram.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_PSRAM_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_INCLUDE_BL616CL_PSRAM_H

#include <nuttx/config.h>
#include <stddef.h>

#define BL616CL_PSRAM_BASE          (0x88000000)
#define BL616CL_PSRAM_NOCACHE_BASE  0x18000000u
#define BL616CL_PSRAM_MAX_SIZE      (16u * 1024u * 1024u)

#ifdef CONFIG_BL616CL_PSRAM
/* Returns the usable heap region size, or zero if initialization failed.
 * The uncached alias accesses the same storage. Clients must synchronize
 * caches before accessing an allocation through that alias.
 */

size_t bl616cl_psram_size_get(void);
#endif

#endif
