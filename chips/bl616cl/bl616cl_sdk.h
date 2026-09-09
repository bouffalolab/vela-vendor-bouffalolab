/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_sdk.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SDK_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SDK_H

#include "bl616cl_lhal.h"

/* bflb_sf_ctrl.h has one legacy non-prototype declaration. Import it once
 * under a narrow diagnostic guard before std headers include it transitively.
 */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include "bflb_sf_ctrl.h"
#pragma GCC diagnostic pop

/* Import the upstream types once, before including any std API header.
 * Rename its ERROR enumerator without changing BL_Err_Type or its ABI,
 * after the LHAL compatibility header has imported bflb_core.h.
 */

#pragma push_macro("ERROR")
#undef ERROR
#define ERROR BL616CL_SDK_ERROR
#include "bl616cl_common.h"
#pragma pop_macro("ERROR")

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SDK_H */
