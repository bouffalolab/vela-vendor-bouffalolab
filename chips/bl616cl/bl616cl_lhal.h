/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_lhal.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_LHAL_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_LHAL_H

#include <nuttx/config.h>
#include <errno.h>
#include <nuttx/arch.h>
#include <sys/types.h>

#include "riscv_internal.h"
#include "hardware/bl616cl_memorymap.h"

/* bflb_core.h imports the toolchain's sys/errno.h. Preserve the NuttX errno
 * ABI and register access helpers while making the LHAL declarations
 * available to chip adapter sources.
 */

#pragma push_macro("getreg8")
#pragma push_macro("getreg16")
#pragma push_macro("getreg32")
#pragma push_macro("putreg8")
#pragma push_macro("putreg16")
#pragma push_macro("putreg32")
#pragma push_macro("BIT")
#pragma push_macro("EADDRINUSE")
#pragma push_macro("EADDRNOTAVAIL")
#pragma push_macro("EAFNOSUPPORT")
#pragma push_macro("EALREADY")
#pragma push_macro("EBADMSG")
#pragma push_macro("ECANCELED")
#pragma push_macro("ECONNABORTED")
#pragma push_macro("EDEADLK")
#pragma push_macro("EDESTADDRREQ")
#pragma push_macro("EDQUOT")
#pragma push_macro("EFTYPE")
#pragma push_macro("EHOSTDOWN")
#pragma push_macro("EHOSTUNREACH")
#pragma push_macro("EIDRM")
#pragma push_macro("EILSEQ")
#pragma push_macro("EINPROGRESS")
#pragma push_macro("EISCONN")
#pragma push_macro("ELOOP")
#pragma push_macro("EMSGSIZE")
#pragma push_macro("EMULTIHOP")
#pragma push_macro("ENAMETOOLONG")
#pragma push_macro("ENETDOWN")
#pragma push_macro("ENETRESET")
#pragma push_macro("ENETUNREACH")
#pragma push_macro("ENOLCK")
#pragma push_macro("ENOMSG")
#pragma push_macro("ENOPROTOOPT")
#pragma push_macro("ENOSYS")
#pragma push_macro("ENOTCONN")
#pragma push_macro("ENOTEMPTY")
#pragma push_macro("ENOTRECOVERABLE")
#pragma push_macro("ENOTSOCK")
#pragma push_macro("ENOTSUP")
#pragma push_macro("EOVERFLOW")
#pragma push_macro("EOWNERDEAD")
#pragma push_macro("EPROTONOSUPPORT")
#pragma push_macro("EPROTOTYPE")
#pragma push_macro("ESTALE")
#pragma push_macro("ETIMEDOUT")
#pragma push_macro("ETOOMANYREFS")
#undef getreg8
#undef getreg16
#undef getreg32
#undef putreg8
#undef putreg16
#undef putreg32
#include "bflb_core.h"

enum
{
  BL616CL_LHAL_ETIMEDOUT = ETIMEDOUT,
};

#pragma pop_macro("ETOOMANYREFS")
#pragma pop_macro("ETIMEDOUT")
#pragma pop_macro("ESTALE")
#pragma pop_macro("EPROTOTYPE")
#pragma pop_macro("EPROTONOSUPPORT")
#pragma pop_macro("EOWNERDEAD")
#pragma pop_macro("EOVERFLOW")
#pragma pop_macro("ENOTSUP")
#pragma pop_macro("ENOTSOCK")
#pragma pop_macro("ENOTRECOVERABLE")
#pragma pop_macro("ENOTEMPTY")
#pragma pop_macro("ENOTCONN")
#pragma pop_macro("ENOSYS")
#pragma pop_macro("ENOPROTOOPT")
#pragma pop_macro("ENOMSG")
#pragma pop_macro("ENOLCK")
#pragma pop_macro("ENETUNREACH")
#pragma pop_macro("ENETRESET")
#pragma pop_macro("ENETDOWN")
#pragma pop_macro("ENAMETOOLONG")
#pragma pop_macro("EMULTIHOP")
#pragma pop_macro("EMSGSIZE")
#pragma pop_macro("ELOOP")
#pragma pop_macro("EISCONN")
#pragma pop_macro("EINPROGRESS")
#pragma pop_macro("EILSEQ")
#pragma pop_macro("EIDRM")
#pragma pop_macro("EHOSTUNREACH")
#pragma pop_macro("EHOSTDOWN")
#pragma pop_macro("EFTYPE")
#pragma pop_macro("EDQUOT")
#pragma pop_macro("EDESTADDRREQ")
#pragma pop_macro("EDEADLK")
#pragma pop_macro("ECONNABORTED")
#pragma pop_macro("ECANCELED")
#pragma pop_macro("EBADMSG")
#pragma pop_macro("EALREADY")
#pragma pop_macro("EAFNOSUPPORT")
#pragma pop_macro("EADDRNOTAVAIL")
#pragma pop_macro("EADDRINUSE")
#pragma pop_macro("BIT")
#pragma pop_macro("putreg32")
#pragma pop_macro("putreg16")
#pragma pop_macro("putreg8")
#pragma pop_macro("getreg32")
#pragma pop_macro("getreg16")
#pragma pop_macro("getreg8")

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_LHAL_H */
