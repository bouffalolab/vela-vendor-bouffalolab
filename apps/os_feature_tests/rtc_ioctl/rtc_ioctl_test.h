/****************************************************************************
 * apps/vendor/bouffalolab/apps/os_feature_tests/rtc_ioctl/rtc_ioctl_test.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#ifndef __VENDOR_BOUFFALOLAB_APPS_OS_FEATURE_TESTS_RTC_IOCTL_TEST_H
#define __VENDOR_BOUFFALOLAB_APPS_OS_FEATURE_TESTS_RTC_IOCTL_TEST_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nuttx/timers/rtc.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL_RTC_IOCTL_TEST_DEVPATH         "/dev/rtc99"
#define BL_RTC_IOCTL_TEST_MISSING_DEVPATH "/dev/rtc98"
#define BL_RTC_IOCTL_TEST_GUARD_HEAD      UINT32_C(0x13579bdf)
#define BL_RTC_IOCTL_TEST_GUARD_TAIL      UINT32_C(0x2468ace0)
#define BL_RTC_IOCTL_TEST_PRIVATE_RESULT  77
#define BL_RTC_INITIALIZE_TEST_INVALID_CASES 6
#define BL_RTC_INITIALIZE_TEST_CASES         37

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bl_rtc_ioctl_test_method_e
{
  BL_RTC_IOCTL_TEST_METHOD_NONE,
  BL_RTC_IOCTL_TEST_METHOD_RDTIME,
  BL_RTC_IOCTL_TEST_METHOD_SETTIME,
  BL_RTC_IOCTL_TEST_METHOD_HAVESETTIME,
#ifdef CONFIG_RTC_ALARM
  BL_RTC_IOCTL_TEST_METHOD_SETALARM,
  BL_RTC_IOCTL_TEST_METHOD_SETRELATIVE,
  BL_RTC_IOCTL_TEST_METHOD_CANCELALARM,
  BL_RTC_IOCTL_TEST_METHOD_RDALARM,
#endif
#ifdef CONFIG_RTC_PERIODIC
  BL_RTC_IOCTL_TEST_METHOD_SETPERIODIC,
  BL_RTC_IOCTL_TEST_METHOD_CANCELPERIODIC,
#endif
#ifdef CONFIG_RTC_IOCTL
  BL_RTC_IOCTL_TEST_METHOD_IOCTL,
#endif
};

struct bl_rtc_ioctl_test_snapshot_s
{
  uint32_t guard_head;
  uint32_t rdtime_calls;
  uint32_t settime_calls;
  uint32_t havesettime_calls;
  struct rtc_time last_time;
#ifdef CONFIG_RTC_ALARM
  uint32_t setalarm_calls;
  uint32_t setrelative_calls;
  uint32_t cancelalarm_calls;
  uint32_t rdalarm_calls;
  uint32_t setalarm_context_valid;
  uint32_t setrelative_context_valid;
  uint32_t rdalarm_context_valid;
  struct rtc_time last_alarm_time;
  time_t last_reltime;
#endif
#ifdef CONFIG_RTC_PERIODIC
  uint32_t setperiodic_calls;
  uint32_t cancelperiodic_calls;
  uint32_t setperiodic_context_valid;
  struct timespec last_period;
#endif
#ifdef CONFIG_RTC_IOCTL
  uint32_t ioctl_calls;
#endif
  uint32_t destroy_calls;
  uint32_t null_argument_calls;
  enum bl_rtc_ioctl_test_method_e previous_method;
  enum bl_rtc_ioctl_test_method_e last_method;
  int last_id;
  int last_cmd;
  unsigned long last_arg;
  uint32_t guard_tail;
};

struct bl_rtc_initialize_test_result_s
{
  int invalid_ret[BL_RTC_INITIALIZE_TEST_INVALID_CASES];
  bool invalid_sentinel_ok[BL_RTC_INITIALIZE_TEST_INVALID_CASES];
  uint32_t invalid_destroy_calls;
  int register_ret;
  int open1_ret;
  int open1_errno;
  int open2_ret;
  int open2_errno;
  int read_ret;
  int read_errno;
  int duplicate_ret;
  bool duplicate_all_eexist;
  uint32_t challenger_destroy_calls;
  int owner_after_conflict_ret;
  int owner_after_conflict_errno;
  int unlink_ret;
  int unlink_errno;
  int open_after_unlink_ret;
  int open_after_unlink_errno;
  int old_fd_ret;
  int old_fd_errno;
  int close1_ret;
  int close1_errno;
  uint32_t destroy_after_close1;
  int close2_ret;
  int close2_errno;
  uint32_t destroy_after_close2;
  size_t heap_before_used;
  size_t heap_before_allocs;
  size_t heap_after_used;
  size_t heap_after_allocs;
  int boundary_register_ret;
  int boundary_unlink_ret;
  int boundary_unlink_errno;
  uint32_t boundary_destroy_calls;
  int empty_register_ret;
  int empty_open_ret;
  int empty_open_errno;
  int empty_ioctl_ret;
  int empty_ioctl_errno;
  int empty_close_ret;
  int empty_close_errno;
  int empty_unlink_ret;
  int empty_unlink_errno;
  size_t empty_heap_before_used;
  size_t empty_heap_before_allocs;
  size_t empty_heap_after_used;
  size_t empty_heap_after_allocs;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bl_rtc_ioctl_test_lower_register(void);
void bl_rtc_ioctl_test_lower_reset(void);
int bl_rtc_ioctl_test_lower_snapshot(
  struct bl_rtc_ioctl_test_snapshot_s *snapshot);
int bl_rtc_initialize_test_run(
  struct bl_rtc_initialize_test_result_s *result);

#endif
