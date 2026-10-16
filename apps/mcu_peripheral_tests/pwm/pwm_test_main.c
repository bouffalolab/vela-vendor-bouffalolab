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
#include <nuttx/ioexpander/gpio.h>
#include <nuttx/timers/pwm.h>

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

/* Reads of the loopback GPIO that must see both levels of a running 1 kHz
 * output. One read takes a few microseconds, so this spans many periods.
 */

#define LOOPBACK_READS      10000

/* PWM0 has four channels. The board configuration selects the channels and
 * their output pins; -1 marks an unused output.
 */

#define PWM_NCHANNELS       4

#ifdef CONFIG_AI_M64P_KIT_PWM_CH0
#  define PWM_CH0_POS       CONFIG_AI_M64P_KIT_PWM_CH0_POS_PIN
#  define PWM_CH0_NEG       CONFIG_AI_M64P_KIT_PWM_CH0_NEG_PIN
#else
#  define PWM_CH0_POS       -1
#  define PWM_CH0_NEG       -1
#endif

#ifdef CONFIG_AI_M64P_KIT_PWM_CH1
#  define PWM_CH1_POS       CONFIG_AI_M64P_KIT_PWM_CH1_POS_PIN
#  define PWM_CH1_NEG       CONFIG_AI_M64P_KIT_PWM_CH1_NEG_PIN
#else
#  define PWM_CH1_POS       -1
#  define PWM_CH1_NEG       -1
#endif

#ifdef CONFIG_AI_M64P_KIT_PWM_CH2
#  define PWM_CH2_POS       CONFIG_AI_M64P_KIT_PWM_CH2_POS_PIN
#  define PWM_CH2_NEG       CONFIG_AI_M64P_KIT_PWM_CH2_NEG_PIN
#else
#  define PWM_CH2_POS       -1
#  define PWM_CH2_NEG       -1
#endif

#ifdef CONFIG_AI_M64P_KIT_PWM_CH3
#  define PWM_CH3_POS       CONFIG_AI_M64P_KIT_PWM_CH3_POS_PIN
#  define PWM_CH3_NEG       CONFIG_AI_M64P_KIT_PWM_CH3_NEG_PIN
#else
#  define PWM_CH3_POS       -1
#  define PWM_CH3_NEG       -1
#endif

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
  const char *gpio;             /* Loopback input, NULL if not wired */
  int hold_s;
};

struct pwm_point_s
{
  uint32_t frequency;
  ub16_t duty;
  uint8_t cpol;
  uint8_t dcpol;
};

struct pwm_pins_s
{
  int8_t pos;
  int8_t neg;
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

static const struct pwm_pins_s g_pins[PWM_NCHANNELS] =
{
  { PWM_CH0_POS, PWM_CH0_NEG },
  { PWM_CH1_POS, PWM_CH1_NEG },
  { PWM_CH2_POS, PWM_CH2_NEG },
  { PWM_CH3_POS, PWM_CH3_NEG },
};

/* Cases 001-008 drive the primary channel, the lowest channel the board
 * configured. The loopback GPIO reads its positive output, or its negative
 * output when the channel has no positive one.
 */

static int g_primary = -1;

/* Level checks skipped because no loopback GPIO was given */

static int g_level_skips;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void print_usage(const char *progname)
{
  printf("Usage: %s [-c id|all] [-d dev] [-g gpio] [-w seconds]\n",
         progname);
  printf("  -c <id>  Case 001-006, 008, 009 or all (default: all)\n");
  printf("  -d <dev> PWM device (default: %s)\n", DEFAULT_PWM_DEVPATH);
  printf("  -g <dev> GPIO input wired to the primary channel output;\n"
         "           without it the level checks are skipped\n");
  printf("  -w <s>   Waveform observation hold per point (default: %d)\n",
         DEFAULT_HOLD_S);
}

static bool channel_configured(int ch)
{
  return g_pins[ch].pos >= 0 || g_pins[ch].neg >= 0;
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

static void pwm_fill(const struct pwm_point_s *point,
                     struct pwm_info_s *info)
{
  memset(info, 0, sizeof(*info));
  info->frequency = point->frequency;
#ifdef CONFIG_PWM_MULTICHAN
  info->channels[0].channel = g_primary + 1;
#endif
  INFO_DUTY(info) = point->duty;
  INFO_CPOL(info) = point->cpol;
  INFO_DCPOL(info) = point->dcpol;
}

static int pwm_set_info(int fd, const struct pwm_info_s *info)
{
  if (ioctl(fd, PWMIOC_SETCHARACTERISTICS,
            (unsigned long)(uintptr_t)info) < 0)
    {
      return -errno;
    }

  return 0;
}

static int pwm_set(int fd, const struct pwm_point_s *point)
{
  struct pwm_info_s info;

  pwm_fill(point, &info);
  return pwm_set_info(fd, &info);
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

/* GETCHARACTERISTICS returns the request that the last SET stored, whether
 * the output runs or not.
 */

static int verify_get(int fd, const struct pwm_info_s *expected,
                      const char *label)
{
  struct pwm_info_s info;
  int ret;

  ret = pwm_get(fd, &info);
  if (ret < 0 || memcmp(&info, expected, sizeof(info)) != 0)
    {
      printf("  FAIL: %s GET ret=%d does not match the request\n",
             label, ret);
      return -EIO;
    }

  return 0;
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

#ifdef CONFIG_DEV_GPIO
static int loopback_open(const char *path)
{
  int fd;
  int ret;

  fd = open(path, O_RDWR);
  if (fd < 0)
    {
      return -errno;
    }

  /* An input never drives against the PWM output it is wired to */

  if (ioctl(fd, GPIOC_SETPINTYPE, (unsigned long)GPIO_INPUT_PIN) < 0)
    {
      ret = -errno;
      close(fd);
      return ret;
    }

  return fd;
}

static int loopback_read(int fd, bool *level)
{
  if (ioctl(fd, GPIOC_READ, (unsigned long)(uintptr_t)level) < 0)
    {
      return -errno;
    }

  return 0;
}
#else
static int loopback_open(const char *path)
{
  return -ENOSYS;
}

static int loopback_read(int fd, bool *level)
{
  return -ENOSYS;
}
#endif

/* After STOP the pins of a channel are plain GPIO outputs: the positive
 * output idles at the dcpol level and the negative output at the
 * complement. The loopback GPIO reads the primary output from outside the
 * PWM driver.
 */

static int check_idle_level(const struct app_config_s *cfg, uint8_t dcpol,
                            const char *label)
{
  bool expected;
  bool level = false;
  int ret;
  int fd;

  if (cfg->gpio == NULL)
    {
      printf("  idle level %s: SKIP (no loopback GPIO, use -g)\n", label);
      g_level_skips++;
      return 0;
    }

  expected = (dcpol == PWM_DCPOL_HIGH) == (g_pins[g_primary].pos >= 0);
  fd = loopback_open(cfg->gpio);
  ret = fd < 0 ? fd : loopback_read(fd, &level);
  if (fd >= 0)
    {
      close(fd);
    }

  if (ret < 0)
    {
      printf("  FAIL: idle level %s: read %s ret=%d\n",
             label, cfg->gpio, ret);
      return ret;
    }

  if (level != expected)
    {
      printf("  FAIL: idle level %s: %s=%u expected=%u\n",
             label, cfg->gpio, level, expected);
      return -EIO;
    }

  printf("  idle level %s: %u PASS\n", label, level);
  return 0;
}

/* A running primary output toggles, so the loopback GPIO must see both
 * levels. This tells a started output from a START that returned 0 without
 * starting the lower half.
 */

static int check_running(const struct app_config_s *cfg, const char *label)
{
  bool seen[2] =
  {
    false, false
  };

  bool level = false;
  int ret = 0;
  int fd;
  int i;

  if (cfg->gpio == NULL)
    {
      printf("  running %s: SKIP (no loopback GPIO, use -g)\n", label);
      g_level_skips++;
      return 0;
    }

  fd = loopback_open(cfg->gpio);
  if (fd < 0)
    {
      printf("  FAIL: running %s: open %s ret=%d\n", label, cfg->gpio, fd);
      return fd;
    }

  for (i = 0; i < LOOPBACK_READS && !(seen[0] && seen[1]); i++)
    {
      ret = loopback_read(fd, &level);
      if (ret < 0)
        {
          break;
        }

      seen[level] = true;
    }

  close(fd);
  if (ret < 0)
    {
      printf("  FAIL: running %s: read %s ret=%d\n",
             label, cfg->gpio, ret);
      return ret;
    }

  if (!seen[0] || !seen[1])
    {
      printf("  FAIL: running %s: %s stayed %u for %d reads\n",
             label, cfg->gpio, level, LOOPBACK_READS);
      return -EIO;
    }

  printf("  running %s: output toggles PASS\n", label);
  return 0;
}

static int set_start_verify(int fd, const struct pwm_point_s *point,
                            bool already_started)
{
  struct pwm_info_s info;
  int ret;

  pwm_fill(point, &info);
  ret = pwm_set_info(fd, &info);
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

  return verify_get(fd, &info, "SET");
}

static int run_case_001(const struct app_config_s *cfg)
{
  /* clang-format off */

  const struct pwm_point_s point =
  {
    1000, DUTY_50, PWM_CPOL_HIGH, PWM_DCPOL_LOW
  };

  /* clang-format on */

  struct pwm_info_s expected;
  int fd1 = -1;
  int fd2 = -1;
  int ret = -EIO;

  printf("[PWM-001] Open/close and multi-fd lifecycle\n");
  pwm_fill(&point, &expected);

  fd1 = pwm_open(cfg->devpath);
  if (fd1 < 0)
    {
      goto out;
    }

  fd2 = pwm_open(cfg->devpath);
  if (fd2 < 0)
    {
      goto out;
    }

  if (set_start_verify(fd1, &point, false) < 0)
    {
      goto out;
    }

  close(fd1);
  fd1 = -1;
  if (verify_get(fd2, &expected, "after first close") < 0 ||
      check_running(cfg, "after first close") < 0)
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

  /* Closing the last descriptor while the output runs shuts the lower half
   * down. The upper half keeps the characteristics, so a new descriptor
   * starts again without SET.
   */

  fd1 = pwm_open(cfg->devpath);
  if (fd1 < 0 || pwm_start(fd1) < 0)
    {
      printf("  FAIL: START before active close\n");
      goto out;
    }

  close(fd1);
  fd1 = pwm_open(cfg->devpath);
  if (fd1 < 0 || pwm_start(fd1) < 0 ||
      verify_get(fd1, &expected, "after active close") < 0 ||
      check_running(cfg, "after active close") < 0 ||
      pwm_stop(fd1) < 0)
    {
      printf("  FAIL: active close/reopen did not restart\n");
      goto out;
    }

  close(fd1);
  printf("  [PWM-001] PASS lifecycle and shared state%s\n\n",
         cfg->gpio != NULL ? ", output running" : ", running SKIP");
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

  struct pwm_info_s info;
  int fd;
  int ret = -EIO;
  int i;

  printf("[PWM-002] Missing characteristics and invalid requests\n");
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

  if (set_start_verify(fd, &valid, false) < 0)
    {
      goto out;
    }

  if (expect_set_errno(fd, &invalid[0], EINVAL,
                       "running SET zero frequency") < 0 ||
      pwm_get(fd, &info) < 0 || info.frequency != 0)
    {
      printf("  FAIL: invalid running SET was not kept by upper GET\n");
      goto out;
    }

  if (set_start_verify(fd, &valid, true) < 0 || pwm_stop(fd) < 0)
    {
      goto out;
    }

  close(fd);
  printf("  [PWM-002] PASS errno and upper overwrite\n\n");
  return 0;

out:
  (void)pwm_stop(fd);
  close(fd);
  return ret;
}

static int run_points(const struct app_config_s *cfg,
                      const struct pwm_point_s *points, size_t count)
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
      if (set_start_verify(fd, &points[i], i != 0) < 0)
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
  return 0;

out:
  (void)pwm_stop(fd);
  close(fd);
  return ret;
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

  printf("[PWM-003] Frequency and duty points\n");
  if (run_points(cfg, points, sizeof(points) / sizeof(points[0])) < 0)
    {
      return -EIO;
    }

  printf("  [PWM-003] PASS SET/START/GET at every point\n\n");
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
  if (run_points(cfg, points, sizeof(points) / sizeof(points[0])) < 0)
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

  char label[16];
  int fd;
  int ret = -EIO;
  int i;

  printf("[PWM-005] CPOL/DCPOL mapping and stopped levels\n");
  fd = pwm_open(cfg->devpath);
  if (fd < 0)
    {
      return fd;
    }

  for (i = 0; i < (int)(sizeof(points) / sizeof(points[0])); i++)
    {
      if (set_start_verify(fd, &points[i], false) < 0)
        {
          goto out;
        }

      printf("  combination %d: cpol=%u dcpol=%u\n", i + 1,
             points[i].cpol, points[i].dcpol);
      if (cfg->hold_s > 0)
        {
          sleep(cfg->hold_s);
        }

      if (pwm_stop(fd) < 0)
        {
          printf("  FAIL: STOP combination %d\n", i + 1);
          goto out;
        }

      snprintf(label, sizeof(label), "combination %d", i + 1);
      if (check_idle_level(cfg, points[i].dcpol, label) < 0)
        {
          goto out;
        }
    }

  close(fd);
  printf("  [PWM-005] PASS CPOL/DCPOL requests%s\n\n",
         cfg->gpio != NULL ? " and idle levels" : ", idle levels SKIP");
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

  int fd;
  int ret = -EIO;

  printf("[PWM-006] Same-divider and cross-divider updates\n");
  fd = pwm_open(cfg->devpath);
  if (fd < 0)
    {
      return fd;
    }

  if (set_start_verify(fd, &p1, false) < 0)
    {
      goto out;
    }

  if (pwm_start(fd) < 0)
    {
      printf("  FAIL: repeated START\n");
      goto out;
    }

  if (set_start_verify(fd, &p2, true) < 0)
    {
      printf("  FAIL: same-frequency duty update\n");
      goto out;
    }

  if (set_start_verify(fd, &p3, true) < 0 ||
      set_start_verify(fd, &p4, true) < 0)
    {
      printf("  FAIL: cross-frequency update\n");
      goto out;
    }

  if (cfg->hold_s > 0)
    {
      sleep(cfg->hold_s);
    }

  if (pwm_stop(fd) < 0 || pwm_stop(fd) < 0 ||
      set_start_verify(fd, &p1, false) < 0 ||
      pwm_stop(fd) < 0)
    {
      printf("  FAIL: repeated lifecycle did not recover\n");
      goto out;
    }

  close(fd);
  printf("  [PWM-006] PASS update paths and idempotent lifecycle\n\n");
  return 0;

out:
  (void)pwm_stop(fd);
  close(fd);
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

static int expect_pins_owned(void)
{
  char path[16];
  int ch;

  for (ch = 0; ch < PWM_NCHANNELS; ch++)
    {
      if (g_pins[ch].pos >= 0)
        {
          snprintf(path, sizeof(path), "/dev/gpio%d", g_pins[ch].pos);
          if (expect_missing(path) < 0)
            {
              return -EIO;
            }
        }

      if (g_pins[ch].neg >= 0)
        {
          snprintf(path, sizeof(path), "/dev/gpio%d", g_pins[ch].neg);
          if (expect_missing(path) < 0)
            {
              return -EIO;
            }
        }
    }

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

  int fd;

  printf("[PWM-008] Device nodes and pin ownership\n");
  fd = pwm_open(cfg->devpath);
  if (fd < 0)
    {
      return fd;
    }

  if (expect_missing("/dev/pwm1") < 0 || expect_pins_owned() < 0 ||
      set_start_verify(fd, &point, false) < 0 || pwm_stop(fd) < 0)
    {
      (void)pwm_stop(fd);
      close(fd);
      return -EIO;
    }

  close(fd);
  printf("  nodes: pwm0=yes pwm1=no gpio for PWM pins=no\n");
  printf("  [PWM-008] PASS device nodes and pin ownership\n\n");
  return 0;
}

#ifdef CONFIG_PWM_MULTICHAN
/* Build one request with the given channel entries. The list ends with -1
 * when there is room, as the upper half expects.
 */

static void pwm_fill_channels(uint32_t frequency,
                              const struct pwm_chan_s *chans, int nchans,
                              struct pwm_info_s *info)
{
  int i;

  memset(info, 0, sizeof(*info));
  info->frequency = frequency;
  for (i = 0; i < nchans && i < CONFIG_PWM_NCHANNELS; i++)
    {
      info->channels[i] = chans[i];
    }

  if (i < CONFIG_PWM_NCHANNELS)
    {
      info->channels[i].channel = -1;
    }
}

static int set_channels_verify(int fd, uint32_t frequency,
                               const struct pwm_chan_s *chans, int nchans,
                               bool start, const char *label)
{
  struct pwm_info_s info;
  int ret;

  pwm_fill_channels(frequency, chans, nchans, &info);
  ret = pwm_set_info(fd, &info);
  if (ret >= 0 && start)
    {
      ret = pwm_start(fd);
    }

  if (ret < 0)
    {
      printf("  FAIL: %s ret=%d\n", label, ret);
      return ret;
    }

  return verify_get(fd, &info, label);
}

static int expect_rejected(int fd, const struct pwm_chan_s *chans,
                           int nchans, const char *label)
{
  struct pwm_info_s info;
  int ret;

  pwm_fill_channels(1000, chans, nchans, &info);
  ret = pwm_set_info(fd, &info);
  if (ret != -EINVAL)
    {
      printf("  FAIL: %s ret=%d expected=%d\n", label, ret, -EINVAL);
      return -EIO;
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
  struct pwm_chan_s chans[PWM_NCHANNELS];
  struct pwm_chan_s req[2];
  int unconfigured = -1;
  int nchans = 0;
  int last;
  int fd;
  int ch;
  int i;
  int ret = -EIO;

  printf("[PWM-009] Multichannel and complementary outputs\n");

  /* 20%, 40%, 60% and 80% in channel order, so that every output can be
   * told apart on a logic analyzer.
   */

  memset(chans, 0, sizeof(chans));
  for (ch = 0; ch < PWM_NCHANNELS; ch++)
    {
      if (!channel_configured(ch))
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

  if (set_channels_verify(fd, 1000, chans, nchans, true,
                          "dcpol HIGH start") < 0 ||
      pwm_stop(fd) < 0 ||
      check_idle_level(cfg, PWM_DCPOL_HIGH, "dcpol HIGH") < 0)
    {
      printf("  FAIL: dcpol HIGH start/stop\n");
      goto out;
    }

  /* 2. All channels start together on one timebase. */

  for (i = 0; i < nchans; i++)
    {
      chans[i].dcpol = PWM_DCPOL_LOW;
    }

  if (set_channels_verify(fd, 1000, chans, nchans, true,
                          "all-channel start") < 0)
    {
      goto out;
    }

  for (i = 0; i < nchans; i++)
    {
      ch = chans[i].channel - 1;
      printf("  channel %d: 1000Hz duty=%lu/65536 pins=%d/%d\n", ch,
             (unsigned long)chans[i].duty, g_pins[ch].pos, g_pins[ch].neg);
    }

  if (cfg->hold_s > 0)
    {
      sleep(cfg->hold_s);
    }

  /* 3. An entry with channel 0 is skipped; the request updates only the
   * last channel.
   */

  memset(req, 0, sizeof(req));
  chans[last].duty = (ub16_t)58982;
  req[1] = chans[last];
  if (set_channels_verify(fd, 1000, req, 2, false,
                          "single-channel update") < 0)
    {
      goto out;
    }

  printf("  channel %d: duty=%lu/65536\n",
         chans[last].channel - 1, (unsigned long)chans[last].duty);
  if (cfg->hold_s > 0)
    {
      sleep(cfg->hold_s);
    }

  /* 4. A new frequency rebuilds the timebase; the channels that the
   * request does not mention keep running.
   */

  chans[0].duty = DUTY_50;
  if (set_channels_verify(fd, 10000, chans, 1, false,
                          "frequency change with one channel") < 0)
    {
      goto out;
    }

  printf("  10000Hz via channel %d\n", chans[0].channel - 1);
  if (cfg->hold_s > 0)
    {
      sleep(cfg->hold_s);
    }

  /* 5. Unknown, unconfigured, repeated and missing channels are rejected. */

  req[0] = chans[0];
  req[0].channel = PWM_NCHANNELS + 1;
  if (expect_rejected(fd, req, 1, "unknown channel") < 0)
    {
      goto out;
    }

  if (unconfigured >= 0)
    {
      req[0].channel = unconfigured + 1;
      if (expect_rejected(fd, req, 1, "unconfigured channel") < 0)
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
  if (expect_rejected(fd, req, 2, "repeated channel") < 0 ||
      expect_rejected(fd, req, 0, "no channel") < 0)
    {
      goto out;
    }

  /* 6. STOP leaves positive outputs at dcpol LOW and negative outputs high.
   */

  if (pwm_stop(fd) < 0 ||
      check_idle_level(cfg, PWM_DCPOL_LOW, "dcpol LOW") < 0)
    {
      goto out;
    }

  if (cfg->hold_s > 0)
    {
      sleep(cfg->hold_s);
    }

  close(fd);
  printf("  [PWM-009] PASS %d channels, updates and errno%s\n\n", nchans,
         cfg->gpio != NULL ? ", idle levels" : ", idle levels SKIP");
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
    { "008", run_case_008 },
    { "009", run_case_009 },
  };

  struct app_config_s cfg;
  bool found = false;
  int executed = 0;
  int passed = 0;
  int skipped = 0;
  int loop_pin;
  int opt;
  int ret;
  int i;

  cfg.devpath = DEFAULT_PWM_DEVPATH;
  cfg.case_id = "all";
  cfg.gpio = NULL;
  cfg.hold_s = DEFAULT_HOLD_S;

  while ((opt = getopt(argc, argv, "c:d:g:w:h")) != -1)
    {
      switch (opt)
        {
          case 'c':
            cfg.case_id = optarg;
            break;
          case 'd':
            cfg.devpath = optarg;
            break;
          case 'g':
            cfg.gpio = optarg;
            break;
          case 'w':
            cfg.hold_s = atoi(optarg);
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

#ifndef CONFIG_DEV_GPIO
  if (cfg.gpio != NULL)
    {
      printf("-g needs CONFIG_DEV_GPIO\n");
      return ERROR;
    }
#endif

  printf("MCU Peripheral PWM Tests\n");
  printf("Case: %s Device: %s hold=%ds\n",
         cfg.case_id, cfg.devpath, cfg.hold_s);

  g_primary = -1;
  g_level_skips = 0;
  for (i = 0; i < PWM_NCHANNELS; i++)
    {
      if (!channel_configured(i))
        {
          continue;
        }

      if (g_primary < 0)
        {
          g_primary = i;
        }

      printf("Channel %d: positive GPIO%d negative GPIO%d\n", i,
             g_pins[i].pos, g_pins[i].neg);
    }

  printf("Primary channel for 001-008: %d\n", g_primary);
  loop_pin = g_pins[g_primary].pos >= 0 ? g_pins[g_primary].pos :
                                          g_pins[g_primary].neg;
  if (cfg.gpio != NULL)
    {
      printf("Loopback: %s reads GPIO%d (channel %d %s output)\n",
             cfg.gpio, loop_pin, g_primary,
             g_pins[g_primary].pos >= 0 ? "positive" : "negative");
    }
  else
    {
      printf("Loopback: none, level checks are skipped\n");
    }

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

  printf("PWM Summary: executed=%d passed=%d skipped=%d failed=%d "
         "level_checks_skipped=%d -> %s\n",
         executed, passed, skipped, executed - passed - skipped,
         g_level_skips, executed == passed + skipped ? "PASS" : "FAIL");

  return executed == passed + skipped ? OK : ERROR;
}
