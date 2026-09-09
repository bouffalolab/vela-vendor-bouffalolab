# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BL_OS_FEATURE_TESTS_CACHE)
  nuttx_add_application(
    NAME
    cache_test
    PRIORITY
    ${CONFIG_BL_OS_FEATURE_TESTS_CACHE_PRIORITY}
    STACKSIZE
    ${CONFIG_BL_OS_FEATURE_TESTS_CACHE_STACKSIZE}
    MODULE
    ${CONFIG_BL_OS_FEATURE_TESTS_CACHE}
    SRCS
    cache_test.c
    cache_test_trampoline.S)
endif()
