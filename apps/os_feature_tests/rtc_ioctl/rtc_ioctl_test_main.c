/****************************************************************************
 * apps/vendor/bouffalolab/apps/os_feature_tests/rtc_ioctl/rtc_ioctl_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/timers/rtc.h>

#include "rtc_ioctl_test.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_DEBUG_ASSERTIONS
#  define RTC_IOCTL_ASSERTIONS_STATE "on"
#else
#  define RTC_IOCTL_ASSERTIONS_STATE "off"
#endif

#ifdef CONFIG_RTC_ALARM
#  define RTC_IOCTL_ALARM_STATE "on"
#else
#  define RTC_IOCTL_ALARM_STATE "off"
#endif

#ifdef CONFIG_RTC_PERIODIC
#  define RTC_IOCTL_PERIODIC_STATE "on"
#else
#  define RTC_IOCTL_PERIODIC_STATE "off"
#endif

#define RTC_IOCTL_PREFLIGHT_CASES 2
#define RTC_IOCTL_BASE_CASES      11

#ifdef CONFIG_RTC_ALARM
#  if ULONG_MAX > UINT_MAX
#    define RTC_IOCTL_ALARM_CASES 19
#  else
#    define RTC_IOCTL_ALARM_CASES 18
#  endif
#else
#  define RTC_IOCTL_ALARM_CASES   0
#endif

#ifdef CONFIG_RTC_PERIODIC
#  if ULONG_MAX > UINT_MAX
#    define RTC_IOCTL_PERIODIC_CASES 10
#  else
#    define RTC_IOCTL_PERIODIC_CASES 9
#  endif
#else
#  define RTC_IOCTL_PERIODIC_CASES   0
#endif

#ifdef CONFIG_RTC_IOCTL
#  define RTC_IOCTL_PRIVATE_CASES 1
#else
#  define RTC_IOCTL_PRIVATE_CASES 0
#endif

#define RTC_IOCTL_ALL_CASES \
  (RTC_IOCTL_BASE_CASES + RTC_IOCTL_ALARM_CASES + \
   RTC_IOCTL_PERIODIC_CASES + RTC_IOCTL_PRIVATE_CASES)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct rtc_ioctl_test_s
{
  int fd;
  int missing_fd;
  unsigned int cases;
  unsigned int failures;
};

struct rtc_ioctl_input_s
{
  enum bl_rtc_ioctl_test_method_e method;
  union
  {
    struct rtc_time time;
#ifdef CONFIG_RTC_ALARM
    struct rtc_setalarm_s setalarm;
    struct rtc_setrelative_s setrelative;
#endif
#ifdef CONFIG_RTC_PERIODIC
    struct rtc_setperiodic_s setperiodic;
#endif
  } value;
};

#ifdef CONFIG_RTC_ALARM
struct rtc_ioctl_guarded_query_s
{
  uint32_t head;
  struct rtc_rdalarm_s query;
  uint32_t tail;
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool rtc_ioctl_snapshot_equal(
  FAR const struct bl_rtc_ioctl_test_snapshot_s *left,
  FAR const struct bl_rtc_ioctl_test_snapshot_s *right)
{
  return memcmp(left, right, sizeof(*left)) == 0;
}

static bool rtc_ioctl_guards_valid(
  FAR const struct bl_rtc_ioctl_test_snapshot_s *snapshot)
{
  return snapshot->guard_head == BL_RTC_IOCTL_TEST_GUARD_HEAD &&
         snapshot->guard_tail == BL_RTC_IOCTL_TEST_GUARD_TAIL;
}

static void rtc_ioctl_record(FAR struct rtc_ioctl_test_s *test,
                             FAR const char *name, bool pass,
                             int ret, int error)
{
  test->cases++;
  if (pass)
    {
      printf("  PASS: %s\n", name);
    }
  else
    {
      test->failures++;
      printf("  FAIL: %s ret=%d errno=%d\n", name, ret, error);
    }
}

static void rtc_ioctl_expect_invalid(FAR struct rtc_ioctl_test_s *test,
                                     FAR const char *name, int cmd,
                                     unsigned long arg)
{
  struct bl_rtc_ioctl_test_snapshot_s before;
  struct bl_rtc_ioctl_test_snapshot_s after;
  int error;
  int ret;

  bl_rtc_ioctl_test_lower_snapshot(&before);
  errno = 0;
  ret = ioctl(test->fd, cmd, arg);
  error = errno;
  bl_rtc_ioctl_test_lower_snapshot(&after);

  rtc_ioctl_record(test, name,
                   ret == ERROR && error == EINVAL &&
                   rtc_ioctl_snapshot_equal(&before, &after) &&
                   rtc_ioctl_guards_valid(&after),
                   ret, error);
}

static void rtc_ioctl_capture_input(
  FAR struct rtc_ioctl_input_s *input,
  enum bl_rtc_ioctl_test_method_e method, unsigned long arg)
{
  switch (method)
    {
      case BL_RTC_IOCTL_TEST_METHOD_SETTIME:
        input->method = method;
        input->value.time = *(FAR const struct rtc_time *)((uintptr_t)arg);
        break;

#ifdef CONFIG_RTC_ALARM
      case BL_RTC_IOCTL_TEST_METHOD_SETALARM:
        input->method = method;
        input->value.setalarm =
          *(FAR const struct rtc_setalarm_s *)((uintptr_t)arg);
        break;

      case BL_RTC_IOCTL_TEST_METHOD_SETRELATIVE:
        input->method = method;
        input->value.setrelative =
          *(FAR const struct rtc_setrelative_s *)((uintptr_t)arg);
        break;
#endif

#ifdef CONFIG_RTC_PERIODIC
      case BL_RTC_IOCTL_TEST_METHOD_SETPERIODIC:
        input->method = method;
        input->value.setperiodic =
          *(FAR const struct rtc_setperiodic_s *)((uintptr_t)arg);
        break;
#endif

      default:
        break;
    }
}

static bool rtc_ioctl_input_unchanged(
  FAR const struct rtc_ioctl_input_s *input, unsigned long arg)
{
  switch (input->method)
    {
      case BL_RTC_IOCTL_TEST_METHOD_SETTIME:
        return memcmp(&input->value.time,
                      (FAR const void *)((uintptr_t)arg),
                      sizeof(input->value.time)) == 0;

#ifdef CONFIG_RTC_ALARM
      case BL_RTC_IOCTL_TEST_METHOD_SETALARM:
        return memcmp(&input->value.setalarm,
                      (FAR const void *)((uintptr_t)arg),
                      sizeof(input->value.setalarm)) == 0;

      case BL_RTC_IOCTL_TEST_METHOD_SETRELATIVE:
        return memcmp(&input->value.setrelative,
                      (FAR const void *)((uintptr_t)arg),
                      sizeof(input->value.setrelative)) == 0;
#endif

#ifdef CONFIG_RTC_PERIODIC
      case BL_RTC_IOCTL_TEST_METHOD_SETPERIODIC:
        return memcmp(&input->value.setperiodic,
                      (FAR const void *)((uintptr_t)arg),
                      sizeof(input->value.setperiodic)) == 0;
#endif

      default:
        return true;
    }
}

static void rtc_ioctl_expected_call(
  FAR struct bl_rtc_ioctl_test_snapshot_s *expected,
  FAR const struct rtc_ioctl_input_s *input,
  enum bl_rtc_ioctl_test_method_e method, int id, int cmd,
  unsigned long arg)
{
  if (method != BL_RTC_IOCTL_TEST_METHOD_NONE)
    {
      expected->previous_method = expected->last_method;
      expected->last_method = method;
    }

  switch (method)
    {
      case BL_RTC_IOCTL_TEST_METHOD_NONE:
        break;

      case BL_RTC_IOCTL_TEST_METHOD_RDTIME:
        expected->rdtime_calls++;
        break;

      case BL_RTC_IOCTL_TEST_METHOD_SETTIME:
        expected->settime_calls++;
        expected->last_time = input->value.time;
        break;

      case BL_RTC_IOCTL_TEST_METHOD_HAVESETTIME:
        expected->havesettime_calls++;
        break;

#ifdef CONFIG_RTC_ALARM
      case BL_RTC_IOCTL_TEST_METHOD_SETALARM:
        {
          expected->setalarm_calls++;
          expected->last_id = id;
          expected->setalarm_context_valid = 1;
          expected->last_alarm_time = input->value.setalarm.time;
        }
        break;

      case BL_RTC_IOCTL_TEST_METHOD_SETRELATIVE:
        {
          expected->setrelative_calls++;
          expected->last_id = id;
          expected->setrelative_context_valid = 1;
          expected->last_reltime = input->value.setrelative.reltime;
        }
        break;

      case BL_RTC_IOCTL_TEST_METHOD_CANCELALARM:
        expected->cancelalarm_calls++;
        expected->last_id = id;
        break;

      case BL_RTC_IOCTL_TEST_METHOD_RDALARM:
        expected->rdalarm_calls++;
        expected->last_id = id;
        expected->rdalarm_context_valid = 1;
        break;
#endif

#ifdef CONFIG_RTC_PERIODIC
      case BL_RTC_IOCTL_TEST_METHOD_SETPERIODIC:
        {
          expected->setperiodic_calls++;
          expected->last_id = id;
          expected->setperiodic_context_valid = 1;
          expected->last_period = input->value.setperiodic.period;
        }
        break;

      case BL_RTC_IOCTL_TEST_METHOD_CANCELPERIODIC:
        expected->cancelperiodic_calls++;
        expected->last_id = id;
        break;
#endif

#ifdef CONFIG_RTC_IOCTL
      case BL_RTC_IOCTL_TEST_METHOD_IOCTL:
        expected->ioctl_calls++;
        expected->last_cmd = cmd;
        expected->last_arg = arg;
        break;
#endif
    }
}

static void rtc_ioctl_expect_calls(FAR struct rtc_ioctl_test_s *test,
                                   FAR const char *name, int cmd,
                                   unsigned long arg,
                                   enum bl_rtc_ioctl_test_method_e
                                     first_method,
                                   enum bl_rtc_ioctl_test_method_e
                                     second_method,
                                   int expected_ret, int expected_id)
{
  struct bl_rtc_ioctl_test_snapshot_s before;
  struct bl_rtc_ioctl_test_snapshot_s expected;
  struct bl_rtc_ioctl_test_snapshot_s after;
  struct rtc_ioctl_input_s input;
  int error;
  int ret;

  memset(&input, 0, sizeof(input));
  rtc_ioctl_capture_input(&input, first_method, arg);
  rtc_ioctl_capture_input(&input, second_method, arg);
  bl_rtc_ioctl_test_lower_snapshot(&before);
  errno = 0;
  ret = ioctl(test->fd, cmd, arg);
  error = errno;
  bl_rtc_ioctl_test_lower_snapshot(&after);

  expected = before;
  rtc_ioctl_expected_call(&expected, &input, first_method,
                           expected_id, cmd, arg);
  rtc_ioctl_expected_call(&expected, &input, second_method,
                           expected_id, cmd, arg);
  rtc_ioctl_record(test, name,
                   ret == expected_ret &&
                   rtc_ioctl_input_unchanged(&input, arg) &&
                   rtc_ioctl_snapshot_equal(&expected, &after) &&
                   rtc_ioctl_guards_valid(&after),
                   ret, error);
}

static void rtc_ioctl_expect_call(FAR struct rtc_ioctl_test_s *test,
                                  FAR const char *name, int cmd,
                                  unsigned long arg,
                                  enum bl_rtc_ioctl_test_method_e method,
                                  int expected_ret, int expected_id)
{
  rtc_ioctl_expect_calls(test, name, cmd, arg, method,
                         BL_RTC_IOCTL_TEST_METHOD_NONE,
                         expected_ret, expected_id);
}

static void rtc_ioctl_expect_errno(FAR struct rtc_ioctl_test_s *test,
                                   FAR const char *name, int fd, int cmd,
                                   unsigned long arg, int expected)
{
  int error;
  int ret;

  errno = 0;
  ret = ioctl(fd, cmd, arg);
  error = errno;
  rtc_ioctl_record(test, name,
                   ret == ERROR && error == expected,
                   ret, error);
}

static void rtc_ioctl_run_preflight(FAR struct rtc_ioctl_test_s *test)
{
  rtc_ioctl_expect_invalid(test, "RTC-PRE-001 RD_TIME NULL",
                           RTC_RD_TIME, 0);
  rtc_ioctl_expect_invalid(test, "RTC-PRE-002 SET_TIME NULL",
                           RTC_SET_TIME, 0);
}

static void rtc_ioctl_run_initialize(FAR struct rtc_ioctl_test_s *test)
{
  static const char *const invalid_names[] = {
    "RTC-INIT-001 NULL lower",
    "RTC-INIT-002 NULL ops",
    "RTC-INIT-003 minor -1",
    "RTC-INIT-004 minor 1000",
    "RTC-INIT-005 minor INT_MIN",
    "RTC-INIT-006 minor INT_MAX",
  };

  static const char *const sentinel_names[] = {
    "RTC-INIT-101 NULL lower sentinel",
    "RTC-INIT-102 NULL ops sentinel",
    "RTC-INIT-103 minor -1 sentinel",
    "RTC-INIT-104 minor 1000 sentinel",
    "RTC-INIT-105 minor INT_MIN sentinel",
    "RTC-INIT-106 minor INT_MAX sentinel",
  };

  struct bl_rtc_initialize_test_result_s result;
  int ret;
  int i;

  ret = bl_rtc_initialize_test_run(&result);
  if (ret < 0)
    {
      test->failures++;
      printf("  FAIL: RTC initialize harness ret=%d\n", ret);
      return;
    }

  for (i = 0; i < BL_RTC_INITIALIZE_TEST_INVALID_CASES; i++)
    {
      rtc_ioctl_record(test, invalid_names[i],
                       result.invalid_ret[i] == -EINVAL,
                       result.invalid_ret[i], 0);
    }

  for (i = 0; i < BL_RTC_INITIALIZE_TEST_INVALID_CASES; i++)
    {
      rtc_ioctl_record(test, sentinel_names[i],
                       result.invalid_sentinel_ok[i],
                       result.invalid_sentinel_ok[i], 0);
    }

  rtc_ioctl_record(test, "RTC-INIT-107 invalid lower not destroyed",
                   result.invalid_destroy_calls == 0,
                   result.invalid_destroy_calls, 0);
  rtc_ioctl_record(test, "RTC-INIT-201 valid register",
                   result.register_ret == OK,
                   result.register_ret, 0);
  rtc_ioctl_record(test, "RTC-INIT-202 first open",
                   result.open1_ret >= 0,
                   result.open1_ret, result.open1_errno);
  rtc_ioctl_record(test, "RTC-INIT-203 second open",
                   result.open2_ret >= 0,
                   result.open2_ret, result.open2_errno);
  rtc_ioctl_record(test, "RTC-INIT-204 owner ioctl",
                   result.read_ret == OK,
                   result.read_ret, result.read_errno);
  rtc_ioctl_record(test, "RTC-INIT-205 duplicate registration",
                   result.duplicate_ret == -EEXIST &&
                     result.duplicate_all_eexist,
                   result.duplicate_ret, 0);
  rtc_ioctl_record(test, "RTC-INIT-206 challenger not destroyed",
                   result.challenger_destroy_calls == 0,
                   result.challenger_destroy_calls, 0);
  rtc_ioctl_record(test, "RTC-INIT-207 owner preserved after conflict",
                   result.owner_after_conflict_ret == OK,
                   result.owner_after_conflict_ret,
                   result.owner_after_conflict_errno);
  rtc_ioctl_record(test, "RTC-INIT-208 conflict loop releases upper",
                   result.heap_before_used == result.heap_after_used &&
                     result.heap_before_allocs == result.heap_after_allocs,
                   0, 0);
  rtc_ioctl_record(test, "RTC-INIT-209 unlink owner",
                   result.unlink_ret == OK,
                   result.unlink_ret, result.unlink_errno);
  rtc_ioctl_record(test, "RTC-INIT-210 new open after unlink",
                   result.open_after_unlink_ret == ERROR &&
                     result.open_after_unlink_errno == ENOENT,
                   result.open_after_unlink_ret,
                   result.open_after_unlink_errno);
  rtc_ioctl_record(test, "RTC-INIT-211 old fd after unlink",
                   result.old_fd_ret == OK,
                   result.old_fd_ret, result.old_fd_errno);
  rtc_ioctl_record(test, "RTC-INIT-212 close non-final fd",
                   result.close1_ret == OK,
                   result.close1_ret, result.close1_errno);
  rtc_ioctl_record(test, "RTC-INIT-213 non-final close keeps lower",
                   result.destroy_after_close1 == 0,
                   result.destroy_after_close1, 0);
  rtc_ioctl_record(test, "RTC-INIT-214 close final fd",
                   result.close2_ret == OK,
                   result.close2_ret, result.close2_errno);
  rtc_ioctl_record(test, "RTC-INIT-215 final close destroys lower",
                   result.destroy_after_close2 == 1,
                   result.destroy_after_close2, 0);
  rtc_ioctl_record(test, "RTC-INIT-301 minor 999 register",
                   result.boundary_register_ret == OK,
                   result.boundary_register_ret, 0);
  rtc_ioctl_record(test, "RTC-INIT-302 no-open unlink",
                   result.boundary_unlink_ret == OK,
                   result.boundary_unlink_ret,
                   result.boundary_unlink_errno);
  rtc_ioctl_record(test, "RTC-INIT-303 no-open unlink destroys lower",
                   result.boundary_destroy_calls == 1,
                   result.boundary_destroy_calls, 0);
  rtc_ioctl_record(test, "RTC-INIT-401 empty ops register",
                   result.empty_register_ret == OK,
                   result.empty_register_ret, 0);
  rtc_ioctl_record(test, "RTC-INIT-402 empty ops open",
                   result.empty_open_ret >= 0,
                   result.empty_open_ret, result.empty_open_errno);
  rtc_ioctl_record(test, "RTC-INIT-403 empty method returns ENOSYS",
                   result.empty_ioctl_ret == ERROR &&
                     result.empty_ioctl_errno == ENOSYS,
                   result.empty_ioctl_ret, result.empty_ioctl_errno);
  rtc_ioctl_record(test, "RTC-INIT-404 empty ops close",
                   result.empty_close_ret == OK,
                   result.empty_close_ret, result.empty_close_errno);
  rtc_ioctl_record(test, "RTC-INIT-405 empty ops unlink",
                   result.empty_unlink_ret == OK,
                   result.empty_unlink_ret, result.empty_unlink_errno);
  rtc_ioctl_record(test, "RTC-INIT-406 empty ops releases upper",
                   result.empty_heap_before_used ==
                     result.empty_heap_after_used &&
                     result.empty_heap_before_allocs ==
                       result.empty_heap_after_allocs,
                   0, 0);
}

static void rtc_ioctl_run_base(FAR struct rtc_ioctl_test_s *test)
{
  struct rtc_time set_time;
  struct rtc_time read_time;
  bool have_set_time = false;

  rtc_ioctl_expect_invalid(test, "RTC-VAL-001 HAVE_SET_TIME NULL",
                           RTC_HAVE_SET_TIME, 0);

  memset(&set_time, 0, sizeof(set_time));
  set_time.tm_year = 126;
  set_time.tm_mon = 7;
  set_time.tm_mday = 30;
  set_time.tm_hour = 5;
  set_time.tm_min = 17;
  set_time.tm_sec = 23;

  rtc_ioctl_expect_call(test, "RTC-VAL-002 SET_TIME valid", RTC_SET_TIME,
                        (unsigned long)(uintptr_t)&set_time,
                        BL_RTC_IOCTL_TEST_METHOD_SETTIME, OK, -1);
  rtc_ioctl_expect_call(test, "RTC-VAL-003 HAVE_SET_TIME valid",
                        RTC_HAVE_SET_TIME,
                        (unsigned long)(uintptr_t)&have_set_time,
                        BL_RTC_IOCTL_TEST_METHOD_HAVESETTIME, OK, -1);
  rtc_ioctl_record(test, "RTC-VAL-004 HAVE_SET_TIME result",
                   have_set_time, have_set_time, 0);

  memset(&read_time, 0, sizeof(read_time));
  rtc_ioctl_expect_call(test, "RTC-VAL-005 RD_TIME valid", RTC_RD_TIME,
                        (unsigned long)(uintptr_t)&read_time,
                        BL_RTC_IOCTL_TEST_METHOD_RDTIME, OK, -1);
  rtc_ioctl_record(test, "RTC-VAL-006 RD_TIME result",
                   memcmp(&set_time, &read_time, sizeof(set_time)) == 0,
                   0, 0);
}

static void rtc_ioctl_run_missing(FAR struct rtc_ioctl_test_s *test)
{
  struct rtc_time rtc_time;
  bool have_set_time;

  memset(&rtc_time, 0, sizeof(rtc_time));
  rtc_ioctl_expect_errno(test, "RTC-VAL-007 RD_TIME missing method",
                         test->missing_fd, RTC_RD_TIME,
                         (unsigned long)(uintptr_t)&rtc_time, ENOSYS);
  rtc_ioctl_expect_errno(test, "RTC-VAL-008 SET_TIME missing method",
                         test->missing_fd, RTC_SET_TIME,
                         (unsigned long)(uintptr_t)&rtc_time, ENOSYS);
  rtc_ioctl_expect_errno(test, "RTC-VAL-009 HAVE_SET_TIME missing method",
                         test->missing_fd, RTC_HAVE_SET_TIME,
                         (unsigned long)(uintptr_t)&have_set_time, ENOSYS);

#ifdef CONFIG_RTC_ALARM
  struct rtc_setalarm_s setalarm;
  struct rtc_setrelative_s setrelative;
  struct rtc_rdalarm_s rdalarm;

  memset(&setalarm, 0, sizeof(setalarm));
  memset(&setrelative, 0, sizeof(setrelative));
  memset(&rdalarm, 0, sizeof(rdalarm));
  rtc_ioctl_expect_errno(test, "RTC-VAL-010 SET_ALARM missing method",
                         test->missing_fd, RTC_SET_ALARM,
                         (unsigned long)(uintptr_t)&setalarm, ENOSYS);
  rtc_ioctl_expect_errno(test, "RTC-VAL-011 SET_RELATIVE missing method",
                         test->missing_fd, RTC_SET_RELATIVE,
                         (unsigned long)(uintptr_t)&setrelative, ENOSYS);
  rtc_ioctl_expect_errno(test, "RTC-VAL-012 CANCEL_ALARM missing method",
                         test->missing_fd, RTC_CANCEL_ALARM, 0, ENOSYS);
  rtc_ioctl_expect_errno(test, "RTC-VAL-013 RD_ALARM missing method",
                         test->missing_fd, RTC_RD_ALARM,
                         (unsigned long)(uintptr_t)&rdalarm, ENOSYS);
#endif

#ifdef CONFIG_RTC_PERIODIC
  struct rtc_setperiodic_s periodic;

  memset(&periodic, 0, sizeof(periodic));
  rtc_ioctl_expect_errno(test, "RTC-VAL-014 SET_PERIODIC missing method",
                         test->missing_fd, RTC_SET_PERIODIC,
                         (unsigned long)(uintptr_t)&periodic, ENOSYS);
  rtc_ioctl_expect_errno(test,
                         "RTC-VAL-015 CANCEL_PERIODIC missing method",
                         test->missing_fd, RTC_CANCEL_PERIODIC, 0, ENOSYS);
#endif
}

#ifdef CONFIG_RTC_ALARM
static void rtc_ioctl_run_alarm(FAR struct rtc_ioctl_test_s *test)
{
  struct rtc_setalarm_s setalarm;
  struct rtc_setrelative_s setrelative;
  struct rtc_ioctl_guarded_query_s guarded_query;
  struct rtc_rdalarm_s query_before;

  memset(&setalarm, 0, sizeof(setalarm));
  setalarm.id = 0;
  setalarm.time.tm_year = 126;
  setalarm.time.tm_mon = 7;
  setalarm.time.tm_mday = 30;
  setalarm.time.tm_hour = 6;
  rtc_ioctl_expect_call(test, "RTC-VAL-101 SET_ALARM seed", RTC_SET_ALARM,
                        (unsigned long)(uintptr_t)&setalarm,
                        BL_RTC_IOCTL_TEST_METHOD_SETALARM, OK, 0);

  rtc_ioctl_expect_invalid(test, "RTC-VAL-102 SET_ALARM NULL",
                           RTC_SET_ALARM, 0);
  rtc_ioctl_expect_invalid(test, "RTC-VAL-103 SET_RELATIVE NULL",
                           RTC_SET_RELATIVE, 0);
  rtc_ioctl_expect_invalid(test, "RTC-VAL-104 RD_ALARM NULL",
                           RTC_RD_ALARM, 0);

  setalarm.id = CONFIG_RTC_NALARMS;
  rtc_ioctl_expect_invalid(test, "RTC-VAL-105 SET_ALARM high ID",
                           RTC_SET_ALARM,
                           (unsigned long)(uintptr_t)&setalarm);

  memset(&setrelative, 0, sizeof(setrelative));
  setrelative.id = CONFIG_RTC_NALARMS;
  rtc_ioctl_expect_invalid(test, "RTC-VAL-106 SET_RELATIVE high ID",
                           RTC_SET_RELATIVE,
                           (unsigned long)(uintptr_t)&setrelative);

  rtc_ioctl_expect_invalid(test, "RTC-VAL-107 CANCEL_ALARM negative ID",
                           RTC_CANCEL_ALARM, (unsigned long)-1);
  rtc_ioctl_expect_invalid(test, "RTC-VAL-108 CANCEL_ALARM high ID",
                           RTC_CANCEL_ALARM, CONFIG_RTC_NALARMS);
#if ULONG_MAX > UINT_MAX
  rtc_ioctl_expect_invalid(test, "RTC-VAL-108A CANCEL_ALARM wrapped ID",
                           RTC_CANCEL_ALARM,
                           (unsigned long)UINT_MAX + 1);
#endif

  memset(&guarded_query, 0xa5, sizeof(guarded_query));
  guarded_query.head = BL_RTC_IOCTL_TEST_GUARD_HEAD;
  guarded_query.query.id = CONFIG_RTC_NALARMS;
  guarded_query.tail = BL_RTC_IOCTL_TEST_GUARD_TAIL;
  query_before = guarded_query.query;
  rtc_ioctl_expect_invalid(test, "RTC-VAL-109 RD_ALARM high ID",
                           RTC_RD_ALARM,
                           (unsigned long)(uintptr_t)&guarded_query.query);
  rtc_ioctl_record(test, "RTC-VAL-110 RD_ALARM output guards",
                   guarded_query.head == BL_RTC_IOCTL_TEST_GUARD_HEAD &&
                   guarded_query.tail == BL_RTC_IOCTL_TEST_GUARD_TAIL &&
                   memcmp(&query_before, &guarded_query.query,
                          sizeof(query_before)) == 0,
                   0, 0);

  memset(&guarded_query, 0, sizeof(guarded_query));
  guarded_query.query.id = 0;
  rtc_ioctl_expect_call(test, "RTC-VAL-111 RD_ALARM after invalid inputs",
                        RTC_RD_ALARM,
                        (unsigned long)(uintptr_t)&guarded_query.query,
                        BL_RTC_IOCTL_TEST_METHOD_RDALARM, OK, 0);
  rtc_ioctl_record(test, "RTC-VAL-112 alarm state preserved",
                   guarded_query.query.active &&
                   memcmp(&setalarm.time, &guarded_query.query.time,
                          sizeof(setalarm.time)) == 0,
                   guarded_query.query.active, 0);

  memset(&setrelative, 0, sizeof(setrelative));
  setrelative.id = 0;
  setrelative.reltime = 1;
  rtc_ioctl_expect_calls(test, "RTC-VAL-113 SET_RELATIVE replaces alarm",
                         RTC_SET_RELATIVE,
                         (unsigned long)(uintptr_t)&setrelative,
                         BL_RTC_IOCTL_TEST_METHOD_CANCELALARM,
                         BL_RTC_IOCTL_TEST_METHOD_SETRELATIVE, OK, 0);
  rtc_ioctl_expect_call(test, "RTC-VAL-114 CANCEL_ALARM valid",
                        RTC_CANCEL_ALARM, 0,
                        BL_RTC_IOCTL_TEST_METHOD_CANCELALARM, OK, 0);
}
#endif

#ifdef CONFIG_RTC_PERIODIC
static void rtc_ioctl_run_periodic(FAR struct rtc_ioctl_test_s *test)
{
  struct rtc_setperiodic_s periodic;

  memset(&periodic, 0, sizeof(periodic));
  periodic.id = 0;
  periodic.period.tv_sec = 1;
  rtc_ioctl_expect_call(test, "RTC-VAL-201 SET_PERIODIC seed",
                        RTC_SET_PERIODIC,
                        (unsigned long)(uintptr_t)&periodic,
                        BL_RTC_IOCTL_TEST_METHOD_SETPERIODIC, OK, 0);

  rtc_ioctl_expect_invalid(test, "RTC-VAL-202 SET_PERIODIC NULL",
                           RTC_SET_PERIODIC, 0);

  periodic.id = 1;
  rtc_ioctl_expect_invalid(test, "RTC-VAL-203 SET_PERIODIC nonzero ID",
                           RTC_SET_PERIODIC,
                           (unsigned long)(uintptr_t)&periodic);
  rtc_ioctl_expect_invalid(test, "RTC-VAL-204 CANCEL_PERIODIC negative ID",
                           RTC_CANCEL_PERIODIC, (unsigned long)-1);
  rtc_ioctl_expect_invalid(test, "RTC-VAL-205 CANCEL_PERIODIC positive ID",
                           RTC_CANCEL_PERIODIC, 1);
#if ULONG_MAX > UINT_MAX
  rtc_ioctl_expect_invalid(test, "RTC-VAL-205A CANCEL_PERIODIC wrapped ID",
                           RTC_CANCEL_PERIODIC,
                           (unsigned long)UINT_MAX + 1);
#endif

  periodic.id = 0;
  periodic.period.tv_sec = 2;
  rtc_ioctl_expect_calls(test, "RTC-VAL-206 SET_PERIODIC replaces active",
                         RTC_SET_PERIODIC,
                         (unsigned long)(uintptr_t)&periodic,
                         BL_RTC_IOCTL_TEST_METHOD_CANCELPERIODIC,
                         BL_RTC_IOCTL_TEST_METHOD_SETPERIODIC, OK, 0);
  rtc_ioctl_expect_call(test, "RTC-VAL-207 CANCEL_PERIODIC valid",
                        RTC_CANCEL_PERIODIC, 0,
                        BL_RTC_IOCTL_TEST_METHOD_CANCELPERIODIC, OK, 0);
}
#endif

#ifdef CONFIG_RTC_IOCTL
static void rtc_ioctl_run_private(FAR struct rtc_ioctl_test_s *test)
{
  const int cmd = _RTCIOC(RTC_USER_IOCBASE);
  const unsigned long arg = 0x12345678;

  rtc_ioctl_expect_call(test, "RTC-VAL-301 private ioctl forwarding",
                        cmd, arg, BL_RTC_IOCTL_TEST_METHOD_IOCTL,
                        BL_RTC_IOCTL_TEST_PRIVATE_RESULT, -1);
}
#endif

static int rtc_ioctl_cleanup(FAR struct rtc_ioctl_test_s *test)
{
  struct bl_rtc_ioctl_test_snapshot_s snapshot;
  int ret = OK;

  if (test->fd >= 0 && close(test->fd) < 0)
    {
      printf("  FAIL: close fake RTC errno=%d\n", errno);
      ret = ERROR;
    }

  if (test->missing_fd >= 0 && close(test->missing_fd) < 0)
    {
      printf("  FAIL: close missing-method RTC errno=%d\n", errno);
      ret = ERROR;
    }

  if (unlink(BL_RTC_IOCTL_TEST_DEVPATH) < 0)
    {
      printf("  FAIL: unlink fake RTC errno=%d\n", errno);
      ret = ERROR;
    }

  if (unlink(BL_RTC_IOCTL_TEST_MISSING_DEVPATH) < 0)
    {
      printf("  FAIL: unlink missing-method RTC errno=%d\n", errno);
      ret = ERROR;
    }

  bl_rtc_ioctl_test_lower_snapshot(&snapshot);
  if (snapshot.destroy_calls != 1 || !rtc_ioctl_guards_valid(&snapshot))
    {
      printf("  FAIL: fake RTC destroy=%lu guards=%d\n",
             (unsigned long)snapshot.destroy_calls,
             rtc_ioctl_guards_valid(&snapshot));
      ret = ERROR;
    }

  return ret;
}

static void rtc_ioctl_usage(FAR const char *progname)
{
  printf("Usage: %s [-c preflight|initialize|all]\n", progname);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  struct rtc_ioctl_test_s test =
  {
    .fd = -1,
    .missing_fd = -1,
  };

  bool initialize_only = false;
  bool preflight_only = false;
  unsigned int expected_cases;
  int ret;

  if (argc == 3 && strcmp(argv[1], "-c") == 0)
    {
      if (strcmp(argv[2], "preflight") == 0)
        {
          preflight_only = true;
        }
      else if (strcmp(argv[2], "initialize") == 0)
        {
          initialize_only = true;
        }
      else if (strcmp(argv[2], "all") != 0)
        {
          rtc_ioctl_usage(argv[0]);
          return EXIT_FAILURE;
        }
    }
  else if (argc != 1)
    {
      rtc_ioctl_usage(argv[0]);
      return EXIT_FAILURE;
    }

  if (initialize_only)
    {
      printf("RTC initialize validation: assertions=%s\n",
             RTC_IOCTL_ASSERTIONS_STATE);
      rtc_ioctl_run_initialize(&test);
      if (test.cases != BL_RTC_INITIALIZE_TEST_CASES)
        {
          test.failures++;
          printf("  FAIL: case count actual=%u expected=%u\n",
                 test.cases, BL_RTC_INITIALIZE_TEST_CASES);
        }

      printf("RTC initialize validation: cases=%u failures=%u result=%s\n",
             test.cases, test.failures,
             test.failures == 0 ? "PASS" : "FAIL");
      return test.failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  ret = bl_rtc_ioctl_test_lower_register();
  if (ret < 0)
    {
      printf("FAIL: fake RTC registration ret=%d\n", ret);
      return EXIT_FAILURE;
    }

  test.fd = open(BL_RTC_IOCTL_TEST_DEVPATH, O_RDWR);
  test.missing_fd = open(BL_RTC_IOCTL_TEST_MISSING_DEVPATH, O_RDWR);
  if (test.fd < 0 || test.missing_fd < 0)
    {
      printf("FAIL: fake RTC open fd=%d missing_fd=%d errno=%d\n",
             test.fd, test.missing_fd, errno);
      rtc_ioctl_cleanup(&test);
      return EXIT_FAILURE;
    }

  printf("RTC ioctl validation: assertions=%s alarm=%s periodic=%s\n",
         RTC_IOCTL_ASSERTIONS_STATE, RTC_IOCTL_ALARM_STATE,
         RTC_IOCTL_PERIODIC_STATE);

  rtc_ioctl_run_preflight(&test);
  if (!preflight_only)
    {
      rtc_ioctl_run_base(&test);
      rtc_ioctl_run_missing(&test);
#ifdef CONFIG_RTC_ALARM
      rtc_ioctl_run_alarm(&test);
#endif
#ifdef CONFIG_RTC_PERIODIC
      rtc_ioctl_run_periodic(&test);
#endif
#ifdef CONFIG_RTC_IOCTL
      rtc_ioctl_run_private(&test);
#endif
    }

  expected_cases = preflight_only ? RTC_IOCTL_PREFLIGHT_CASES :
                                    RTC_IOCTL_ALL_CASES;
  if (test.cases != expected_cases)
    {
      test.failures++;
      printf("  FAIL: case count actual=%u expected=%u\n",
             test.cases, expected_cases);
    }

  if (rtc_ioctl_cleanup(&test) < 0)
    {
      test.failures++;
    }

  printf("RTC ioctl validation: cases=%u failures=%u result=%s\n",
         test.cases, test.failures, test.failures == 0 ? "PASS" : "FAIL");
  return test.failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
