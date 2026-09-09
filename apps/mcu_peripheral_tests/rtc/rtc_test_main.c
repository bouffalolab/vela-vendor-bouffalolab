/****************************************************************************
 * apps/vendor/bouffalolab/apps/mcu_peripheral_tests/rtc/rtc_test_main.c
 *
 * MCU Peripheral RTC Test Cases (RTC-001 ~ RTC-005)
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/timers/rtc.h>

#include "bl616cl_rtc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DEFAULT_RTC_DEVPATH       "/dev/rtc0"

#define CASE_001                  "001"
#define CASE_002                  "002"
#define CASE_003                  "003"
#define CASE_004                  "004"
#define CASE_005                  "005"
#define CASE_ALL                  "all"

#define RTC_TEST_SIGNO            SIGUSR1
#define ALARM_SECONDS             2
#define ALARM_CANCEL_WAIT_SECONDS 3
#define ALARM_DUPLICATE_WAIT_MS   200
#define ALARM_EARLY_TOLERANCE_MS  200
#define ALARM_LATE_TOLERANCE_MS   300
#define ALARM_QUERY_TOLERANCE_MS  200
#define CLOCK_MATCH_TOLERANCE_NS  200000000LL
#define COUNTER_SAMPLE_SECONDS    5
#define FRESH_BOOT_MAX_SECONDS    30
#define SUBSECOND_SAMPLE_US       1000
#define SUBSECOND_TIMEOUT_MS      2000
#define RECEIVER_READY_TIMEOUT_S  2
#define RTC_READBACK_TOLERANCE_NS 200000000LL
#define RTC_TEST_SKIPPED          1

#ifdef CONFIG_BL616CL_RTC_CLOCK_DIG32K
#define CLOCK_MAX_ERROR_PPM 1000
#else
#define CLOCK_MAX_ERROR_PPM 20000
#endif

#define RTC_COUNTER_MASK          UINT64_C(0x0000ffffffffffff)

#define ALARM_VALUE_RELATIVE      0x101
#define ALARM_VALUE_REARM         0x102
#define ALARM_VALUE_ABSOLUTE      0x103
#define ALARM_VALUE_REPLACE       0x201
#define ALARM_VALUE_SETTIME_REARM 0x202
#define ALARM_VALUE_SETTIME_FIRE  0x203
#define ALARM_VALUE_SHORT         0x204
#define ALARM_VALUE_UNLINK        0x301

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct app_config_s
{
  const char *devpath;
  const char *case_id;
  bool verbose;
};

#ifdef CONFIG_BL616CL_RTC_ALARM
struct alarm_receiver_s
{
  sem_t ready;
  sigset_t set;
  struct timespec received_time;
  pid_t pid;
  int signal;
  int value;
  int error;
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void print_usage(const char *progname)
{
  printf("Usage: %s [options]\n", progname);
  printf("  -c <id>   Case: 001,002,003,004,005,all (default: all)\n");
  printf("              all excludes destructive case 005\n");
  printf("  -d <dev>  RTC device (default: %s)\n", DEFAULT_RTC_DEVPATH);
  printf("  -v        Verbose output\n");
  printf("  -h        Show this help\n");
}

static bool case_is_supported(const char *case_id)
{
  return strcmp(case_id, CASE_001) == 0 ||
         strcmp(case_id, CASE_002) == 0 ||
         strcmp(case_id, CASE_003) == 0 ||
         strcmp(case_id, CASE_004) == 0 ||
         strcmp(case_id, CASE_005) == 0 ||
         strcmp(case_id, CASE_ALL) == 0;
}

static int parse_args(int argc, char *argv[], struct app_config_s *cfg)
{
  int i;

  cfg->devpath = DEFAULT_RTC_DEVPATH;
  cfg->case_id = CASE_ALL;
  cfg->verbose = false;

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
          print_usage(argv[0]);
          return 1;
        }

      if (strcmp(argv[i], "-v") == 0)
        {
          cfg->verbose = true;
          continue;
        }

      if (i + 1 >= argc)
        {
          printf("ERROR: missing value for %s\n", argv[i]);
          return -EINVAL;
        }

      if (strcmp(argv[i], "-c") == 0)
        {
          cfg->case_id = argv[++i];
        }
      else if (strcmp(argv[i], "-d") == 0)
        {
          cfg->devpath = argv[++i];
        }
      else
        {
          printf("ERROR: unknown option %s\n", argv[i]);
          return -EINVAL;
        }
    }

  if (!case_is_supported(cfg->case_id))
    {
      printf("ERROR: unsupported case '%s'\n", cfg->case_id);
      return -EINVAL;
    }

  return 0;
}

static int rtc_open(const char *devpath)
{
  int fd = open(devpath, O_RDWR);

  if (fd < 0)
    {
      printf("ERROR: open %s failed: errno=%d\n", devpath, errno);
      return -errno;
    }

  return fd;
}

static int rtc_close(int fd)
{
  if (close(fd) < 0)
    {
      int error = errno;

      printf("ERROR: close fd=%d failed: errno=%d\n", fd, error);
      return -error;
    }

  return 0;
}

static int rtc_read_time(int fd, struct rtc_time *time)
{
  if (ioctl(fd, RTC_RD_TIME, (unsigned long)(uintptr_t)time) < 0)
    {
      printf("ERROR: RTC_RD_TIME failed: errno=%d\n", errno);
      return -errno;
    }

  return 0;
}

static int rtc_set_time(int fd, const struct rtc_time *time)
{
  if (ioctl(fd, RTC_SET_TIME, (unsigned long)(uintptr_t)time) < 0)
    {
      return -errno;
    }

  return 0;
}

static void print_time(const char *label, const struct rtc_time *time)
{
  printf("  %s: %04d-%02d-%02d %02d:%02d:%02d",
         label, time->tm_year + 1900, time->tm_mon + 1, time->tm_mday,
         time->tm_hour, time->tm_min, time->tm_sec);
#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  printf(".%09ld", time->tm_nsec);
#endif
  printf(" UTC\n");
}

static void make_time(struct rtc_time *time, int year, int month, int day,
                      int hour, int minute, int second, long nsec)
{
  memset(time, 0, sizeof(*time));
  time->tm_year = year - 1900;
  time->tm_mon = month - 1;
  time->tm_mday = day;
  time->tm_hour = hour;
  time->tm_min = minute;
  time->tm_sec = second;
#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  time->tm_nsec = nsec;
#else
  (void)nsec;
#endif
}

static int rtc_time_to_epoch(const struct rtc_time *time, time_t *epoch)
{
  struct tm tm;

  memcpy(&tm, time, sizeof(tm));
  *epoch = timegm(&tm);
  return *epoch < 0 ? -ERANGE : 0;
}

static int rtc_epoch_to_time(time_t epoch, struct rtc_time *time)
{
  struct tm tm;

  if (gmtime_r(&epoch, &tm) == NULL)
    {
      return -ERANGE;
    }

  memset(time, 0, sizeof(*time));
  memcpy(time, &tm, sizeof(tm));
  return 0;
}

#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
static int rtc_elapsed_ns(const struct rtc_time *before,
                          const struct rtc_time *after, int64_t *elapsed_ns)
{
  time_t before_epoch;
  time_t after_epoch;

  if (rtc_time_to_epoch(before, &before_epoch) < 0 ||
      rtc_time_to_epoch(after, &after_epoch) < 0)
    {
      return -ERANGE;
    }

  *elapsed_ns = ((int64_t)after_epoch - (int64_t)before_epoch) *
                1000000000LL;
  *elapsed_ns += after->tm_nsec - before->tm_nsec;
  return 0;
}
#endif

static int expect_invalid_time(int fd, const char *label,
                               const struct rtc_time *invalid)
{
  struct rtc_time before;
  struct rtc_time after;
#ifndef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  time_t before_epoch;
  time_t after_epoch;
#else
  int64_t elapsed_ns;
#endif
  int ret;

  ret = rtc_read_time(fd, &before);
  if (ret < 0)
    {
      return ret;
    }

  ret = rtc_set_time(fd, invalid);
  if (ret != -EINVAL)
    {
      printf("  FAIL: invalid %s ret=%d, expected=%d\n", label, ret,
             -EINVAL);
      (void)rtc_set_time(fd, &before);
      return -EIO;
    }

  ret = rtc_read_time(fd, &after);
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  if (rtc_elapsed_ns(&before, &after, &elapsed_ns) < 0 || elapsed_ns < 0 ||
      elapsed_ns > RTC_READBACK_TOLERANCE_NS)
    {
      printf("  FAIL: invalid %s changed RTC time\n", label);
      print_time("before", &before);
      print_time("after", &after);
      return -EIO;
    }

  printf("  invalid %s rejected; RTC advanced %lld ns\n", label,
         (long long)elapsed_ns);
#else
  if (rtc_time_to_epoch(&before, &before_epoch) < 0 ||
      rtc_time_to_epoch(&after, &after_epoch) < 0)
    {
      return -EIO;
    }

  if (after_epoch < before_epoch || after_epoch > before_epoch + 1)
    {
      printf("  FAIL: invalid %s changed RTC time\n", label);
      print_time("before", &before);
      print_time("after", &after);
      return -EIO;
    }

  printf("  invalid %s rejected without changing RTC\n", label);
#endif
  return 0;
}

static int rtc_matches_system_time(int fd, const char *step)
{
  struct timespec system_time;
  struct rtc_time rtc_time;
  time_t rtc_epoch;
  int64_t delta_ns;
  int ret;

  ret = rtc_read_time(fd, &rtc_time);
  if (ret < 0 || (ret = rtc_time_to_epoch(&rtc_time, &rtc_epoch)) < 0)
    {
      return ret;
    }

  if (clock_gettime(CLOCK_REALTIME, &system_time) < 0)
    {
      printf("  FAIL: %s CLOCK_REALTIME read errno=%d\n", step, errno);
      return -errno;
    }

  delta_ns = ((int64_t)rtc_epoch - (int64_t)system_time.tv_sec) *
             1000000000LL;
#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  delta_ns += rtc_time.tm_nsec;
#endif
  delta_ns -= system_time.tv_nsec;
  if (delta_ns < 0)
    {
      delta_ns = -delta_ns;
    }

  printf("  %s RTC/system delta=%lld ns\n", step, (long long)delta_ns);
  if (delta_ns > CLOCK_MATCH_TOLERANCE_NS)
    {
      printf("  FAIL: %s RTC and CLOCK_REALTIME differ\n", step);
      return -EIO;
    }

  return 0;
}

#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
static int rtc_check_subsecond_monotonic(int fd)
{
  struct timespec deadline;
  struct timespec now;
  struct rtc_time previous;
  struct rtc_time current;
  time_t previous_epoch;
  time_t current_epoch;
  unsigned int samples = 0;
  bool crossed_second = false;
  int ret;

  ret = rtc_read_time(fd, &previous);
  if (ret < 0 || rtc_time_to_epoch(&previous, &previous_epoch) < 0 ||
      clock_gettime(CLOCK_MONOTONIC, &deadline) < 0)
    {
      return ret < 0 ? ret : -EIO;
    }

  deadline.tv_sec += SUBSECOND_TIMEOUT_MS / 1000;
  deadline.tv_nsec += (SUBSECOND_TIMEOUT_MS % 1000) * 1000000L;
  if (deadline.tv_nsec >= 1000000000L)
    {
      deadline.tv_sec++;
      deadline.tv_nsec -= 1000000000L;
    }

  for (; ; )
    {
      usleep(SUBSECOND_SAMPLE_US);
      ret = rtc_read_time(fd, &current);
      if (ret < 0 || rtc_time_to_epoch(&current, &current_epoch) < 0)
        {
          return ret < 0 ? ret : -EIO;
        }

      samples++;
      if (current_epoch < previous_epoch ||
          (current_epoch == previous_epoch &&
           current.tm_nsec < previous.tm_nsec))
        {
          printf("  FAIL: subsecond time moved backwards at sample %u\n",
                 samples);
          print_time("previous", &previous);
          print_time("current", &current);
          return -ETIME;
        }

      if (current_epoch > previous_epoch)
        {
          crossed_second = true;
          break;
        }

      previous = current;
      previous_epoch = current_epoch;
      if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        {
          return -errno;
        }

      if (now.tv_sec > deadline.tv_sec ||
          (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec))
        {
          break;
        }
    }

  printf("  subsecond monotonic samples=%u crossed-second=%d\n", samples,
         crossed_second);
  return crossed_second ? 0 : -ETIME;
}
#endif

static int run_case_001(int fd, const struct app_config_s *cfg)
{
  struct timespec original_monotonic;
  struct timespec current_monotonic;
  struct timespec system_time;
#ifdef CONFIG_BL616CL_RTC_ALARM
  struct rtc_rdalarm_s alarm;
#endif
  struct rtc_time original;
  struct rtc_time start_time;
  struct rtc_time value;
  struct rtc_time readback;
  struct rtc_time restored;
  int64_t fresh_boot_offset_ns;
#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  int64_t readback_delta_ns = INT64_MAX;
#endif
  time_t original_epoch;
  time_t restored_epoch;
  time_t start_epoch;
  int64_t elapsed_seconds;
  int64_t restored_epoch64;
  long restored_nanoseconds;
  bool had_set_time;
  bool have_set_time;
  int ret;

  printf("[RTC-001] time ABI, leap day, invalid date, 2038, subsecond\n");
  printf("  ABI: rtc_time=%u tm=%u offsets sec=%u min=%u year=%u\n",
         (unsigned int)sizeof(struct rtc_time),
         (unsigned int)sizeof(struct tm),
         (unsigned int)offsetof(struct rtc_time, tm_sec),
         (unsigned int)offsetof(struct rtc_time, tm_min),
         (unsigned int)offsetof(struct rtc_time, tm_year));

  if (offsetof(struct rtc_time, tm_sec) != offsetof(struct tm, tm_sec) ||
      offsetof(struct rtc_time, tm_min) != offsetof(struct tm, tm_min) ||
      offsetof(struct rtc_time, tm_year) != offsetof(struct tm, tm_year) ||
      sizeof(struct rtc_time) < sizeof(struct tm))
    {
      printf("  FAIL: rtc_time is not struct tm compatible\n");
      return -EINVAL;
    }

  ret = rtc_read_time(fd, &original);
  if (ret < 0 || clock_gettime(CLOCK_MONOTONIC, &original_monotonic) < 0)
    {
      return ret < 0 ? ret : -errno;
    }

  had_set_time = false;
  if (ioctl(fd, RTC_HAVE_SET_TIME,
            (unsigned long)(uintptr_t)&had_set_time) < 0)
    {
      printf("  FAIL: RTC_HAVE_SET_TIME errno=%d\n", errno);
      return -errno;
    }

  if (!had_set_time)
    {
      make_time(&start_time, CONFIG_START_YEAR, CONFIG_START_MONTH,
                CONFIG_START_DAY, 0, 0, 0, 0);
      if (rtc_time_to_epoch(&original, &original_epoch) < 0 ||
          rtc_time_to_epoch(&start_time, &start_epoch) < 0)
        {
          return -EIO;
        }

      fresh_boot_offset_ns =
        ((int64_t)original_epoch - (int64_t)start_epoch) * 1000000000LL;
#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
      fresh_boot_offset_ns += original.tm_nsec;
#endif
      if (fresh_boot_offset_ns < 0 ||
          fresh_boot_offset_ns > FRESH_BOOT_MAX_SECONDS * 1000000000LL)
        {
          printf("  FAIL: fresh-boot RTC is outside the %d-second window\n",
                 FRESH_BOOT_MAX_SECONDS);
          print_time("expected start", &start_time);
          print_time("actual", &original);
          return -EIO;
        }

      print_time("fresh-boot baseline", &original);
      printf("  fresh-boot HAVE_SET_TIME=0 offset=%lld ns\n",
             (long long)fresh_boot_offset_ns);

#ifdef CONFIG_BL616CL_RTC_ALARM
      memset(&alarm, 0, sizeof(alarm));
      alarm.id = 0;
      if (ioctl(fd, RTC_RD_ALARM,
                (unsigned long)(uintptr_t)&alarm) < 0 ||
          alarm.active)
        {
          printf("  FAIL: fresh-boot alarm active=%d errno=%d\n",
                 alarm.active, errno);
          return -EIO;
        }

      printf("  fresh-boot alarm active=0\n");
#endif
    }

  make_time(&value, 2024, 2, 29, 12, 34, 56, 123456789);
  ret = rtc_set_time(fd, &value);
  if (ret < 0)
    {
      printf("  FAIL: set leap-day time errno=%d\n", -ret);
      return ret;
    }

  if (ioctl(fd, RTC_HAVE_SET_TIME,
            (unsigned long)(uintptr_t)&have_set_time) < 0 ||
      !have_set_time)
    {
      printf("  FAIL: RTC_HAVE_SET_TIME did not become true\n");
      ret = -EIO;
      goto restore;
    }

  ret = rtc_read_time(fd, &readback);
  if (ret < 0)
    {
      goto restore;
    }

  if (readback.tm_year != value.tm_year ||
      readback.tm_mon != value.tm_mon ||
      readback.tm_mday != value.tm_mday ||
      readback.tm_hour != value.tm_hour ||
      readback.tm_min != value.tm_min || readback.tm_sec < value.tm_sec ||
      readback.tm_sec > value.tm_sec + 1)
    {
      printf("  FAIL: leap-day readback differs\n");
      print_time("expected", &value);
      print_time("actual", &readback);
      ret = -EIO;
      goto restore;
    }

  ret = rtc_matches_system_time(fd, "RTC_SET_TIME");
  if (ret < 0)
    {
      goto restore;
    }

#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  if (readback.tm_nsec < 0 || readback.tm_nsec >= 1000000000L ||
      rtc_elapsed_ns(&value, &readback, &readback_delta_ns) < 0 ||
      readback_delta_ns < 0 ||
      readback_delta_ns > RTC_READBACK_TOLERANCE_NS)
    {
      printf("  FAIL: subsecond settime readback delta=%lld ns\n",
             (long long)readback_delta_ns);
      ret = -EIO;
      goto restore;
    }

  printf("  settime readback delta=%lld ns\n",
         (long long)readback_delta_ns);

  ret = rtc_check_subsecond_monotonic(fd);
  if (ret < 0)
    {
      goto restore;
    }
#endif

  make_time(&value, 2024, 3, 1, 1, 2, 3, 456000000);
  ret = rtc_time_to_epoch(&value, &system_time.tv_sec);
  if (ret < 0)
    {
      goto restore;
    }

#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  system_time.tv_nsec = value.tm_nsec;
#else
  system_time.tv_nsec = 0;
#endif
  if (clock_settime(CLOCK_REALTIME, &system_time) < 0)
    {
      ret = -errno;
      printf("  FAIL: clock_settime errno=%d\n", errno);
      goto restore;
    }

  ret = rtc_matches_system_time(fd, "clock_settime");
  if (ret < 0)
    {
      goto restore;
    }

  make_time(&value, 2023, 2, 29, 0, 0, 0, 0);
  if ((ret = expect_invalid_time(fd, "non-leap date", &value)) < 0)
    {
      goto restore;
    }

  make_time(&value, 2024, 1, 1, 0, 0, 0, 1000000000L);
#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  if ((ret = expect_invalid_time(fd, "nanosecond", &value)) < 0)
    {
      goto restore;
    }
#else
  ret = rtc_set_time(fd, &value);
  if (ret < 0)
    {
      printf("  FAIL: valid whole-second settime ret=%d\n", ret);
      goto restore;
    }
#endif

  make_time(&value, 2024, 1, 1, 0, 0, 0, 0);
  value.tm_mon = -1;
  if ((ret = expect_invalid_time(fd, "month -1", &value)) < 0)
    {
      goto restore;
    }

  value.tm_mon = 12;
  if ((ret = expect_invalid_time(fd, "month 12", &value)) < 0)
    {
      goto restore;
    }

  make_time(&value, 2024, 1, 1, 0, 0, 0, 0);
  value.tm_mday = 0;
  if ((ret = expect_invalid_time(fd, "day 0", &value)) < 0)
    {
      goto restore;
    }

  value.tm_mday = 32;
  if ((ret = expect_invalid_time(fd, "day 32", &value)) < 0)
    {
      goto restore;
    }

  make_time(&value, 2024, 1, 1, 24, 0, 0, 0);
  if ((ret = expect_invalid_time(fd, "hour 24", &value)) < 0)
    {
      goto restore;
    }

  make_time(&value, 2024, 1, 1, 0, 60, 0, 0);
  if ((ret = expect_invalid_time(fd, "minute 60", &value)) < 0)
    {
      goto restore;
    }

  make_time(&value, 2024, 1, 1, 0, 0, 60, 0);
  if ((ret = expect_invalid_time(fd, "second 60", &value)) < 0)
    {
      goto restore;
    }

  make_time(&value, 1969, 12, 31, 23, 59, 59, 0);
  if ((ret = expect_invalid_time(fd, "pre-epoch date", &value)) < 0)
    {
      goto restore;
    }

  errno = 0;
  if (ioctl(fd, _RTCIOC(0x007f), 0) >= 0 || errno != ENOSYS)
    {
      printf("  FAIL: unknown ioctl errno=%d, expected=%d\n", errno, ENOSYS);
      ret = -EIO;
      goto restore;
    }

  printf("  unknown ioctl rejected with ENOSYS\n");

  make_time(&value, 2038, 1, 19, 3, 14, 7, 0);
  ret = rtc_set_time(fd, &value);
  if (ret < 0)
    {
      printf("  FAIL: 2038 boundary rejected ret=%d\n", ret);
      goto restore;
    }

  if (sizeof(time_t) <= 4)
    {
      make_time(&value, 2038, 1, 19, 3, 14, 8, 0);
      ret = rtc_set_time(fd, &value);
      if (ret != -EINVAL)
        {
          printf("  FAIL: 32-bit post-2038 ret=%d, expected=%d\n",
                 ret, -EINVAL);
          ret = -EIO;
          goto restore;
        }
    }

  ret = 0;
restore:
  if (clock_gettime(CLOCK_MONOTONIC, &current_monotonic) < 0 ||
      rtc_time_to_epoch(&original, &original_epoch) < 0)
    {
      printf("  WARN: failed to calculate restored RTC time\n");
      if (ret == 0)
        {
          ret = -EIO;
        }

      goto restored_done;
    }

  elapsed_seconds = current_monotonic.tv_sec - original_monotonic.tv_sec;
  restored_nanoseconds = current_monotonic.tv_nsec -
                         original_monotonic.tv_nsec;
#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  restored_nanoseconds += original.tm_nsec;
#endif
  while (restored_nanoseconds >= 1000000000L)
    {
      elapsed_seconds++;
      restored_nanoseconds -= 1000000000L;
    }

  while (restored_nanoseconds < 0)
    {
      elapsed_seconds--;
      restored_nanoseconds += 1000000000L;
    }

  restored_epoch64 = (int64_t)original_epoch + elapsed_seconds;
  restored_epoch = (time_t)restored_epoch64;
  if ((int64_t)restored_epoch != restored_epoch64 ||
      rtc_epoch_to_time(restored_epoch, &restored) < 0)
    {
      printf("  WARN: restored RTC time is outside time_t range\n");
      if (ret == 0)
        {
          ret = -ERANGE;
        }

      goto restored_done;
    }

#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  restored.tm_nsec = restored_nanoseconds;
#endif
  if (rtc_set_time(fd, &restored) < 0)
    {
      printf("  WARN: failed to restore elapsed RTC time\n");
      if (ret == 0)
        {
          ret = -EIO;
        }
    }

restored_done:

  if (!had_set_time)
    {
      printf("  NOTE: standard RTC ABI cannot restore "
             "HAVE_SET_TIME=false\n");
    }

  if (cfg->verbose)
    {
      printf("  initial RTC_HAVE_SET_TIME=%d\n", had_set_time);
      print_time("restored", &original);
    }

  return ret;
}

#ifdef CONFIG_BL616CL_RTC_ALARM
static void drain_alarm_signal(const sigset_t *set)
{
  struct timespec zero;
  siginfo_t info;

  zero.tv_sec = 0;
  zero.tv_nsec = 0;
  while (sigtimedwait(set, &info, &zero) >= 0)
    {
    }
}

static int wait_alarm_signal(const sigset_t *set, int expected_seconds,
                             int timeout_seconds, int value)
{
  struct timespec start;
  struct timespec end;
  struct timespec timeout;
  siginfo_t info;
  int64_t elapsed_ms;
  int ret;

  if (clock_gettime(CLOCK_MONOTONIC, &start) < 0)
    {
      return -errno;
    }

  timeout.tv_sec = timeout_seconds;
  timeout.tv_nsec = 0;
  ret = sigtimedwait(set, &info, &timeout);
  if (ret < 0)
    {
      printf("  FAIL: alarm signal wait errno=%d\n", errno);
      return -errno;
    }

  if (ret != RTC_TEST_SIGNO || info.si_value.sival_int != value)
    {
      printf("  FAIL: signal=%d value=%d, expected signal=%d value=%d\n",
             ret, info.si_value.sival_int, RTC_TEST_SIGNO, value);
      return -EIO;
    }

  if (clock_gettime(CLOCK_MONOTONIC, &end) < 0)
    {
      return -errno;
    }

  elapsed_ms = ((int64_t)end.tv_sec - (int64_t)start.tv_sec) * 1000;
  elapsed_ms += (end.tv_nsec - start.tv_nsec) / 1000000;
  printf("  alarm value=%d elapsed=%lld ms\n", value,
         (long long)elapsed_ms);
  if (elapsed_ms + ALARM_EARLY_TOLERANCE_MS < expected_seconds * 1000)
    {
      printf("  FAIL: alarm value=%d fired early\n", value);
      return -ETIME;
    }

  if (elapsed_ms > expected_seconds * 1000 + ALARM_LATE_TOLERANCE_MS)
    {
      printf("  FAIL: alarm value=%d fired late\n", value);
      return -ETIME;
    }

  return 0;
}

static int wait_no_alarm_signal(const sigset_t *set, int seconds)
{
  struct timespec timeout;
  siginfo_t info;
  int ret;

  timeout.tv_sec = seconds;
  timeout.tv_nsec = 0;
  ret = sigtimedwait(set, &info, &timeout);
  if (ret >= 0)
    {
      printf("  FAIL: canceled alarm signal=%d value=%d was delivered\n",
             ret, info.si_value.sival_int);
      return -EIO;
    }

  if (errno != EAGAIN)
    {
      printf("  FAIL: canceled alarm wait errno=%d\n", errno);
      return -errno;
    }

  return 0;
}

static int wait_no_duplicate_alarm(const sigset_t *set)
{
  struct timespec timeout;
  siginfo_t info;
  int ret;

  timeout.tv_sec = 0;
  timeout.tv_nsec = ALARM_DUPLICATE_WAIT_MS * 1000000L;
  ret = sigtimedwait(set, &info, &timeout);
  if (ret >= 0)
    {
      printf("  FAIL: duplicate alarm signal=%d value=%d\n", ret,
             info.si_value.sival_int);
      return -EIO;
    }

  return errno == EAGAIN ? 0 : -errno;
}

static int alarm_check(int fd, bool expected,
                       const struct rtc_time *expected_time)
{
  struct rtc_rdalarm_s alarm;
  time_t actual_epoch;
  time_t expected_epoch;

  memset(&alarm, 0, sizeof(alarm));
  alarm.id = 0;
  if (ioctl(fd, RTC_RD_ALARM, (unsigned long)(uintptr_t)&alarm) < 0)
    {
      printf("  FAIL: RTC_RD_ALARM errno=%d\n", errno);
      return -errno;
    }

  if (alarm.active != expected)
    {
      printf("  FAIL: alarm active=%d, expected=%d\n", alarm.active,
             expected);
      return -EIO;
    }

  if (expected_time != NULL)
    {
      if (rtc_time_to_epoch(&alarm.time, &actual_epoch) < 0 ||
          rtc_time_to_epoch(expected_time, &expected_epoch) < 0 ||
          actual_epoch != expected_epoch)
        {
          printf("  FAIL: alarm deadline differs\n");
          print_time("expected", expected_time);
          print_time("actual", &alarm.time);
          return -EIO;
        }

#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
      if (alarm.time.tm_nsec != expected_time->tm_nsec)
        {
          printf("  FAIL: alarm deadline subsecond differs\n");
          return -EIO;
        }
#endif
    }

  printf("  alarm active=%d", alarm.active);
  if (expected_time != NULL)
    {
      printf(" deadline=%04d-%02d-%02dT%02d:%02d:%02d",
             alarm.time.tm_year + 1900, alarm.time.tm_mon + 1,
             alarm.time.tm_mday, alarm.time.tm_hour, alarm.time.tm_min,
             alarm.time.tm_sec);
    }

  printf("\n");
  return 0;
}

static int alarm_check_relative(int fd, time_t expected_seconds)
{
  struct rtc_rdalarm_s alarm;
  struct rtc_time now;
  time_t alarm_epoch;
  time_t now_epoch;
  int64_t delta_ns;

  memset(&alarm, 0, sizeof(alarm));
  alarm.id = 0;
  if (ioctl(fd, RTC_RD_ALARM, (unsigned long)(uintptr_t)&alarm) < 0 ||
      !alarm.active || rtc_read_time(fd, &now) < 0 ||
      rtc_time_to_epoch(&alarm.time, &alarm_epoch) < 0 ||
      rtc_time_to_epoch(&now, &now_epoch) < 0)
    {
      printf("  FAIL: relative alarm deadline query failed\n");
      return -EIO;
    }

  delta_ns = ((int64_t)alarm_epoch - (int64_t)now_epoch) * 1000000000LL;
#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  delta_ns += alarm.time.tm_nsec - now.tm_nsec;
#endif

  printf("  relative deadline delta=%lld ns\n", (long long)delta_ns);
  if (delta_ns < expected_seconds * 1000000000LL -
                   ALARM_QUERY_TOLERANCE_MS * 1000000LL ||
      delta_ns > expected_seconds * 1000000000LL)
    {
      printf("  FAIL: relative alarm deadline is outside tolerance\n");
      return -ETIME;
    }

  return 0;
}

static int set_relative_alarm(int fd, time_t seconds, int value)
{
  struct rtc_setrelative_s alarm;

  memset(&alarm, 0, sizeof(alarm));
  alarm.id = 0;
  alarm.pid = 0;
  alarm.reltime = seconds;
  alarm.event.sigev_notify = SIGEV_SIGNAL;
  alarm.event.sigev_signo = RTC_TEST_SIGNO;
  alarm.event.sigev_value.sival_int = value;

  if (ioctl(fd, RTC_SET_RELATIVE, (unsigned long)(uintptr_t)&alarm) < 0)
    {
      return -errno;
    }

  return 0;
}

static int set_absolute_alarm_to(int fd, const struct rtc_time *time,
                                 int value, pid_t pid)
{
  struct rtc_setalarm_s alarm;

  memset(&alarm, 0, sizeof(alarm));
  alarm.id = 0;
  alarm.pid = pid;
  alarm.time = *time;
  alarm.event.sigev_notify = SIGEV_SIGNAL;
  alarm.event.sigev_signo = RTC_TEST_SIGNO;
  alarm.event.sigev_value.sival_int = value;

  if (ioctl(fd, RTC_SET_ALARM, (unsigned long)(uintptr_t)&alarm) < 0)
    {
      return -errno;
    }

  return 0;
}

static int set_absolute_alarm(int fd, const struct rtc_time *time, int value)
{
  return set_absolute_alarm_to(fd, time, value, 0);
}

static int run_short_alarm_test(int fd, const sigset_t *set)
{
  struct rtc_time now;
  struct rtc_time target;
  time_t epoch;
  long nanosecond;
  int attempt;
  int ret;

  for (attempt = 0; attempt < 10; attempt++)
    {
      ret = rtc_read_time(fd, &now);
      if (ret < 0 || (ret = rtc_time_to_epoch(&now, &epoch)) < 0)
        {
          return ret;
        }

#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
      nanosecond = now.tm_nsec + 500000;
#else
      nanosecond = 0;
#endif
      if (nanosecond >= 1000000000L)
        {
          epoch++;
          nanosecond -= 1000000000L;
        }

      ret = rtc_epoch_to_time(epoch, &target);
      if (ret < 0)
        {
          return ret;
        }

#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
      target.tm_nsec = nanosecond;
#endif
      ret = set_absolute_alarm(fd, &target, ALARM_VALUE_SHORT);
      if (ret == 0)
        {
          ret = wait_alarm_signal(set, 0, 1, ALARM_VALUE_SHORT);
          if (ret == 0)
            {
              ret = alarm_check(fd, false, NULL);
            }

          if (ret == 0)
            {
              ret = wait_no_duplicate_alarm(set);
            }

          return ret;
        }

      if (ret != -ETIME)
        {
          return ret;
        }
    }

  printf("  FAIL: short absolute alarm could not be armed\n");
  return -ETIME;
}

static void *alarm_receiver(void *arg)
{
  struct alarm_receiver_s *receiver = arg;
  struct timespec timeout;
  siginfo_t info;

  timeout.tv_sec = 1;
  timeout.tv_nsec = 0;
  receiver->pid = gettid();
  if (sem_post(&receiver->ready) < 0)
    {
      receiver->error = errno;
      return NULL;
    }

  receiver->signal = sigtimedwait(&receiver->set, &info, &timeout);
  if (receiver->signal < 0)
    {
      receiver->error = errno;
      return NULL;
    }

  receiver->value = info.si_value.sival_int;
  if (clock_gettime(CLOCK_REALTIME, &receiver->received_time) < 0)
    {
      receiver->error = errno;
    }

  return NULL;
}

static int run_settime_order_test(int fd, const sigset_t *set)
{
  struct alarm_receiver_s receiver;
  struct sched_param param;
  struct timespec ready_timeout;
  struct rtc_time now;
  struct rtc_time settime;
  struct rtc_time target;
  pthread_attr_t attr;
  pthread_t thread;
  time_t epoch;
  time_t settime_epoch;
  long settime_nanosecond = 0;
  int priority_max;
  int cleanup_ret;
  int ret;

  memset(&receiver, 0, sizeof(receiver));
  receiver.set = *set;
  if (sem_init(&receiver.ready, 0, 0) < 0)
    {
      printf("  FAIL: receiver semaphore initialization errno=%d\n", errno);
      return -errno;
    }

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      sem_destroy(&receiver.ready);
      printf("  FAIL: receiver attribute initialization ret=%d\n", ret);
      return -ret;
    }

  if (sched_getparam(0, &param) < 0)
    {
      ret = -errno;
      pthread_attr_destroy(&attr);
      sem_destroy(&receiver.ready);
      printf("  FAIL: receiver priority query errno=%d\n", -ret);
      return ret;
    }

  priority_max = sched_get_priority_max(SCHED_FIFO);
  if (priority_max < 0)
    {
      ret = -errno;
      pthread_attr_destroy(&attr);
      sem_destroy(&receiver.ready);
      printf("  FAIL: FIFO priority range query errno=%d\n", -ret);
      return ret;
    }

  if (param.sched_priority < priority_max)
    {
      param.sched_priority++;
    }

  ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  if (ret == 0)
    {
      ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setschedparam(&attr, &param);
    }

  if (ret == 0)
    {
      ret = pthread_create(&thread, &attr, alarm_receiver, &receiver);
    }

  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      sem_destroy(&receiver.ready);
      printf("  FAIL: high-priority receiver create ret=%d\n", ret);
      return -ret;
    }

  if (clock_gettime(CLOCK_REALTIME, &ready_timeout) < 0)
    {
      ret = -errno;
      printf("  FAIL: receiver ready clock errno=%d\n", -ret);
      goto join_receiver;
    }

  ready_timeout.tv_sec += RECEIVER_READY_TIMEOUT_S;
  do
    {
      ret = sem_timedwait(&receiver.ready, &ready_timeout);
    }
  while (ret < 0 && errno == EINTR);

  if (ret < 0)
    {
      ret = -errno;
      printf("  FAIL: receiver ready wait errno=%d\n", -ret);
      goto join_receiver;
    }

  ret = rtc_read_time(fd, &now);
  if (ret == 0)
    {
      ret = rtc_time_to_epoch(&now, &epoch);
    }

  if (ret == 0)
    {
      ret = rtc_epoch_to_time(epoch + ALARM_CANCEL_WAIT_SECONDS, &target);
    }

#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  if (ret == 0)
    {
      target.tm_nsec = now.tm_nsec;
    }
#endif

  if (ret == 0)
    {
      ret = set_absolute_alarm_to(fd, &target, ALARM_VALUE_SETTIME_FIRE,
                                  receiver.pid);
    }

  if (ret == 0)
    {
      ret = rtc_epoch_to_time(epoch + ALARM_CANCEL_WAIT_SECONDS + 1,
                              &settime);
    }

#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  if (ret == 0)
    {
      settime.tm_nsec = now.tm_nsec;
      settime_nanosecond = now.tm_nsec;
    }
#endif

  if (ret == 0)
    {
      ret = rtc_set_time(fd, &settime);
    }

join_receiver:
  cleanup_ret = pthread_join(thread, NULL);
  if (cleanup_ret != 0)
    {
      printf("  FAIL: receiver join ret=%d\n", cleanup_ret);
      return ret < 0 ? ret : -cleanup_ret;
    }

  if (sem_destroy(&receiver.ready) < 0)
    {
      cleanup_ret = -errno;
      printf("  FAIL: receiver semaphore destroy errno=%d\n", -cleanup_ret);
      return ret < 0 ? ret : cleanup_ret;
    }

  if (ret < 0)
    {
      return ret;
    }

  if (receiver.error != 0 || receiver.signal != RTC_TEST_SIGNO ||
      receiver.value != ALARM_VALUE_SETTIME_FIRE ||
      rtc_time_to_epoch(&settime, &settime_epoch) < 0 ||
      receiver.received_time.tv_sec < settime_epoch ||
      (receiver.received_time.tv_sec == settime_epoch &&
       receiver.received_time.tv_nsec < settime_nanosecond))
    {
      printf("  FAIL: high-priority receiver signal=%d value=%d errno=%d "
             "time=%lld.%09ld\n",
             receiver.signal, receiver.value, receiver.error,
             (long long)receiver.received_time.tv_sec,
             receiver.received_time.tv_nsec);
      return -EIO;
    }

  printf("  receiver observed post-settime wall time=%lld.%09ld\n",
         (long long)receiver.received_time.tv_sec,
         receiver.received_time.tv_nsec);
  return alarm_check(fd, false, NULL);
}

static int run_case_002(int fd)
{
  sigset_t set;
  struct rtc_time now;
  struct rtc_time target;
  time_t epoch;
  int ret;

  printf("[RTC-002] relative/absolute alarm, rearm, active state, "
         "signal value\n");
  sigemptyset(&set);
  sigaddset(&set, RTC_TEST_SIGNO);
  sigprocmask(SIG_BLOCK, &set, NULL);
  drain_alarm_signal(&set);

  ret = set_relative_alarm(fd, ALARM_SECONDS, ALARM_VALUE_RELATIVE);
  if (ret < 0 || (ret = alarm_check_relative(fd, ALARM_SECONDS)) < 0 ||
      (ret = wait_alarm_signal(&set, ALARM_SECONDS, ALARM_SECONDS + 2,
                               ALARM_VALUE_RELATIVE)) < 0 ||
      (ret = alarm_check(fd, false, NULL)) < 0 ||
      (ret = wait_no_duplicate_alarm(&set)) < 0)
    {
      goto cancel;
    }

  ret = set_relative_alarm(fd, ALARM_SECONDS, ALARM_VALUE_REARM);
  if (ret < 0 || (ret = alarm_check_relative(fd, ALARM_SECONDS)) < 0 ||
      (ret = wait_alarm_signal(&set, ALARM_SECONDS, ALARM_SECONDS + 2,
                               ALARM_VALUE_REARM)) < 0 ||
      (ret = alarm_check(fd, false, NULL)) < 0 ||
      (ret = wait_no_duplicate_alarm(&set)) < 0)
    {
      goto cancel;
    }

  ret = rtc_read_time(fd, &now);
  if (ret < 0 || (ret = rtc_time_to_epoch(&now, &epoch)) < 0 ||
      (ret = rtc_epoch_to_time(epoch + ALARM_SECONDS, &target)) < 0)
    {
      goto cancel;
    }

#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  target.tm_nsec = now.tm_nsec;
#endif

  ret = set_absolute_alarm(fd, &target, ALARM_VALUE_ABSOLUTE);
  if (ret < 0 || (ret = alarm_check(fd, true, &target)) < 0 ||
      (ret = wait_alarm_signal(&set, ALARM_SECONDS, ALARM_SECONDS + 2,
                               ALARM_VALUE_ABSOLUTE)) < 0 ||
      (ret = alarm_check(fd, false, NULL)) < 0 ||
      (ret = wait_no_duplicate_alarm(&set)) < 0)
    {
      goto cancel;
    }

  return 0;
cancel:
  (void)ioctl(fd, RTC_CANCEL_ALARM, 0);
  return ret;
}

static int run_case_003(int fd)
{
  sigset_t set;
  struct rtc_time future;
  struct rtc_time now;
  struct rtc_time past;
  struct rtc_time settime;
  time_t epoch;
  int ret;

  printf("[RTC-003] cancel, replace, reject, settime reprogram\n");
  sigemptyset(&set);
  sigaddset(&set, RTC_TEST_SIGNO);
  sigprocmask(SIG_BLOCK, &set, NULL);
  drain_alarm_signal(&set);

  ret = set_relative_alarm(fd, ALARM_CANCEL_WAIT_SECONDS,
                           ALARM_VALUE_RELATIVE);
  if (ret < 0 ||
      (ret = alarm_check_relative(fd, ALARM_CANCEL_WAIT_SECONDS)) < 0)
    {
      goto cancel;
    }

  if (ioctl(fd, RTC_CANCEL_ALARM, 0) < 0 ||
      (ret = alarm_check(fd, false, NULL)) < 0)
    {
      ret = ret < 0 ? ret : -errno;
      printf("  FAIL: RTC_CANCEL_ALARM errno=%d\n", errno);
      goto cancel;
    }

  if (ioctl(fd, RTC_CANCEL_ALARM, 0) < 0 ||
      (ret = alarm_check(fd, false, NULL)) < 0)
    {
      ret = ret < 0 ? ret : -errno;
      printf("  FAIL: repeated RTC_CANCEL_ALARM errno=%d\n", errno);
      goto cancel;
    }

  ret = wait_no_alarm_signal(&set, ALARM_CANCEL_WAIT_SECONDS + 1);
  if (ret < 0)
    {
      goto cancel;
    }

  ret = set_relative_alarm(fd, ALARM_CANCEL_WAIT_SECONDS,
                           ALARM_VALUE_RELATIVE);
  if (ret < 0 ||
      (ret = alarm_check_relative(fd, ALARM_CANCEL_WAIT_SECONDS)) < 0)
    {
      goto cancel;
    }

  ret = set_relative_alarm(fd, ALARM_SECONDS, ALARM_VALUE_REPLACE);
  if (ret < 0 || (ret = alarm_check_relative(fd, ALARM_SECONDS)) < 0 ||
      (ret = wait_alarm_signal(&set, ALARM_SECONDS, ALARM_SECONDS + 2,
                               ALARM_VALUE_REPLACE)) < 0 ||
      (ret = alarm_check(fd, false, NULL)) < 0 ||
      (ret = wait_no_alarm_signal(&set, ALARM_SECONDS)) < 0)
    {
      goto cancel;
    }

  ret = rtc_read_time(fd, &now);
  if (ret < 0 || (ret = rtc_time_to_epoch(&now, &epoch)) < 0 ||
      (ret = rtc_epoch_to_time(epoch - 1, &past)) < 0)
    {
      goto cancel;
    }

  ret = set_absolute_alarm(fd, &past, ALARM_VALUE_ABSOLUTE);
  if (ret != -ETIME)
    {
      printf("  FAIL: past absolute alarm ret=%d, expected=%d\n",
             ret, -ETIME);
      ret = -EIO;
      goto cancel;
    }

  ret = set_relative_alarm(fd, 0, ALARM_VALUE_RELATIVE);
  if (ret != -EINVAL)
    {
      printf("  FAIL: zero relative alarm ret=%d, expected=%d\n",
             ret, -EINVAL);
      ret = -EIO;
      goto cancel;
    }

  ret = set_relative_alarm(fd, (time_t)-1, ALARM_VALUE_RELATIVE);
  if (ret != -EINVAL)
    {
      printf("  FAIL: negative relative alarm ret=%d, expected=%d\n",
             ret, -EINVAL);
      ret = -EIO;
      goto cancel;
    }

  make_time(&future, 2038, 1, 19, 3, 14, 7, 0);
  ret = set_absolute_alarm(fd, &future, ALARM_VALUE_ABSOLUTE);
  if (ret < 0 || (ret = alarm_check(fd, true, &future)) < 0 ||
      ioctl(fd, RTC_CANCEL_ALARM, 0) < 0 ||
      (ret = alarm_check(fd, false, NULL)) < 0)
    {
      ret = ret < 0 ? ret : -errno;
      printf("  FAIL: 2038 absolute alarm boundary ret=%d\n", ret);
      goto cancel;
    }

  if (sizeof(time_t) <= 4)
    {
      ret = set_relative_alarm(fd, INT32_MAX, ALARM_VALUE_RELATIVE);
      if (ret != -ERANGE)
        {
          printf("  FAIL: 32-bit relative range ret=%d, expected=%d\n",
                 ret, -ERANGE);
          ret = -EIO;
          goto cancel;
        }
    }

  ret = set_relative_alarm(fd, ALARM_SECONDS, ALARM_VALUE_SETTIME_REARM);
  if (ret < 0 || (ret = rtc_read_time(fd, &settime)) < 0 ||
      (ret = rtc_set_time(fd, &settime)) < 0 ||
      (ret = alarm_check(fd, true, NULL)) < 0 ||
      (ret = wait_alarm_signal(&set, ALARM_SECONDS, ALARM_SECONDS + 2,
                               ALARM_VALUE_SETTIME_REARM)) < 0 ||
      (ret = alarm_check(fd, false, NULL)) < 0)
    {
      goto cancel;
    }

  ret = run_settime_order_test(fd, &set);
  if (ret < 0)
    {
      goto cancel;
    }

  ret = run_short_alarm_test(fd, &set);
  if (ret < 0)
    {
      goto cancel;
    }

  ret = rtc_matches_system_time(fd, "settime-alarm-order");
  if (ret < 0)
    {
      goto cancel;
    }

  ret = 0;
cancel:
  (void)ioctl(fd, RTC_CANCEL_ALARM, 0);
  return ret;
}
#else
static int run_case_002(int fd)
{
  (void)fd;
  printf("[RTC-002] SKIP: CONFIG_BL616CL_RTC_ALARM is not enabled\n");
  return RTC_TEST_SKIPPED;
}

static int run_case_003(int fd)
{
  (void)fd;
  printf("[RTC-003] SKIP: CONFIG_BL616CL_RTC_ALARM is not enabled\n");
  return RTC_TEST_SKIPPED;
}
#endif

static int run_case_005(const char *devpath)
{
#if defined(CONFIG_BL616CL_RTC_ALARM) && \
  !defined(CONFIG_DISABLE_PSEUDOFS_OPERATIONS)
  sigset_t set;
  struct rtc_time now;
  int fd1;
  int fd2;
  int probe;
  int ret;

  printf("[RTC-005] unlink active alarm and pending callback safety\n");
  sigemptyset(&set);
  sigaddset(&set, RTC_TEST_SIGNO);
  sigprocmask(SIG_BLOCK, &set, NULL);
  fd1 = rtc_open(devpath);
  if (fd1 < 0)
    {
      return fd1;
    }

  fd2 = rtc_open(devpath);
  if (fd2 < 0)
    {
      (void)rtc_close(fd1);
      return fd2;
    }

  drain_alarm_signal(&set);
  ret = set_relative_alarm(fd1, ALARM_SECONDS, ALARM_VALUE_UNLINK);
  if (ret < 0 || (ret = alarm_check(fd2, true, NULL)) < 0)
    {
      (void)rtc_close(fd2);
      (void)rtc_close(fd1);
      return ret;
    }

  if (unlink(devpath) < 0)
    {
      ret = -errno;
      printf("  FAIL: unlink %s errno=%d\n", devpath, -ret);
      (void)rtc_close(fd2);
      (void)rtc_close(fd1);
      return ret;
    }

  probe = open(devpath, O_RDWR);
  if (probe >= 0 || errno != ENOENT)
    {
      printf("  FAIL: open after unlink fd=%d errno=%d\n", probe, errno);
      if (probe >= 0)
        {
          (void)rtc_close(probe);
        }

      (void)rtc_close(fd2);
      (void)rtc_close(fd1);
      return -EIO;
    }

  if (rtc_read_time(fd2, &now) < 0)
    {
      (void)rtc_close(fd2);
      (void)rtc_close(fd1);
      return -EIO;
    }

  ret = rtc_close(fd1);
  if (ret < 0)
    {
      (void)rtc_close(fd2);
      return ret;
    }

  ret = alarm_check(fd2, true, NULL);
  if (ret < 0)
    {
      (void)rtc_close(fd2);
      return ret;
    }

  ret = rtc_close(fd2);
  if (ret < 0)
    {
      return ret;
    }

  ret = wait_no_alarm_signal(&set, ALARM_SECONDS + 1);
  if (ret < 0)
    {
      return ret;
    }

  printf("  unlinked device survived first close; "
         "final close canceled alarm\n");
  return 0;
#else
  (void)devpath;
  printf("[RTC-005] SKIP: alarm or pseudo-filesystem unlink "
         "is unavailable\n");
  return RTC_TEST_SKIPPED;
#endif
}

static int run_case_004(void)
{
  uint64_t before_wrap = RTC_COUNTER_MASK - 10;
  uint64_t after_wrap = 5;
  uint64_t counter_before;
  uint64_t counter_after;
  uint64_t counter_delta;
  uint64_t elapsed;
  uint64_t error_millihertz;
  uint64_t error_ppm;
  uint64_t millihertz;
  uint64_t observed_millihertz;
  uint64_t sample_nanoseconds;
  uint32_t denominator;
  uint32_t numerator;
  struct timespec start;
  struct timespec end;

  printf("[RTC-004] raw counter and 48-bit wrap arithmetic\n");
  numerator = bl616cl_rtc_clock_numerator();
  denominator = bl616cl_rtc_clock_denominator();
  millihertz = (uint64_t)numerator * 1000 / denominator;
  if (clock_gettime(CLOCK_MONOTONIC, &start) < 0)
    {
      printf("  FAIL: CLOCK_MONOTONIC start errno=%d\n", errno);
      return -errno;
    }

  counter_before = bl616cl_rtc_counter();
  sleep(COUNTER_SAMPLE_SECONDS);
  counter_after = bl616cl_rtc_counter();
  if (clock_gettime(CLOCK_MONOTONIC, &end) < 0)
    {
      printf("  FAIL: CLOCK_MONOTONIC end errno=%d\n", errno);
      return -errno;
    }

  counter_delta = (counter_after - counter_before) & RTC_COUNTER_MASK;
  if (counter_delta == 0)
    {
      printf("  FAIL: raw RTC counter did not advance\n");
      return -EIO;
    }

  sample_nanoseconds = ((uint64_t)end.tv_sec - (uint64_t)start.tv_sec) *
                       1000000000ULL;
  if (end.tv_nsec < start.tv_nsec)
    {
      sample_nanoseconds -= (uint64_t)start.tv_nsec - end.tv_nsec;
    }
  else
    {
      sample_nanoseconds += (uint64_t)end.tv_nsec - start.tv_nsec;
    }

  observed_millihertz = counter_delta * 1000000000000ULL /
                        sample_nanoseconds;
  error_millihertz = observed_millihertz > millihertz ?
                       observed_millihertz - millihertz :
                       millihertz - observed_millihertz;
  error_ppm = error_millihertz * 1000000 / millihertz;

  elapsed = (after_wrap - before_wrap) & RTC_COUNTER_MASK;
  if (elapsed != 16 ||
      ((UINT64_C(7) - UINT64_C(3)) & RTC_COUNTER_MASK) != 4 ||
      ((UINT64_C(3) - UINT64_C(7)) & RTC_COUNTER_MASK) !=
        RTC_COUNTER_MASK - 3)
    {
      printf("  FAIL: 48-bit modulo arithmetic mismatch\n");
      return -EIO;
    }

  printf("  raw: 0x%012llx -> 0x%012llx, delta=%llu ticks\n",
         (unsigned long long)counter_before,
         (unsigned long long)counter_after,
         (unsigned long long)counter_delta);
  printf("  clock model: %lu / %lu Hz (%llu mHz)\n",
         (unsigned long)numerator, (unsigned long)denominator,
         (unsigned long long)millihertz);
  printf("  sample: %llu ns, observed=%llu mHz, error=%llu ppm\n",
         (unsigned long long)sample_nanoseconds,
         (unsigned long long)observed_millihertz,
         (unsigned long long)error_ppm);
  printf("  wrap: 0x%012llx -> 0x%012llx, elapsed=%llu ticks\n",
         (unsigned long long)before_wrap, (unsigned long long)after_wrap,
         (unsigned long long)elapsed);

  if (error_ppm > CLOCK_MAX_ERROR_PPM)
    {
      printf("  FAIL: clock error exceeds %u ppm\n", CLOCK_MAX_ERROR_PPM);
      return -ERANGE;
    }

  return 0;
}

int main(int argc, char *argv[])
{
  struct app_config_s cfg;
  int fd = -1;
  int ret;
  int failures = 0;
  int skipped = 0;

  ret = parse_args(argc, argv, &cfg);
  if (ret != 0)
    {
      return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

  if (strcmp(cfg.case_id, CASE_004) == 0)
    {
      ret = run_case_004();
      printf("RTC test %s (%d failure%s)\n", ret == 0 ? "PASS" : "FAIL",
             ret == 0 ? 0 : 1, ret == 0 ? "s" : "");
      return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  if (strcmp(cfg.case_id, CASE_005) == 0)
    {
      ret = run_case_005(cfg.devpath);
      if (ret == RTC_TEST_SKIPPED)
        {
          printf("RTC test SKIP (requested case is unavailable)\n");
          return EXIT_FAILURE;
        }

      return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  fd = rtc_open(cfg.devpath);
  if (fd < 0)
    {
      return EXIT_FAILURE;
    }

  if (strcmp(cfg.case_id, CASE_001) == 0 ||
      strcmp(cfg.case_id, CASE_ALL) == 0)
    {
      if (run_case_001(fd, &cfg) < 0)
        {
          failures++;
        }
    }

  if (strcmp(cfg.case_id, CASE_002) == 0 ||
      strcmp(cfg.case_id, CASE_ALL) == 0)
    {
      ret = run_case_002(fd);
      if (ret < 0)
        {
          failures++;
        }
      else if (ret == RTC_TEST_SKIPPED)
        {
          skipped++;
        }
    }

  if (strcmp(cfg.case_id, CASE_003) == 0 ||
      strcmp(cfg.case_id, CASE_ALL) == 0)
    {
      ret = run_case_003(fd);
      if (ret < 0)
        {
          failures++;
        }
      else if (ret == RTC_TEST_SKIPPED)
        {
          skipped++;
        }
    }

  if (rtc_close(fd) < 0)
    {
      failures++;
    }

  if (strcmp(cfg.case_id, CASE_ALL) == 0 && run_case_004() < 0)
    {
      failures++;
    }

  if (failures == 0 && skipped > 0)
    {
      printf("RTC test PARTIAL (0 failures, %d skipped)\n", skipped);
      return EXIT_FAILURE;
    }
  else
    {
      printf("RTC test %s (%d failure%s)\n",
             failures == 0 ? "PASS" : "FAIL", failures,
             failures == 1 ? "" : "s");
    }

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
