/****************************************************************************
 * apps/vendor/bouffalolab/apps/mcu_peripheral_tests/pwm/pwm_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fixedmath.h>
#include <nuttx/timers/pwm.h>

#include <arch/chip/bl616cl_pwm_test.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DEFAULT_PWM_DEVPATH "/dev/pwm0"
#define DEFAULT_HOLD_S      1

#define DUTY_ZERO           ((ub16_t)0)
#define DUTY_25             ((ub16_t)16384)
#define DUTY_50             ((ub16_t)32768)
#define DUTY_75             ((ub16_t)49152)
#define DUTY_MAX            ((ub16_t)UINT16_MAX)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct app_config_s
{
  FAR const char *devpath;
  FAR const char *case_id;
  int hold_s;
  bool verbose;
};

struct pwm_point_s
{
  uint32_t frequency;
  ub16_t duty;
  uint8_t cpol;
  uint8_t dcpol;
};

typedef int (*case_runner_t)(FAR const struct app_config_s *cfg);

struct case_entry_s
{
  FAR const char *id;
  case_runner_t run;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void print_usage(FAR const char *progname)
{
  printf("Usage: %s [-c 001..008|all] [-d dev] [-w seconds] [-v]\n",
         progname);
  printf("  -c <id>  Select one case or all (default: all)\n");
  printf("  -d <dev> PWM device (default: %s)\n", DEFAULT_PWM_DEVPATH);
  printf("  -w <s>   Waveform observation hold per point (default: %d)\n",
         DEFAULT_HOLD_S);
  printf("  -v       Print every diagnostic point\n");
}

static int pwm_open(FAR const char *devpath)
{
  int fd;

  fd = open(devpath, O_RDWR);
  if (fd < 0)
    {
      printf("  FAIL: open %s errno=%d\n", devpath, errno);
      return -errno;
    }

  return fd;
}

static int pwm_set(int fd, FAR const struct pwm_point_s *point)
{
  struct pwm_info_s info;

  memset(&info, 0, sizeof(info));
  info.frequency = point->frequency;
  info.duty = point->duty;
  info.cpol = point->cpol;
  info.dcpol = point->dcpol;

  if (ioctl(fd, PWMIOC_SETCHARACTERISTICS,
            (unsigned long)(uintptr_t)&info) < 0)
    {
      return -errno;
    }

  return 0;
}

static int pwm_get(int fd, FAR struct pwm_info_s *info)
{
  memset(info, 0, sizeof(*info));
  if (ioctl(fd, PWMIOC_GETCHARACTERISTICS,
            (unsigned long)(uintptr_t)info) < 0)
    {
      return -errno;
    }

  return 0;
}

static int pwm_start(int fd)
{
  if (ioctl(fd, PWMIOC_START, 0) < 0)
    {
      return -errno;
    }

  return 0;
}

static int pwm_stop(int fd)
{
  if (ioctl(fd, PWMIOC_STOP, 0) < 0)
    {
      return -errno;
    }

  return 0;
}

static int get_diag(FAR struct bl616cl_pwm_test_diag_s *diag)
{
  int ret;

  memset(diag, 0, sizeof(*diag));
  ret = bl616cl_pwm_test_get_diag(diag);
  if (ret < 0)
    {
      printf("  FAIL: get lower diagnostic ret=%d\n", ret);
    }

  return ret;
}

static void print_diag(FAR const char *label,
                       FAR const struct bl616cl_pwm_test_diag_s *diag)
{
  printf("  %s: calls=%lu/%lu/%lu/%lu src=%luHz actual=%luHz "
         "div=%u period=%u threshold=%u..%u pol=%u stop=%u "
         "channel=%u pin=%u clock=%u started=%u last=%d errors=%lu\n",
         label, (unsigned long)diag->setup_calls,
         (unsigned long)diag->start_calls,
         (unsigned long)diag->stop_calls,
         (unsigned long)diag->shutdown_calls,
         (unsigned long)diag->source_frequency,
         (unsigned long)diag->actual_frequency, diag->divider, diag->period,
         diag->threshold_low, diag->threshold_high,
         diag->polarity_active_high, diag->stop_active,
         diag->channel_enabled, diag->pin_acquired, diag->clock_enabled,
         diag->started, diag->last_error, (unsigned long)diag->error_count);
}

static bool diag_clean(FAR const struct bl616cl_pwm_test_diag_s *diag)
{
  return !diag->channel_enabled && !diag->pin_acquired &&
         !diag->clock_enabled && !diag->started;
}

static int expect_ioctl_errno(int fd, int command, int expected,
                              FAR const char *operation)
{
  int ret;

  errno = 0;
  ret = ioctl(fd, command, 0);
  if (ret >= 0 || errno != expected)
    {
      printf("  FAIL: %s ret=%d errno=%d expected=%d\n",
             operation, ret, errno, expected);
      return -EIO;
    }

  printf("  rejected: %s errno=%d\n", operation, expected);
  return 0;
}

static int expect_set_errno(int fd, FAR const struct pwm_point_s *point,
                            int expected, FAR const char *operation)
{
  int ret;

  errno = 0;
  ret = pwm_set(fd, point);
  if (ret != -expected)
    {
      printf("  FAIL: %s ret=%d expected=%d\n", operation, ret, -expected);
      return -EIO;
    }

  printf("  rejected: %s errno=%d\n", operation, expected);
  return 0;
}

static int verify_clean(FAR const char *operation)
{
  struct bl616cl_pwm_test_diag_s diag;

  if (get_diag(&diag) < 0 || !diag_clean(&diag))
    {
      print_diag(operation, &diag);
      printf("  FAIL: %s left lower resources acquired\n", operation);
      return -EIO;
    }

  return 0;
}

static uint32_t absolute_difference(uint32_t a, uint32_t b)
{
  return a >= b ? a - b : b - a;
}

static int verify_point(FAR const struct pwm_point_s *point,
                        FAR const struct bl616cl_pwm_test_diag_s *diag,
                        bool verbose)
{
  uint64_t product;
  uint32_t calculated_frequency;
  uint32_t frequency_error;
  uint32_t expected_delta;
  uint32_t actual_delta;

  if (diag->source_frequency == 0 || diag->divider == 0 ||
      diag->period < 2 || diag->threshold_low > diag->threshold_high ||
      diag->threshold_high >= diag->period || !diag->channel_enabled ||
      !diag->pin_acquired || !diag->clock_enabled || !diag->started)
    {
      print_diag("invalid register state", diag);
      return -EIO;
    }

  product = (uint64_t)diag->divider * diag->period;
  calculated_frequency =
    (uint32_t)((diag->source_frequency + product / 2) / product);
  frequency_error = absolute_difference(calculated_frequency,
                                        point->frequency);
  if ((uint64_t)frequency_error * 100 > point->frequency)
    {
      print_diag("frequency out of tolerance", diag);
      return -ERANGE;
    }

  actual_delta = diag->threshold_high - diag->threshold_low;
  if (point->duty == DUTY_MAX)
    {
      expected_delta = diag->period - 1;
    }
  else
    {
      expected_delta =
        (uint32_t)(((uint64_t)diag->period * point->duty + 32768) >> 16);
      if (expected_delta >= diag->period)
        {
          expected_delta = diag->period - 1;
        }
    }

  if (absolute_difference(actual_delta, expected_delta) > 1)
    {
      print_diag("duty threshold mismatch", diag);
      return -ERANGE;
    }

  if (diag->cpol != point->cpol || diag->dcpol != point->dcpol ||
      diag->polarity_active_high != (point->cpol == PWM_CPOL_HIGH) ||
      diag->stop_active != ((point->cpol == PWM_CPOL_HIGH &&
                             point->dcpol == PWM_DCPOL_HIGH) ||
                            (point->cpol == PWM_CPOL_LOW &&
                             point->dcpol == PWM_DCPOL_LOW)))
    {
      print_diag("polarity mapping mismatch", diag);
      return -EIO;
    }

  if (verbose)
    {
      print_diag("verified", diag);
    }

  return 0;
}

static int set_start_verify(int fd, FAR const struct pwm_point_s *point,
                            bool already_started, bool verbose)
{
  struct bl616cl_pwm_test_diag_s diag;
  int ret;

  ret = pwm_set(fd, point);
  if (ret < 0)
    {
      printf("  FAIL: SET freq=%lu duty=%lu ret=%d\n",
             (unsigned long)point->frequency,
             (unsigned long)point->duty, ret);
      return ret;
    }

  if (!already_started)
    {
      ret = pwm_start(fd);
      if (ret < 0)
        {
          printf("  FAIL: START freq=%lu duty=%lu ret=%d\n",
                 (unsigned long)point->frequency,
                 (unsigned long)point->duty, ret);
          return ret;
        }
    }

  ret = get_diag(&diag);
  if (ret < 0)
    {
      return ret;
    }

  return verify_point(point, &diag, verbose);
}

static int run_case_001(FAR const struct app_config_s *cfg)
{
  /* clang-format off */

  const struct pwm_point_s point =
  {
    1000, DUTY_50, PWM_CPOL_HIGH, PWM_DCPOL_LOW
  };

  /* clang-format on */

  struct bl616cl_pwm_test_diag_s diag;
  struct pwm_info_s info;
  int fd1 = -1;
  int fd2 = -1;
  int ret = -EIO;

  printf("[PWM-001] Open/close and multi-fd lifecycle\n");
  bl616cl_pwm_test_reset();

  fd1 = pwm_open(cfg->devpath);
  if (fd1 < 0 || get_diag(&diag) < 0 || diag.setup_calls != 1)
    {
      goto out;
    }

  fd2 = pwm_open(cfg->devpath);
  if (fd2 < 0 || get_diag(&diag) < 0 || diag.setup_calls != 1)
    {
      goto out;
    }

  if (set_start_verify(fd1, &point, false, cfg->verbose) < 0)
    {
      goto out;
    }

  close(fd1);
  fd1 = -1;
  if (pwm_get(fd2, &info) < 0 || info.frequency != point.frequency ||
      info.duty != point.duty || get_diag(&diag) < 0 || !diag.started ||
      diag.shutdown_calls != 0)
    {
      printf("  FAIL: first close changed shared state\n");
      goto out;
    }

  if (pwm_stop(fd2) < 0 || pwm_stop(fd2) < 0)
    {
      printf("  FAIL: STOP was not idempotent\n");
      goto out;
    }

  close(fd2);
  fd2 = -1;
  if (get_diag(&diag) < 0 || diag.shutdown_calls != 1 || !diag_clean(&diag))
    {
      print_diag("final close", &diag);
      goto out;
    }

  printf("  setup=1 shutdown=1 active-after-first-close=yes\n");
  printf("  [PWM-001] PASS lifecycle and shared state\n\n");
  return 0;

out:
  if (fd1 >= 0)
    {
      (void)pwm_stop(fd1);
      close(fd1);
    }

  if (fd2 >= 0)
    {
      (void)pwm_stop(fd2);
      close(fd2);
    }

  return ret;
}

static int check_stopped_invalid(int fd, FAR const struct pwm_point_s *point,
                                 int expected, FAR const char *label)
{
  struct pwm_info_s info;

  if (pwm_set(fd, point) < 0 || pwm_get(fd, &info) < 0 ||
      info.frequency != point->frequency || info.cpol != point->cpol ||
      info.dcpol != point->dcpol)
    {
      printf("  FAIL: stopped SET/GET %s did not preserve upper request\n",
             label);
      return -EIO;
    }

  return expect_ioctl_errno(fd, PWMIOC_START, expected, label);
}

static int run_case_002(FAR const struct app_config_s *cfg)
{
  /* clang-format off */

  const struct pwm_point_s valid =
  {
    1000, DUTY_50, PWM_CPOL_HIGH, PWM_DCPOL_LOW
  };

  const struct pwm_point_s invalid[] =
  {
    { 0, DUTY_50, PWM_CPOL_HIGH, PWM_DCPOL_LOW },
    { UINT32_MAX, DUTY_50, PWM_CPOL_HIGH, PWM_DCPOL_LOW },
    { 1000, DUTY_50, PWM_CPOL_NDEF, PWM_DCPOL_LOW },
    { 1000, DUTY_50, PWM_CPOL_HIGH, PWM_DCPOL_NDEF },
    { 1000, DUTY_50, 3, PWM_DCPOL_LOW },
    { 1000, DUTY_50, PWM_CPOL_HIGH, 3 },
  };

  FAR const char *labels[] = {
    "zero frequency",
    "frequency above hardware range",
    "undefined cpol",
    "undefined dcpol",
    "cpol outside ABI",
    "dcpol outside ABI",
  };

  const int expected[] = {
    EINVAL,
    ERANGE,
    EINVAL,
    EINVAL,
    EINVAL,
    EINVAL,
  };

  /* clang-format on */

  struct bl616cl_pwm_test_diag_s before;
  struct bl616cl_pwm_test_diag_s after;
  struct pwm_info_s info;
  int fd;
  int ret = -EIO;
  int i;

  printf("[PWM-002] Missing characteristics and invalid requests\n");
  bl616cl_pwm_test_reset();
  fd = pwm_open(cfg->devpath);
  if (fd < 0)
    {
      return fd;
    }

  /* The upper-half keeps characteristics across close/open. A freshly booted
   * device has zero frequency and exercises START-before-SET; on a reused
   * device report the prerequisite as already consumed instead of claiming a
   * false result.
   */

  if (pwm_get(fd, &info) < 0)
    {
      goto out;
    }

  if (info.frequency == 0)
    {
      if (expect_ioctl_errno(fd, PWMIOC_START, EINVAL,
                             "START before first SET") < 0)
        {
          goto out;
        }
    }
  else
    {
      printf("  START-before-SET: SKIP (upper characteristics already set; "
             "reboot for zero-state proof)\n");
    }

  for (i = 0; i < (int)(sizeof(invalid) / sizeof(invalid[0])); i++)
    {
      if (check_stopped_invalid(fd, &invalid[i], expected[i], labels[i]) < 0)
        {
          goto out;
        }
    }

  if (set_start_verify(fd, &valid, false, cfg->verbose) < 0 ||
      get_diag(&before) < 0)
    {
      goto out;
    }

  if (expect_set_errno(fd, &invalid[0], EINVAL,
                       "running SET zero frequency") < 0 ||
      pwm_get(fd, &info) < 0 || info.frequency != 0 ||
      get_diag(&after) < 0 ||
      after.actual_frequency != before.actual_frequency ||
      after.divider != before.divider || after.period != before.period ||
      !after.started)
    {
      printf("  FAIL: invalid running SET changed hardware or "
             "not upper GET\n");
      goto out;
    }

  if (set_start_verify(fd, &valid, true, cfg->verbose) < 0 ||
      pwm_stop(fd) < 0)
    {
      goto out;
    }

  close(fd);
  if (verify_clean("invalid request cleanup") < 0)
    {
      return -EIO;
    }

  printf("  [PWM-002] PASS errno, upper overwrite and hardware safety\n\n");
  return 0;

out:
  (void)pwm_stop(fd);
  close(fd);
  return ret;
}

static int run_points(FAR const struct app_config_s *cfg,
                      FAR const struct pwm_point_s *points, size_t count,
                      FAR const char *case_id)
{
  int fd;
  int ret = -EIO;
  size_t i;

  fd = pwm_open(cfg->devpath);
  if (fd < 0)
    {
      return fd;
    }

  for (i = 0; i < count; i++)
    {
      if (set_start_verify(fd, &points[i], i != 0, cfg->verbose) < 0)
        {
          goto out;
        }

      printf("  point %u: freq=%luHz duty=%lu/65536 cpol=%u dcpol=%u\n",
             (unsigned)(i + 1), (unsigned long)points[i].frequency,
             (unsigned long)points[i].duty, points[i].cpol,
             points[i].dcpol);
      if (cfg->hold_s > 0)
        {
          sleep(cfg->hold_s);
        }
    }

  if (pwm_stop(fd) < 0)
    {
      goto out;
    }

  close(fd);
  if (verify_clean(case_id) < 0)
    {
      return -EIO;
    }

  return 0;

out:
  (void)pwm_stop(fd);
  close(fd);
  return ret;
}

static int run_case_003(FAR const struct app_config_s *cfg)
{
  static const struct pwm_point_s points[] = {
    { 100, DUTY_75, PWM_CPOL_HIGH, PWM_DCPOL_LOW },
    { 1000, DUTY_25, PWM_CPOL_HIGH, PWM_DCPOL_LOW },
    { 1000, DUTY_50, PWM_CPOL_HIGH, PWM_DCPOL_LOW },
    { 1000, DUTY_75, PWM_CPOL_HIGH, PWM_DCPOL_LOW },
    { 10000, DUTY_25, PWM_CPOL_HIGH, PWM_DCPOL_LOW },
  };

  printf("[PWM-003] Frequency, duty and register readback\n");
  bl616cl_pwm_test_reset();
  if (run_points(cfg, points, sizeof(points) / sizeof(points[0]),
                 "PWM-003") < 0)
    {
      return -EIO;
    }

  printf("  [PWM-003] PASS software frequency and threshold contract\n\n");
  return 0;
}

static int run_case_004(FAR const struct app_config_s *cfg)
{
  static const struct pwm_point_s points[] = {
    { 1000, DUTY_ZERO, PWM_CPOL_HIGH, PWM_DCPOL_LOW },
    { 1000, DUTY_25, PWM_CPOL_HIGH, PWM_DCPOL_LOW },
    { 1000, DUTY_50, PWM_CPOL_HIGH, PWM_DCPOL_LOW },
    { 1000, DUTY_75, PWM_CPOL_HIGH, PWM_DCPOL_LOW },
    { 1000, DUTY_MAX, PWM_CPOL_HIGH, PWM_DCPOL_LOW },
    { 1000, DUTY_25, PWM_CPOL_HIGH, PWM_DCPOL_LOW },
  };

  printf("[PWM-004] Duty boundaries and live update\n");
  bl616cl_pwm_test_reset();
  if (run_points(cfg, points, sizeof(points) / sizeof(points[0]),
                 "PWM-004") < 0)
    {
      return -EIO;
    }

  printf("  [PWM-004] PASS zero, intermediate and 65535/65536 duty\n\n");
  return 0;
}

static int run_case_005(FAR const struct app_config_s *cfg)
{
  static const struct pwm_point_s points[] = {
    { 1000, DUTY_50, PWM_CPOL_LOW, PWM_DCPOL_LOW },
    { 1000, DUTY_50, PWM_CPOL_LOW, PWM_DCPOL_HIGH },
    { 1000, DUTY_50, PWM_CPOL_HIGH, PWM_DCPOL_LOW },
    { 1000, DUTY_50, PWM_CPOL_HIGH, PWM_DCPOL_HIGH },
  };

  struct bl616cl_pwm_test_diag_s diag;
  int fd;
  int ret = -EIO;
  int i;

  printf("[PWM-005] CPOL/DCPOL mapping and stopped state\n");
  bl616cl_pwm_test_reset();
  fd = pwm_open(cfg->devpath);
  if (fd < 0)
    {
      return fd;
    }

  for (i = 0; i < (int)(sizeof(points) / sizeof(points[0])); i++)
    {
      if (set_start_verify(fd, &points[i], false, cfg->verbose) < 0)
        {
          goto out;
        }

      printf("  combination %d: cpol=%u dcpol=%u\n", i + 1,
             points[i].cpol, points[i].dcpol);
      if (cfg->hold_s > 0)
        {
          sleep(cfg->hold_s);
        }

      if (pwm_stop(fd) < 0 || get_diag(&diag) < 0 ||
          diag.channel_enabled || diag.started)
        {
          printf("  FAIL: STOP did not disable combination %d\n", i + 1);
          goto out;
        }
    }

  close(fd);
  if (verify_clean("PWM-005 close") < 0)
    {
      return -EIO;
    }

  printf("  [PWM-005] PASS all CPOL/DCPOL register mappings\n\n");
  return 0;

out:
  (void)pwm_stop(fd);
  close(fd);
  return ret;
}

static int run_case_006(FAR const struct app_config_s *cfg)
{
  /* clang-format off */

  const struct pwm_point_s p1 =
  {
    1000, DUTY_25, PWM_CPOL_HIGH, PWM_DCPOL_LOW
  };

  const struct pwm_point_s p2 =
  {
    1000, DUTY_75, PWM_CPOL_HIGH, PWM_DCPOL_LOW
  };

  const struct pwm_point_s p3 =
  {
    100, DUTY_50, PWM_CPOL_HIGH, PWM_DCPOL_LOW
  };

  const struct pwm_point_s p4 =
  {
    10000, DUTY_50, PWM_CPOL_HIGH, PWM_DCPOL_LOW
  };

  /* clang-format on */

  struct bl616cl_pwm_test_diag_s d1;
  struct bl616cl_pwm_test_diag_s d2;
  struct bl616cl_pwm_test_diag_s d3;
  struct bl616cl_pwm_test_diag_s d4;
  uint32_t start_calls;
  int fd;
  int ret = -EIO;

  printf("[PWM-006] Same-divider and cross-divider updates\n");
  bl616cl_pwm_test_reset();
  fd = pwm_open(cfg->devpath);
  if (fd < 0)
    {
      return fd;
    }

  if (set_start_verify(fd, &p1, false, cfg->verbose) < 0 ||
      get_diag(&d1) < 0)
    {
      goto out;
    }

  start_calls = d1.start_calls;
  if (pwm_start(fd) < 0 || get_diag(&d2) < 0 ||
      d2.start_calls != start_calls)
    {
      printf("  FAIL: repeated START called lower again\n");
      goto out;
    }

  if (set_start_verify(fd, &p2, true, cfg->verbose) < 0 ||
      get_diag(&d2) < 0 || d2.divider != d1.divider ||
      d2.period != d1.period || d2.threshold_high == d1.threshold_high)
    {
      printf("  FAIL: same-frequency duty update changed timebase\n");
      goto out;
    }

  if (set_start_verify(fd, &p3, true, cfg->verbose) < 0 ||
      get_diag(&d3) < 0 ||
      set_start_verify(fd, &p4, true, cfg->verbose) < 0 ||
      get_diag(&d4) < 0 ||
      (d3.divider == d4.divider && d3.period == d4.period) ||
      (d3.divider == d1.divider && d4.divider == d1.divider))
    {
      printf("  FAIL: cross-frequency update did not change timebase\n");
      goto out;
    }

  if (cfg->hold_s > 0)
    {
      sleep(cfg->hold_s);
    }

  if (pwm_stop(fd) < 0 || pwm_stop(fd) < 0 ||
      set_start_verify(fd, &p1, false, cfg->verbose) < 0 ||
      pwm_stop(fd) < 0)
    {
      printf("  FAIL: repeated lifecycle did not recover\n");
      goto out;
    }

  close(fd);
  if (verify_clean("PWM-006 close") < 0)
    {
      return -EIO;
    }

  printf("  divider/period: 1k=%u/%u 100=%u/%u 10k=%u/%u\n",
         d1.divider, d1.period, d3.divider, d3.period,
         d4.divider, d4.period);
  printf("  [PWM-006] PASS update paths and idempotent lifecycle\n\n");
  return 0;

out:
  (void)pwm_stop(fd);
  close(fd);
  return ret;
}

static int expect_fault_cleanup(FAR const char *label, int expected_error)
{
  struct bl616cl_pwm_test_diag_s diag;

  if (get_diag(&diag) < 0 || diag.last_error != expected_error ||
      !diag_clean(&diag))
    {
      print_diag(label, &diag);
      printf("  FAIL: %s did not report and clean timeout\n", label);
      return -EIO;
    }

  return 0;
}

static int run_case_007(FAR const struct app_config_s *cfg)
{
  /* clang-format off */

  const struct pwm_point_s point =
  {
    1000, DUTY_50, PWM_CPOL_HIGH, PWM_DCPOL_LOW
  };

  /* clang-format on */

  struct bl616cl_pwm_test_diag_s diag;
  uint32_t start_calls;
  int fd = -1;
  int ret = -EIO;

  printf("[PWM-007] LHAL timeout errno and forced cleanup\n");
  bl616cl_pwm_test_reset();
  fd = pwm_open(cfg->devpath);
  if (fd < 0 || pwm_set(fd, &point) < 0)
    {
      if (fd >= 0)
        {
          close(fd);
        }

      return -EIO;
    }

  if (bl616cl_pwm_test_set_fault(BL616CL_PWM_TEST_FAULT_INIT_TIMEOUT) < 0)
    {
      printf("  FAIL: could not inject init timeout\n");
      goto out;
    }

  if (expect_ioctl_errno(fd, PWMIOC_START, ETIMEDOUT, "init timeout") < 0 ||
      expect_fault_cleanup("init timeout", -ETIMEDOUT) < 0)
    {
      goto out;
    }

  (void)bl616cl_pwm_test_set_fault(BL616CL_PWM_TEST_FAULT_NONE);
  (void)bl616cl_pwm_test_set_fault(BL616CL_PWM_TEST_FAULT_START_TIMEOUT);
  if (expect_ioctl_errno(fd, PWMIOC_START, ETIMEDOUT, "start timeout") < 0 ||
      expect_fault_cleanup("start timeout", -ETIMEDOUT) < 0)
    {
      goto out;
    }

  (void)bl616cl_pwm_test_set_fault(BL616CL_PWM_TEST_FAULT_NONE);
  if (pwm_start(fd) < 0)
    {
      printf("  FAIL: normal START did not recover\n");
      goto out;
    }

  (void)bl616cl_pwm_test_set_fault(BL616CL_PWM_TEST_FAULT_STOP_TIMEOUT);
  if (expect_ioctl_errno(fd, PWMIOC_STOP, ETIMEDOUT, "stop timeout") < 0 ||
      expect_fault_cleanup("stop timeout", -ETIMEDOUT) < 0)
    {
      goto out;
    }

  (void)bl616cl_pwm_test_set_fault(BL616CL_PWM_TEST_FAULT_NONE);
  if (pwm_start(fd) < 0 || pwm_stop(fd) < 0)
    {
      printf("  FAIL: normal START/STOP did not recover\n");
      goto out;
    }

  /* Shutdown has its own lower-half state wait. The upper half ignores this
   * errno on close, so inspect the diagnostic after closing.
   */

  (void)bl616cl_pwm_test_set_fault(BL616CL_PWM_TEST_FAULT_DEINIT_TIMEOUT);
  if (pwm_start(fd) < 0)
    {
      printf("  FAIL: setup before shutdown timeout failed\n");
      goto out;
    }

  close(fd);
  fd = -1;
  if (get_diag(&diag) < 0 || diag.last_error != -ETIMEDOUT ||
      !diag_clean(&diag))
    {
      print_diag("shutdown timeout", &diag);
      printf("  FAIL: close did not force shutdown cleanup\n");
      return -EIO;
    }

  start_calls = diag.start_calls;
  (void)bl616cl_pwm_test_set_fault(BL616CL_PWM_TEST_FAULT_NONE);
  fd = pwm_open(cfg->devpath);
  if (fd < 0 || pwm_start(fd) < 0 || get_diag(&diag) < 0 ||
      !diag.started || diag.start_calls != start_calls + 1)
    {
      printf("  FAIL: active close/reopen did not restart lower\n");
      if (fd >= 0)
        {
          close(fd);
        }

      return -EIO;
    }

  if (pwm_stop(fd) < 0)
    {
      close(fd);
      return -EIO;
    }

  close(fd);
  if (verify_clean("post-fault recovery") < 0)
    {
      return -EIO;
    }

  printf("  [PWM-007] PASS ETIMEDOUT and forced cleanup contracts\n\n");
  return 0;

out:
  (void)bl616cl_pwm_test_set_fault(BL616CL_PWM_TEST_FAULT_NONE);
  if (fd >= 0)
    {
      (void)pwm_stop(fd);
      close(fd);
    }

  return ret;
}

static int expect_missing(FAR const char *path)
{
  int fd;

  errno = 0;
  fd = open(path, O_RDWR);
  if (fd >= 0)
    {
      close(fd);
      printf("  FAIL: unexpected owner node %s exists\n", path);
      return -EEXIST;
    }

  if (errno != ENOENT)
    {
      printf("  FAIL: open %s errno=%d expected=%d\n", path, errno, ENOENT);
      return -EIO;
    }

  return 0;
}

static int run_case_008(FAR const struct app_config_s *cfg)
{
  /* clang-format off */

  const struct pwm_point_s point =
  {
    1000, DUTY_50, PWM_CPOL_HIGH, PWM_DCPOL_LOW
  };

  /* clang-format on */

  int fd;

  printf("[PWM-008] Device nodes, GPIO22 ownership and cleanup\n");
  bl616cl_pwm_test_reset();
  fd = pwm_open(cfg->devpath);
  if (fd < 0)
    {
      return fd;
    }

  if (expect_missing("/dev/pwm1") < 0 ||
      expect_missing("/dev/gpio22") < 0 ||
      set_start_verify(fd, &point, false, cfg->verbose) < 0 ||
      pwm_stop(fd) < 0)
    {
      (void)pwm_stop(fd);
      close(fd);
      return -EIO;
    }

  close(fd);
  if (verify_clean("PWM-008 close") < 0)
    {
      return -EIO;
    }

  printf("  nodes: pwm0=yes pwm1=no gpio22=no\n");
  printf("  [PWM-008] PASS runtime owner and cleanup contract\n\n");
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  /* PWM-002 is first so its START-before-SET assertion observes the zeroed
   * upper-half state on a freshly booted test firmware.
   */

  static const struct case_entry_s cases[] = {
    { "002", run_case_002 },
    { "001", run_case_001 },
    { "003", run_case_003 },
    { "004", run_case_004 },
    { "005", run_case_005 },
    { "006", run_case_006 },
    { "007", run_case_007 },
    { "008", run_case_008 },
  };

  struct app_config_s cfg;
  bool found = false;
  int executed = 0;
  int passed = 0;
  int opt;
  int ret;
  int i;

  cfg.devpath = DEFAULT_PWM_DEVPATH;
  cfg.case_id = "all";
  cfg.hold_s = DEFAULT_HOLD_S;
  cfg.verbose = false;

  while ((opt = getopt(argc, argv, "c:d:w:vh")) != -1)
    {
      switch (opt)
        {
          case 'c':
            cfg.case_id = optarg;
            break;
          case 'd':
            cfg.devpath = optarg;
            break;
          case 'w':
            cfg.hold_s = atoi(optarg);
            break;
          case 'v':
            cfg.verbose = true;
            break;
          case 'h':
          default:
            print_usage(argv[0]);
            return OK;
        }
    }

  if (cfg.hold_s < 0)
    {
      printf("Invalid hold time: %d\n", cfg.hold_s);
      return ERROR;
    }

  printf("MCU Peripheral PWM Tests\n");
  printf("Case: %s Device: %s GPIO: 22 hold=%ds\n",
         cfg.case_id, cfg.devpath, cfg.hold_s);

  for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++)
    {
      if (strcmp(cfg.case_id, "all") != 0 &&
          strcmp(cfg.case_id, cases[i].id) != 0)
        {
          continue;
        }

      found = true;
      executed++;
      ret = cases[i].run(&cfg);
      if (ret >= 0)
        {
          passed++;
        }
      else
        {
          printf("  [PWM-%s] FAIL ret=%d\n\n", cases[i].id, ret);
          if (strcmp(cfg.case_id, "all") != 0)
            {
              break;
            }
        }
    }

  if (!found)
    {
      printf("Unsupported case id: %s\n", cfg.case_id);
      print_usage(argv[0]);
      return ERROR;
    }

  (void)bl616cl_pwm_test_set_fault(BL616CL_PWM_TEST_FAULT_NONE);
  printf("PWM Summary: executed=%d passed=%d failed=%d -> %s\n",
         executed, passed, executed - passed,
         executed == passed ? "PASS" : "FAIL");

  return executed == passed ? OK : ERROR;
}
