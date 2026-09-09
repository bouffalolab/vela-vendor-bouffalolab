# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BL_OS_FEATURE_TESTS_UBSAN)
  nuttx_add_application(
    NAME
    ubsan_test
    PRIORITY
    ${CONFIG_BL_OS_FEATURE_TESTS_UBSAN_PRIORITY}
    STACKSIZE
    ${CONFIG_BL_OS_FEATURE_TESTS_UBSAN_STACKSIZE}
    MODULE
    ${CONFIG_BL_OS_FEATURE_TESTS_UBSAN}
    COMPILE_FLAGS
    -fsanitize=signed-integer-overflow,shift
    SRCS
    ubsan_test.c)
endif()
