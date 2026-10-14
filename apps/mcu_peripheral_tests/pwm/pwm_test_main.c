/****************************************************************************
 * vendor/bouffalolab/apps/mcu_peripheral_tests/pwm/pwm_test_main.c
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

#include <arch/chip/bl616cl_pwm.h>

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

#define CASE_SKIPPED        1

#ifdef CONFIG_PWM_MULTICHAN
#  define INFO_DUTY(info)   ((info)->channels[0].duty)
#  define INFO_CPOL(info)   ((info)->channels[0].cpol)
#  define INFO_DCPOL(info)  ((info)->channels[0].dcpol)
#else
#  define INFO_DUTY(info)   ((info)->duty)
#  define INFO_CPOL(info)   ((info)->cpol)
#  define INFO_DCPOL(info)  ((info)->dcpol)
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct app_config_s
{
  const char *devpath;
  const char *case_id;
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

typedef int (*case_runner_t)(const struct app_config_s *cfg);

struct case_entry_s
{
  const char *id;
  case_runner_t run;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Cases 001-008 drive the primary channel, the lowest channel the board
 * configured. main() reads it from the lower-half diagnostics.
 */

static int g_primary = -1;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void print_usage(const char *progname)
{
  printf("Usage: %s [-c 001..009|all] [-d dev] [-w seconds] [-v]\n",
         progname);
  printf("  -c <id>  Select one case or all (default: all)\n");
  printf("  -d <dev> PWM device (default: %s)\n", DEFAULT_PWM_DEVPATH);
  printf("  -w <s>   Waveform observation hold per point (default: %d)\n",
         DEFAULT_HOLD_S);
  printf("  -v       Print every diagnostic point\n");
}

static int pwm_open(const char *devpath)
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

static int pwm_set(int fd, const struct pwm_point_s *point)
{
  struct pwm_info_s info;

  memset(&info, 0, sizeof(info));
  info.frequency = point->frequency;
#ifdef CONFIG_PWM_MULTICHAN
  info.channels[0].channel = g_primary + 1;
#endif
  INFO_DUTY(&info) = point->duty;
  INFO_CPOL(&info) = point->cpol;
  INFO_DCPOL(&info) = point->dcpol;

  if (ioctl(fd, PWMIOC_SETCHARACTERISTICS,
            (unsigned long)(uintptr_t)&info) < 0)
    {
      return -errno;
    }

  return 0;
}

static int pwm_get(int fd, struct pwm_info_s *info)
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

static int get_diag(struct bl616cl_pwm_test_diag_s *diag)
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

static void print_diag(const char *label,
                       const struct bl616cl_pwm_test_diag_s *diag)
{
  const struct bl616cl_pwm_test_channel_s *chan;
  int ch;

  printf("  %s: calls=%lu/%lu/%lu/%lu src=%luHz actual=%luHz "
         "div=%u period=%u io_sel=%u pin=%u clock=%u started=%u "
         "last=%d errors=%lu\n",
         label, (unsigned long)diag->setup_calls,
         (unsigned long)diag->start_calls,
         (unsigned long)diag->stop_calls,
         (unsigned long)diag->shutdown_calls,
         (unsigned long)diag->source_frequency,
         (unsigned long)diag->actual_frequency, diag->divider, diag->period,
         diag->io_sel_diff_end, diag->pin_acquired, diag->clock_enabled, diag->started,
         diag->last_error, (unsigned long)diag->error_count);

  for (ch = 0; ch < BL616CL_PWM_TEST_NCHANNELS; ch++)
    {
      chan = &diag->channels[ch];
      if (!chan->configured)
        {
          continue;
        }

      printf("    ch%d pins=%d/%d threshold=%u..%u active=%u enabled=%u "
             "out=%u/%u pol=%u/%u stop=%u/%u level=%u/%u\n",
             ch, chan->pos_pin, chan->neg_pin, chan->threshold_low,
             chan->threshold_high, chan->active, chan->enabled,
             chan->pos_output, chan->neg_output, chan->pos_polarity_high,
             chan->neg_polarity_high, chan->pos_stop_active,
             chan->neg_stop_active, chan->pos_level, chan->neg_level);
    }
}

static bool diag_clean(const struct bl616cl_pwm_test_diag_s *diag)
{
  int ch;

  for (ch = 0; ch < BL616CL_PWM_TEST_NCHANNELS; ch++)
    {
      if (diag->channels[ch].enabled || diag->channels[ch].active)
        {
          return false;
        }
    }

  return !diag->pin_acquired && !diag->clock_enabled && !diag->started;
}

static int expect_ioctl_errno(int fd, int command, int expected,
                              const char *operation)
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

static int expect_set_errno(int fd, const struct pwm_point_s *point,
                            int expected, const char *operation)
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

static int verify_clean(const char *operation)
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

static int verify_timebase(uint32_t frequency,
                           const struct bl616cl_pwm_test_diag_s *diag)
{
  uint64_t product;
  uint64_t scaled;
  uint32_t calculated_frequency;
  uint32_t frequency_error;

  if (diag->source_frequency == 0 || diag->divider == 0 ||
      diag->period < 2 || !diag->pin_acquired || !diag->clock_enabled ||
      !diag->started)
    {
      print_diag("invalid register state", diag);
      return -EIO;
    }

  /* The divider must be the smallest one whose period fits in 16 bits, for
   * the finest duty steps, and the period must be rounded to the request.
   */

  scaled = (uint64_t)frequency * diag->divider;
  if (diag->source_frequency > scaled * UINT16_MAX ||
      (diag->divider > 1 && diag->source_frequency <=
                            (scaled - frequency) * UINT16_MAX) ||
      diag->period != (diag->source_frequency + scaled / 2) / scaled)
    {
      print_diag("divider is not the smallest that fits", diag);
      return -ERANGE;
    }

  product = (uint64_t)diag->divider * diag->period;
  calculated_frequency =
    (uint32_t)((diag->source_frequency + product / 2) / product);
  frequency_error = absolute_difference(calculated_frequency, frequency);
  if ((uint64_t)frequency_error * 100 > frequency)
    {
      print_diag("frequency out of tolerance", diag);
      return -ERANGE;
    }

  return 0;
}

/* A running channel must hold the threshold of its duty, the polarity and
 * stop state of its cpol/dcpol, and output enables for exactly the board
 * pins. The negative output uses the same polarity and the opposite stop
 * state, which makes it the complement of the positive output.
 */

static int verify_channel(int ch, ub16_t duty, uint8_t cpol, uint8_t dcpol,
                          const struct bl616cl_pwm_test_diag_s *diag)
{
  const struct bl616cl_pwm_test_channel_s *chan = &diag->channels[ch];
  uint32_t expected_delta;
  uint32_t actual_delta;
  bool stop_active;

  if (!chan->configured || !chan->active || !chan->enabled ||
      chan->threshold_low > chan->threshold_high ||
      chan->threshold_high >= diag->period ||
      chan->pos_output != (chan->pos_pin >= 0) ||
      chan->neg_output != (chan->neg_pin >= 0))
    {
      print_diag("invalid channel state", diag);
      printf("  FAIL: channel %d is not running on its board pins\n", ch);
      return -EIO;
    }

  actual_delta = chan->threshold_high - chan->threshold_low;
  if (duty == DUTY_MAX)
    {
      expected_delta = diag->period - 1;
    }
  else
    {
      expected_delta =
        (uint32_t)(((uint64_t)diag->period * duty + 32768) >> 16);
      if (expected_delta >= diag->period)
        {
          expected_delta = diag->period - 1;
        }
    }

  if (absolute_difference(actual_delta, expected_delta) > 1)
    {
      print_diag("duty threshold mismatch", diag);
      printf("  FAIL: channel %d duty=%lu\n", ch, (unsigned long)duty);
      return -ERANGE;
    }

  stop_active = (cpol == PWM_CPOL_HIGH && dcpol == PWM_DCPOL_HIGH) ||
                (cpol == PWM_CPOL_LOW && dcpol == PWM_DCPOL_LOW);
  if (chan->cpol != cpol || chan->dcpol != dcpol ||
      chan->pos_polarity_high != (cpol == PWM_CPOL_HIGH) ||
      chan->neg_polarity_high != chan->pos_polarity_high ||
      chan->pos_stop_active != stop_active ||
      chan->neg_stop_active == stop_active)
    {
      print_diag("polarity mapping mismatch", diag);
      printf("  FAIL: channel %d cpol=%u dcpol=%u\n", ch, cpol, dcpol);
      return -EIO;
    }

  return 0;
}

static int verify_point(const struct pwm_point_s *point,
                        const struct bl616cl_pwm_test_diag_s *diag,
                        bool verbose)
{
  int ret;

  ret = verify_timebase(point->frequency, diag);
  if (ret < 0)
    {
      return ret;
    }

  ret = verify_channel(g_primary, point->duty, point->cpol, point->dcpol,
                       diag);
  if (ret < 0)
    {
      return ret;
    }

  if (verbose)
    {
      print_diag("verified", diag);
    }

  return 0;
}

/* After STOP every board pin is a plain GPIO output again: the positive
 * output idles at the dcpol level of its channel and the negative output at
 * the complement. The GPIO input register follows the pin in this mode.
 */

static int verify_idle_levels(const char *label)
{
  const struct bl616cl_pwm_test_channel_s *chan;
  struct bl616cl_pwm_test_diag_s diag;
  bool high;
  int ch;

  if (get_diag(&diag) < 0)
    {
      return -EIO;
    }

  for (ch = 0; ch < BL616CL_PWM_TEST_NCHANNELS; ch++)
    {
      chan = &diag.channels[ch];
      if (!chan->configured)
        {
          continue;
        }

      high = chan->dcpol == PWM_DCPOL_HIGH;
      if ((chan->pos_pin >= 0 && chan->pos_level != high) ||
          (chan->neg_pin >= 0 && chan->neg_level == high))
        {
          print_diag(label, &diag);
          printf("  FAIL: %s channel %d idle levels do not match dcpol\n",
                 label, ch);
          return -EIO;
        }
    }

  return 0;
}

static int set_start_verify(int fd, const struct pwm_point_s *point,
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

static int run_case_001(const struct app_config_s *cfg)
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
      INFO_DUTY(&info) != point.duty || get_diag(&diag) < 0 ||
      !diag.started ||
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

static int check_stopped_invalid(int fd, const struct pwm_point_s *point,
                                 int expected, const char *label)
{
  struct pwm_info_s info;

  if (pwm_set(fd, point) < 0 || pwm_get(fd, &info) < 0 ||
      info.frequency != point->frequency ||
      INFO_CPOL(&info) != point->cpol || INFO_DCPOL(&info) != point->dcpol)
    {
      printf("  FAIL: stopped SET/GET %s did not preserve upper request\n",
             label);
      return -EIO;
    }

  return expect_ioctl_errno(fd, PWMIOC_START, expected, label);
}

static int run_case_002(const struct app_config_s *cfg)
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

  const char *labels[] = {
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

static int run_points(const struct app_config_s *cfg,
                      const struct pwm_point_s *points, size_t count,
                      const char *case_id)
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

/* The lower half must reuse its divider and period while the frequency
 * stays the same and solve again only when it changes.
 */

static int expect_solve_calls(const char *case_id, uint32_t expected)
{
  struct bl616cl_pwm_test_diag_s diag;

  if (get_diag(&diag) < 0)
    {
      return -EIO;
    }

  if (diag.solve_calls != expected)
    {
      printf("  FAIL: %s solved the divider %lu times, expected %lu\n",
             case_id, (unsigned long)diag.solve_calls,
             (unsigned long)expected);
      return -EIO;
    }

  return 0;
}

static int run_case_003(const struct app_config_s *cfg)
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
                 "PWM-003") < 0 ||
      expect_solve_calls("PWM-003", 3) < 0)
    {
      return -EIO;
    }

  printf("  [PWM-003] PASS software frequency and threshold contract\n\n");
  return 0;
}

static int run_case_004(const struct app_config_s *cfg)
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
                 "PWM-004") < 0 ||
      expect_solve_calls("PWM-004", 1) < 0)
    {
      return -EIO;
    }

  printf("  [PWM-004] PASS zero, intermediate and 65535/65536 duty\n\n");
  return 0;
}

static int run_case_005(const struct app_config_s *cfg)
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

  printf("[PWM-005] CPOL/DCPOL mapping and stopped levels\n");
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
          diag.channels[g_primary].enabled || diag.started)
        {
          printf("  FAIL: STOP did not disable combination %d\n", i + 1);
          goto out;
        }

      if (verify_idle_levels("PWM-005 STOP") < 0)
        {
          goto out;
        }
    }

  close(fd);
  if (verify_clean("PWM-005 close") < 0)
    {
      return -EIO;
    }

  printf("  [PWM-005] PASS CPOL/DCPOL registers and stopped levels\n\n");
  return 0;

out:
  (void)pwm_stop(fd);
  close(fd);
  return ret;
}

static int run_case_006(const struct app_config_s *cfg)
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
      d2.period != d1.period ||
      d2.channels[g_primary].threshold_high ==
        d1.channels[g_primary].threshold_high)
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

static int expect_fault_cleanup(const char *label, int expected_error)
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

static int run_case_007(const struct app_config_s *cfg)
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

static int expect_missing(const char *path)
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

/* Every board PWM pin belongs to /dev/pwm0, so none of them may also be
 * registered as a GPIO test device.
 */

static int expect_pins_owned(const struct bl616cl_pwm_test_diag_s *diag)
{
  const struct bl616cl_pwm_test_channel_s *chan;
  char path[16];
  int ch;

  for (ch = 0; ch < BL616CL_PWM_TEST_NCHANNELS; ch++)
    {
      chan = &diag->channels[ch];
      if (!chan->configured)
        {
          continue;
        }

      if (chan->pos_pin >= 0)
        {
          snprintf(path, sizeof(path), "/dev/gpio%d", chan->pos_pin);
          if (expect_missing(path) < 0)
            {
              return -EIO;
            }
        }

      if (chan->neg_pin >= 0)
        {
          snprintf(path, sizeof(path), "/dev/gpio%d", chan->neg_pin);
          if (expect_missing(path) < 0)
            {
              return -EIO;
            }
        }
    }

  return 0;
}

/* The GLB pin layout must be differential exactly when some channel has a
 * negative pin. Positive-only boards get the single-ended layout, which
 * offers twice as many pins per channel.
 */

static int expect_pin_layout(void)
{
  struct bl616cl_pwm_test_diag_s diag;
  bool diff_end = false;
  int ch;

  if (get_diag(&diag) < 0)
    {
      return -EIO;
    }

  for (ch = 0; ch < BL616CL_PWM_TEST_NCHANNELS; ch++)
    {
      if (diag.channels[ch].configured && diag.channels[ch].neg_pin >= 0)
        {
          diff_end = true;
        }
    }

  if (diag.io_sel_diff_end != diff_end)
    {
      printf("  FAIL: pin layout io_sel=%u, expected %u\n",
             diag.io_sel_diff_end, diff_end);
      return -EIO;
    }

  printf("  pin layout: %s\n", diff_end ? "differential (pin % 8)" :
                                          "single-ended (pin % 4)");
  return 0;
}

static int run_case_008(const struct app_config_s *cfg)
{
  /* clang-format off */

  const struct pwm_point_s point =
  {
    1000, DUTY_50, PWM_CPOL_HIGH, PWM_DCPOL_LOW
  };

  /* clang-format on */

  struct bl616cl_pwm_test_diag_s diag;
  int fd;

  printf("[PWM-008] Device nodes, pin ownership and cleanup\n");
  bl616cl_pwm_test_reset();
  fd = pwm_open(cfg->devpath);
  if (fd < 0)
    {
      return fd;
    }

  if (get_diag(&diag) < 0 || expect_missing("/dev/pwm1") < 0 ||
      expect_pins_owned(&diag) < 0 ||
      set_start_verify(fd, &point, false, cfg->verbose) < 0 ||
      expect_pin_layout() < 0 || pwm_stop(fd) < 0)
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

  printf("  nodes: pwm0=yes pwm1=no gpio for PWM pins=no\n");
  printf("  [PWM-008] PASS runtime owner and cleanup contract\n\n");
  return 0;
}

#ifdef CONFIG_PWM_MULTICHAN
/* Send one request with the given channel entries. The list ends with -1
 * when there is room, as the upper half expects.
 */

static int pwm_set_channels(int fd, uint32_t frequency,
                            const struct pwm_chan_s *chans, int nchans)
{
  struct pwm_info_s info;
  int i;

  memset(&info, 0, sizeof(info));
  info.frequency = frequency;
  for (i = 0; i < nchans && i < CONFIG_PWM_NCHANNELS; i++)
    {
      info.channels[i] = chans[i];
    }

  if (i < CONFIG_PWM_NCHANNELS)
    {
      info.channels[i].channel = -1;
    }

  if (ioctl(fd, PWMIOC_SETCHARACTERISTICS,
            (unsigned long)(uintptr_t)&info) < 0)
    {
      return -errno;
    }

  return 0;
}

static int verify_channels(uint32_t frequency, const struct pwm_chan_s *chans,
                           int nchans, bool verbose,
                           struct bl616cl_pwm_test_diag_s *diag)
{
  int i;

  if (get_diag(diag) < 0 || verify_timebase(frequency, diag) < 0)
    {
      return -EIO;
    }

  for (i = 0; i < nchans; i++)
    {
      if (verify_channel(chans[i].channel - 1, chans[i].duty, chans[i].cpol,
                         chans[i].dcpol, diag) < 0)
        {
          return -EIO;
        }
    }

  if (verbose)
    {
      print_diag("verified", diag);
    }

  return 0;
}

/* A rejected request must not touch the running outputs. */

static int expect_rejected(int fd, const struct pwm_chan_s *chans,
                           int nchans, const char *label,
                           const struct bl616cl_pwm_test_diag_s *before)
{
  struct bl616cl_pwm_test_diag_s after;
  int ret;
  int ch;

  ret = pwm_set_channels(fd, 1000, chans, nchans);
  if (ret != -EINVAL || get_diag(&after) < 0 || !after.started ||
      after.divider != before->divider || after.period != before->period)
    {
      printf("  FAIL: %s ret=%d expected=%d\n", label, ret, -EINVAL);
      return -EIO;
    }

  for (ch = 0; ch < BL616CL_PWM_TEST_NCHANNELS; ch++)
    {
      if (after.channels[ch].active != before->channels[ch].active ||
          after.channels[ch].threshold_high !=
            before->channels[ch].threshold_high)
        {
          print_diag(label, &after);
          printf("  FAIL: %s changed channel %d\n", label, ch);
          return -EIO;
        }
    }

  printf("  rejected: %s errno=%d\n", label, EINVAL);
  return 0;
}
#endif

static int run_case_009(const struct app_config_s *cfg)
{
#ifndef CONFIG_PWM_MULTICHAN
  (void)cfg;
  printf("[PWM-009] Multichannel and complementary outputs\n");
  printf("  SKIP: CONFIG_PWM_MULTICHAN is disabled\n\n");
  return CASE_SKIPPED;
#else
  struct pwm_chan_s chans[BL616CL_PWM_TEST_NCHANNELS];
  struct pwm_chan_s req[2];
  struct bl616cl_pwm_test_diag_s before;
  struct bl616cl_pwm_test_diag_s diag;
  int unconfigured = -1;
  int nchans = 0;
  int last;
  int fd;
  int ch;
  int i;
  int ret = -EIO;

  printf("[PWM-009] Multichannel and complementary outputs\n");
  bl616cl_pwm_test_reset();
  if (get_diag(&diag) < 0)
    {
      return -EIO;
    }

  /* 20%, 40%, 60% and 80% in channel order, so that every output can be
   * told apart on a logic analyzer.
   */

  memset(chans, 0, sizeof(chans));
  for (ch = 0; ch < BL616CL_PWM_TEST_NCHANNELS; ch++)
    {
      if (!diag.channels[ch].configured)
        {
          unconfigured = ch;
          continue;
        }

      if (nchans < CONFIG_PWM_NCHANNELS)
        {
          chans[nchans].channel = ch + 1;
          chans[nchans].duty = (ub16_t)(13107 * (nchans + 1));
          chans[nchans].cpol = PWM_CPOL_HIGH;
          chans[nchans].dcpol = PWM_DCPOL_HIGH;
          nchans++;
        }
    }

  if (nchans < 2)
    {
      printf("  SKIP: needs two configured channels and "
             "CONFIG_PWM_NCHANNELS >= 2\n\n");
      return CASE_SKIPPED;
    }

  last = nchans - 1;
  fd = pwm_open(cfg->devpath);
  if (fd < 0)
    {
      return fd;
    }

  /* 1. With dcpol HIGH, STOP leaves positive outputs high and negative
   * outputs low.
   */

  if (pwm_set_channels(fd, 1000, chans, nchans) < 0 || pwm_start(fd) < 0 ||
      verify_channels(1000, chans, nchans, cfg->verbose, &diag) < 0 ||
      pwm_stop(fd) < 0 || verify_idle_levels("PWM-009 STOP high") < 0)
    {
      printf("  FAIL: dcpol HIGH start/stop\n");
      goto out;
    }

  /* 2. All channels start together on one timebase. */

  for (i = 0; i < nchans; i++)
    {
      chans[i].dcpol = PWM_DCPOL_LOW;
    }

  if (pwm_set_channels(fd, 1000, chans, nchans) < 0 || pwm_start(fd) < 0 ||
      verify_channels(1000, chans, nchans, cfg->verbose, &diag) < 0)
    {
      printf("  FAIL: all-channel start\n");
      goto out;
    }

  for (i = 0; i < nchans; i++)
    {
      ch = chans[i].channel - 1;
      printf("  channel %d: 1000Hz duty=%lu/65536 pins=%d/%d\n", ch,
             (unsigned long)chans[i].duty, diag.channels[ch].pos_pin,
             diag.channels[ch].neg_pin);
    }

  if (cfg->hold_s > 0)
    {
      sleep(cfg->hold_s);
    }

  /* 3. An entry with channel 0 is skipped; updating one channel leaves the
   * others and the timebase untouched.
   */

  if (get_diag(&before) < 0)
    {
      goto out;
    }

  memset(req, 0, sizeof(req));
  chans[last].duty = (ub16_t)58982;
  req[1] = chans[last];
  if (pwm_set_channels(fd, 1000, req, 2) < 0 ||
      verify_channels(1000, chans, nchans, cfg->verbose, &diag) < 0 ||
      diag.solve_calls != before.solve_calls)
    {
      printf("  FAIL: single-channel update\n");
      goto out;
    }

  for (i = 0; i < last; i++)
    {
      ch = chans[i].channel - 1;
      if (diag.channels[ch].threshold_high !=
          before.channels[ch].threshold_high)
        {
          printf("  FAIL: updating channel %d changed channel %d\n",
                 chans[last].channel - 1, ch);
          goto out;
        }
    }

  printf("  channel %d: duty=%lu/65536, other channels unchanged\n",
         chans[last].channel - 1, (unsigned long)chans[last].duty);
  if (cfg->hold_s > 0)
    {
      sleep(cfg->hold_s);
    }

  /* 4. A new frequency rebuilds the timebase and keeps running the
   * channels that the request does not mention.
   */

  chans[0].duty = DUTY_50;
  if (pwm_set_channels(fd, 10000, chans, 1) < 0 ||
      verify_channels(10000, chans, nchans, cfg->verbose, &diag) < 0 ||
      diag.divider == before.divider)
    {
      printf("  FAIL: frequency change with one channel\n");
      goto out;
    }

  printf("  10000Hz via channel %d: divider %u->%u, all channels kept\n",
         chans[0].channel - 1, before.divider, diag.divider);
  if (cfg->hold_s > 0)
    {
      sleep(cfg->hold_s);
    }

  /* 5. Unknown, unconfigured, repeated and missing channels are rejected
   * before the hardware changes.
   */

  if (get_diag(&before) < 0)
    {
      goto out;
    }

  req[0] = chans[0];
  req[0].channel = BL616CL_PWM_TEST_NCHANNELS + 1;
  if (expect_rejected(fd, req, 1, "unknown channel", &before) < 0)
    {
      goto out;
    }

  if (unconfigured >= 0)
    {
      req[0].channel = unconfigured + 1;
      if (expect_rejected(fd, req, 1, "unconfigured channel", &before) < 0)
        {
          goto out;
        }
    }
  else
    {
      printf("  unconfigured channel: SKIP (all channels configured)\n");
    }

  req[0] = chans[0];
  req[1] = chans[0];
  if (expect_rejected(fd, req, 2, "repeated channel", &before) < 0 ||
      expect_rejected(fd, req, 0, "no channel", &before) < 0)
    {
      goto out;
    }

  /* 6. STOP leaves positive outputs at dcpol LOW and negative outputs high.
   */

  if (pwm_stop(fd) < 0 || verify_idle_levels("PWM-009 STOP low") < 0)
    {
      goto out;
    }

  if (cfg->hold_s > 0)
    {
      sleep(cfg->hold_s);
    }

  close(fd);
  if (verify_clean("PWM-009 close") < 0)
    {
      return -EIO;
    }

  printf("  [PWM-009] PASS %d channels, updates, errno and idle levels\n\n",
         nchans);
  return 0;

out:
  (void)pwm_stop(fd);
  close(fd);
  return ret;
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
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
    { "009", run_case_009 },
  };

  struct bl616cl_pwm_test_diag_s diag;
  struct app_config_s cfg;
  bool found = false;
  int executed = 0;
  int passed = 0;
  int skipped = 0;
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
  printf("Case: %s Device: %s hold=%ds\n",
         cfg.case_id, cfg.devpath, cfg.hold_s);

  if (get_diag(&diag) < 0)
    {
      return ERROR;
    }

  g_primary = -1;
  for (i = 0; i < BL616CL_PWM_TEST_NCHANNELS; i++)
    {
      if (!diag.channels[i].configured)
        {
          continue;
        }

      if (g_primary < 0)
        {
          g_primary = i;
        }

      printf("Channel %d: positive GPIO%d negative GPIO%d\n", i,
             diag.channels[i].pos_pin, diag.channels[i].neg_pin);
    }

  if (g_primary < 0)
    {
      printf("No PWM channel is configured\n");
      return ERROR;
    }

  printf("Primary channel for 001-008: %d\n", g_primary);

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
      if (ret == CASE_SKIPPED)
        {
          skipped++;
        }
      else if (ret >= 0)
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
  printf("PWM Summary: executed=%d passed=%d skipped=%d failed=%d -> %s\n",
         executed, passed, skipped, executed - passed - skipped,
         executed == passed + skipped ? "PASS" : "FAIL");

  return executed == passed + skipped ? OK : ERROR;
}
