# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD)
  set(mm_record_sources mm_record_test.c)
  set(mm_record_includes)

  if(CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD_STACK)
    list(APPEND mm_record_sources mm_record_stack_test.c)
    list(APPEND mm_record_includes ${NUTTX_DIR}/mm/mm_heap)
  endif()

  if(CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD_REALLOC_STACK)
    list(APPEND mm_record_sources mm_realloc_stack_test.c)
    list(APPEND mm_record_includes ${NUTTX_DIR}/mm/mm_heap)
  endif()

  nuttx_add_application(
    NAME
    ${CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD_PROGNAME}
    PRIORITY
    ${CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD_PRIORITY}
    STACKSIZE
    ${CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD_STACKSIZE}
    MODULE
    ${CONFIG_BL_OS_FEATURE_TESTS_MM_RECORD}
    INCLUDE_DIRECTORIES
    ${mm_record_includes}
    SRCS
    ${mm_record_sources})
endif()
