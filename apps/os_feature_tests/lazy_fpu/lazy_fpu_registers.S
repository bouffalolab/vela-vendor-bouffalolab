/****************************************************************************
 * apps/vendor/bouffalolab/apps/os_feature_tests/lazy_fpu/
 * lazy_fpu_registers.S
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#include <nuttx/config.h>

  .section .text
  .align  2
  .global bl_lazy_fpu_register_test
  .type   bl_lazy_fpu_register_test, function

bl_lazy_fpu_register_test:
  addi sp, sp, -64
  sw   ra, 0(sp)
  sw   s0, 4(sp)
  sw   s1, 8(sp)
  sw   s2, 12(sp)
  fsw  fs0, 16(sp)
  fsw  fs1, 20(sp)
  fsw  fs2, 24(sp)
  fsw  fs3, 28(sp)
  fsw  fs4, 32(sp)
  fsw  fs5, 36(sp)
  fsw  fs6, 40(sp)
  fsw  fs7, 44(sp)
  fsw  fs8, 48(sp)
  fsw  fs9, 52(sp)
  fsw  fs10, 56(sp)
  fsw  fs11, 60(sp)

  mv   s0, a0
  mv   s1, a1
  mv   s2, a2
  flw  fs0, 0(s0)
  flw  fs1, 4(s0)
  flw  fs2, 8(s0)
  flw  fs3, 12(s0)
  flw  fs4, 16(s0)
  flw  fs5, 20(s0)
  flw  fs6, 24(s0)
  flw  fs7, 28(s0)
  flw  fs8, 32(s0)
  flw  fs9, 36(s0)
  flw  fs10, 40(s0)
  flw  fs11, 44(s0)
  lw   t0, 48(s0)
  fscsr t0

1:
  call bl_lazy_fpu_test_yield
  addi s2, s2, -1
  bnez s2, 1b

  fsw  fs0, 0(s1)
  fsw  fs1, 4(s1)
  fsw  fs2, 8(s1)
  fsw  fs3, 12(s1)
  fsw  fs4, 16(s1)
  fsw  fs5, 20(s1)
  fsw  fs6, 24(s1)
  fsw  fs7, 28(s1)
  fsw  fs8, 32(s1)
  fsw  fs9, 36(s1)
  fsw  fs10, 40(s1)
  fsw  fs11, 44(s1)
  frcsr t0
  sw   t0, 48(s1)

  li   a0, 0
  lw   ra, 0(sp)
  lw   s0, 4(sp)
  lw   s1, 8(sp)
  lw   s2, 12(sp)
  flw  fs0, 16(sp)
  flw  fs1, 20(sp)
  flw  fs2, 24(sp)
  flw  fs3, 28(sp)
  flw  fs4, 32(sp)
  flw  fs5, 36(sp)
  flw  fs6, 40(sp)
  flw  fs7, 44(sp)
  flw  fs8, 48(sp)
  flw  fs9, 52(sp)
  flw  fs10, 56(sp)
  flw  fs11, 60(sp)
  addi sp, sp, 64
  ret

  .size bl_lazy_fpu_register_test, . - bl_lazy_fpu_register_test

  .global bl_lazy_fpu_signal_wait
  .type   bl_lazy_fpu_signal_wait, function

bl_lazy_fpu_signal_wait:
  addi sp, sp, -96
  sw   s0, 0(sp)
  sw   s1, 4(sp)
  sw   s2, 8(sp)
  sw   s3, 12(sp)
  sw   s4, 16(sp)
  sw   s5, 20(sp)
  fsw  fs0, 32(sp)
  fsw  fs1, 36(sp)
  fsw  fs2, 40(sp)
  fsw  fs3, 44(sp)
  fsw  fs4, 48(sp)
  fsw  fs5, 52(sp)
  fsw  fs6, 56(sp)
  fsw  fs7, 60(sp)
  fsw  fs8, 64(sp)
  fsw  fs9, 68(sp)
  fsw  fs10, 72(sp)
  fsw  fs11, 76(sp)

  mv   s0, a0
  mv   s1, a1
  mv   s2, a2
  mv   s3, a3
  mv   s4, a4
  mv   s5, a5
  flw  f0, 0(s0)
  flw  f1, 4(s0)
  flw  f2, 8(s0)
  flw  f3, 12(s0)
  flw  f4, 16(s0)
  flw  f5, 20(s0)
  flw  f6, 24(s0)
  flw  f7, 28(s0)
  flw  f8, 32(s0)
  flw  f9, 36(s0)
  flw  f10, 40(s0)
  flw  f11, 44(s0)
  flw  f12, 48(s0)
  flw  f13, 52(s0)
  flw  f14, 56(s0)
  flw  f15, 60(s0)
  flw  f16, 64(s0)
  flw  f17, 68(s0)
  flw  f18, 72(s0)
  flw  f19, 76(s0)
  flw  f20, 80(s0)
  flw  f21, 84(s0)
  flw  f22, 88(s0)
  flw  f23, 92(s0)
  flw  f24, 96(s0)
  flw  f25, 100(s0)
  flw  f26, 104(s0)
  flw  f27, 108(s0)
  flw  f28, 112(s0)
  flw  f29, 116(s0)
  flw  f30, 120(s0)
  flw  f31, 124(s0)
  lw   t0, 128(s0)
  fscsr t0
  li   t0, 1
  sw   t0, 0(s2)

1:
  lw   t0, 0(s3)
  bnez t0, 2f
  lw   t0, 0(s4)
  bnez t0, 3f
  addi s5, s5, -1
  bnez s5, 1b
  li   a0, 2
  j    4f

2:
  li   a0, 0
  j    4f

3:
  li   a0, 1

4:
  fsw  f0, 0(s1)
  fsw  f1, 4(s1)
  fsw  f2, 8(s1)
  fsw  f3, 12(s1)
  fsw  f4, 16(s1)
  fsw  f5, 20(s1)
  fsw  f6, 24(s1)
  fsw  f7, 28(s1)
  fsw  f8, 32(s1)
  fsw  f9, 36(s1)
  fsw  f10, 40(s1)
  fsw  f11, 44(s1)
  fsw  f12, 48(s1)
  fsw  f13, 52(s1)
  fsw  f14, 56(s1)
  fsw  f15, 60(s1)
  fsw  f16, 64(s1)
  fsw  f17, 68(s1)
  fsw  f18, 72(s1)
  fsw  f19, 76(s1)
  fsw  f20, 80(s1)
  fsw  f21, 84(s1)
  fsw  f22, 88(s1)
  fsw  f23, 92(s1)
  fsw  f24, 96(s1)
  fsw  f25, 100(s1)
  fsw  f26, 104(s1)
  fsw  f27, 108(s1)
  fsw  f28, 112(s1)
  fsw  f29, 116(s1)
  fsw  f30, 120(s1)
  fsw  f31, 124(s1)
  frcsr t0
  sw   t0, 128(s1)

  flw  fs0, 32(sp)
  flw  fs1, 36(sp)
  flw  fs2, 40(sp)
  flw  fs3, 44(sp)
  flw  fs4, 48(sp)
  flw  fs5, 52(sp)
  flw  fs6, 56(sp)
  flw  fs7, 60(sp)
  flw  fs8, 64(sp)
  flw  fs9, 68(sp)
  flw  fs10, 72(sp)
  flw  fs11, 76(sp)
  lw   s0, 0(sp)
  lw   s1, 4(sp)
  lw   s2, 8(sp)
  lw   s3, 12(sp)
  lw   s4, 16(sp)
  lw   s5, 20(sp)
  addi sp, sp, 96
  ret

  .size bl_lazy_fpu_signal_wait, . - bl_lazy_fpu_signal_wait

  .global bl_lazy_fpu_signal_clobber
  .type   bl_lazy_fpu_signal_clobber, function

bl_lazy_fpu_signal_clobber:
  flw  ft0, 0(a0)
  flw  ft1, 4(a0)
  flw  ft2, 8(a0)
  flw  ft3, 12(a0)
  flw  ft4, 16(a0)
  flw  ft5, 20(a0)
  flw  ft6, 24(a0)
  flw  ft7, 28(a0)
  flw  fa0, 32(a0)
  flw  fa1, 36(a0)
  flw  fa2, 40(a0)
  flw  fa3, 44(a0)
  flw  fa4, 48(a0)
  flw  fa5, 52(a0)
  flw  fa6, 56(a0)
  flw  fa7, 60(a0)
  flw  ft8, 64(a0)
  flw  ft9, 68(a0)
  flw  ft10, 72(a0)
  flw  ft11, 76(a0)
  fscsr a1
  ret

  .size bl_lazy_fpu_signal_clobber, . - bl_lazy_fpu_signal_clobber
