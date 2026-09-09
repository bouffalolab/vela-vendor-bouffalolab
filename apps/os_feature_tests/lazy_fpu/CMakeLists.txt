# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU)
  nuttx_add_application(
    NAME
    lazy_fpu_test
    PRIORITY
    ${CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU_PRIORITY}
    STACKSIZE
    ${CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU_STACKSIZE}
    MODULE
    ${CONFIG_BL_OS_FEATURE_TESTS_LAZY_FPU}
    SRCS
    lazy_fpu_test.c
    lazy_fpu_registers.S)
endif()
