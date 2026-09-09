# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BL_OS_FEATURE_TESTS_RTC_IOCTL)
  nuttx_add_kernel_library(bl_rtc_ioctl_test_lower)
  target_sources(bl_rtc_ioctl_test_lower PRIVATE rtc_ioctl_fake.c)
  target_include_directories(bl_rtc_ioctl_test_lower
                             PUBLIC ${CMAKE_CURRENT_LIST_DIR})

  nuttx_add_application(
    NAME
    ${CONFIG_BL_OS_FEATURE_TESTS_RTC_IOCTL_PROGNAME}
    PRIORITY
    ${CONFIG_BL_OS_FEATURE_TESTS_RTC_IOCTL_PRIORITY}
    STACKSIZE
    ${CONFIG_BL_OS_FEATURE_TESTS_RTC_IOCTL_STACKSIZE}
    MODULE
    ${CONFIG_BL_OS_FEATURE_TESTS_RTC_IOCTL}
    INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_LIST_DIR}
    SRCS
    rtc_ioctl_test_main.c)
endif()
