/****************************************************************************
 * apps/os_feature_tests/cache/cache_test_trampoline.S
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

  .section .text.bl_cache_test_invalidate_all,"ax",@progbits
  .align  2
  .global bl_cache_test_invalidate_all
  .type   bl_cache_test_invalidate_all, @function

bl_cache_test_invalidate_all:
  addi  a0, a0, -16
  sw    sp, 0(a0)
  sw    ra, 4(a0)
  mv    sp, a0
  call  up_invalidate_dcache_all
  lw    ra, 4(sp)
  lw    a0, 0(sp)
  mv    sp, a0
  ret

  .size bl_cache_test_invalidate_all, .-bl_cache_test_invalidate_all
