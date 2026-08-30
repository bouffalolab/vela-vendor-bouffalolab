/****************************************************************************
 * apps/vendor/bouffalolab/apps/mcu_peripheral_tests/timer/timer_case_main.c
 *
 * MCU Peripheral Timer / PWM Test Cases (TIMER-001 ~ TIMER-010)
 *   TIMER-001  basic counting / overflow period accuracy (/dev/timer0)
 *   TIMER-002  clock prescaler effect        (/dev/timer0 + custom ioctl)
 *   TIMER-003  PWM frequency / duty precision       (/dev/pwm0)
 *   TIMER-004  PWM duty ramp / breathing LED        (/dev/pwm0)
 *   TIMER-005  timer lifecycle and rejected requests (/dev/timer0)
 *
 * 001/002/005 use the selected timer node; 003/004 use /dev/pwm0. 006-010
 * exercise tick, poll, notification lifetime, dual-instance and callback
 * contracts. Timer measurements are software checks; PWM cases need a scope.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <fixedmath.h>
#include <nuttx/clock.h>
#include <nuttx/timers/timer.h>
#include <nuttx/timers/pwm.h>
#include <nuttx/ioexpander/gpio.h>

#include <arch/chip/bl616cl_tim_ioctl.h>
#ifdef CONFIG_BL616CL_TIMER_TEST
#  include <arch/chip/bl616cl_timer_test.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DEFAULT_TIMER_DEVPATH    "/dev/timer0"
#define DEFAULT_PWM_DEVPATH      "/dev/pwm0"
#define DEFAULT_GPIO_DEVPATH     "/dev/gpio1"

#define CASE_001                 "001"
#define CASE_002                 "002"
#define CASE_003                 "003"
#define CASE_004                 "004"
#define CASE_005                 "005"
#define CASE_006                 "006"
#define CASE_007                 "007"
#define CASE_008                 "008"
#define CASE_009                 "009"
#define CASE_010                 "010"
#define CASE_ALL                 "all"

#define TIMER_SIGNO              14

#define DEF_TIMEOUT_US           100000   /* 001: period in us (-t) */
#define DEF_ROUNDS               10       /* 001: measured intervals (-n) */
#define DEF_TOL_PCT              0.5      /* 001: +/- 0.5% tolerance (-e) */

#define DEF_002_COMPARE_US       500000   /* 002: default compare value (-t) */
#define DEF_002_DIV_A            39       /* 002: first divider (-a) */
#define DEF_002_DIV_B            79       /* 002: second divider (-b) */
#define DEF_002_RATIO_TOL_PCT    5.0      /* 002: +/-5% around (b+1)/(a+1) */

#define DEF_BREATH_CYCLES        3        /* 004: breathing cycles (no -n) */
#define DEF_BREATH_STEP_PCT      2        /* 004: duty step in % (-s) */
#define DEF_BREATH_STEP_MS       20       /* 004: step interval in ms (-i) */

#define DEF_PWM_FREQ             1000     /* 004 default / 003 single (-f) */
#define DEF_PWM_DUTY_PCT         50       /* 003: single-point duty (-D) */
#define DEF_PWM_HOLD_S           2        /* 003: per-step hold seconds (-w) */

/* 002 with -g: extra expiries after the measured period, so a scope sees a
 * sustained square wave at each divider (not just 1-2 edges).
 */

#define CASE002_SCOPE_FIRES      6
#define CASE005_TIMEOUT_US       50000

/* The custom timer ioctl BL616CL_TCIOC_SETCLOCKDIV (used by TIMER-002) is
 * defined once in <arch/chip/bl616cl_tim_ioctl.h>, included above, and
 * shared with the chip lower-half driver so the two never drift out of sync.
 */

/* convert percentage (0..100) to b16 duty (0..0xffff) */

#define PCT_TO_DUTY_B16(p) \
  ((ub16_t)((p) >= 100 ? UINT16_MAX : (uint32_t)(p) * 65536u / 100u))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct app_config_s
{
  const char *timer_devpath;
  const char *pwm_devpath;
  const char *gpio_devpath;   /* [001/002] -g toggle output (scope) */
  const char *case_id;
  uint32_t    timeout_us;     /* 001 period / 002 compare value (-t) */
  bool        t_set;          /* -t given (002 uses it vs its default) */
  int         rounds;         /* 001 rounds / 004 cycles (-n) */
  bool        n_set;          /* -n given (004 uses it vs its default) */
  double      tol_pct;        /* 001 tolerance % (-e) */
  int         div_a;          /* 002 first divider (-a) */
  int         div_b;          /* 002 second divider (-b) */
  uint32_t    pwm_freq;       /* 003 single-point / 004 frequency (-f) */
  bool        pwm_freq_set;   /* 003: -f given => single point, not 3 groups */
  unsigned    pwm_duty_pct;   /* 003 single-point duty (-D) */
  int         pwm_hold_s;     /* 003 per-step hold seconds (-w) */
  int         breath_step_pct;
  int         breath_step_ms; /* 004 step interval ms (-i) */
  bool        verbose;
  bool        gpio_toggle;    /* true when -g given: toggle on each expiry */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int timer_set_notification(int fd, bool periodic, int signo);

static void print_usage(const char *progname)
{
  printf("Usage: %s [options]\n", progname);
  printf("Common:\n");
  printf("  -c <id>   Case: 001..010,all (default: all)\n");
  printf("  -d <dev>  Timer device (default: %s)\n", DEFAULT_TIMER_DEVPATH);
  printf("  -p <dev>  PWM device  (default: %s)\n", DEFAULT_PWM_DEVPATH);
  printf("  -g <dev>  [001/002] toggle GPIO each expiry "
         "for scope (e.g. %s)\n",
         DEFAULT_GPIO_DEVPATH);
  printf("  -v        Verbose\n");
  printf("  -h        Help\n");
  printf("[001] basic counting:\n");
  printf("  -t <us>   period microseconds (default: %d)\n", DEF_TIMEOUT_US);
  printf("  -n <num>  measured rounds (default: %d)\n", DEF_ROUNDS);
  printf("  -e <pct>  tolerance percent (default: %.1f)\n", DEF_TOL_PCT);
  printf("[002] prescaler:\n");
  printf("  -t <us>   compare value (default: %d)\n", DEF_002_COMPARE_US);
  printf("  -a <div>  first divider 0..255 (default: %d)\n", DEF_002_DIV_A);
  printf("  -b <div>  second divider 0..255 (default: %d)\n", DEF_002_DIV_B);
  printf("[003] PWM precision:\n");
  printf("  -f <hz>   PWM freq; if set -> single point (else 3 groups)\n");
  printf("  -D <pct>  single-point duty percent (default: %d)\n",
         DEF_PWM_DUTY_PCT);
  printf("  -w <s>    per-step hold seconds (default: %d)\n",
         DEF_PWM_HOLD_S);
  printf("[004] breathing LED:\n");
  printf("  -f <hz>   PWM frequency (default: %d)\n", DEF_PWM_FREQ);
  printf("  -n <num>  breathing cycles (default: %d)\n", DEF_BREATH_CYCLES);
  printf("  -s <pct>  duty step percent (default: %d)\n",
         DEF_BREATH_STEP_PCT);
  printf("  -i <ms>   step interval ms (default: %d)\n", DEF_BREATH_STEP_MS);
  printf("[005] lifecycle/edge requests: no additional options\n");
  printf("[006] tick ioctl conversion: selected timer node\n");
  printf("[007] poll and one-shot notification: selected timer node\n");
  printf("[008] multi-fd shared state: selected timer node\n");
  printf("[009] TIMER0/TIMER1 dual-instance isolation\n");
  printf("[010] raw callback contract (test-only Kconfig)\n");
}

static double mono_now(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ---- /dev/timer0 helpers (used by TIMER-001) ---- */

static int timer_open(const char *devpath)
{
  int fd = open(devpath, O_RDWR);
  if (fd < 0)
    {
      printf("Failed to open %s: errno=%d\n", devpath, errno);
      return -errno;
    }

  return fd;
}

/* Arm /dev/timer0 for periodic SIGEV_SIGNAL notification at timeout_us.
 * The caller must have blocked TIMER_SIGNO so it can be collected with
 * sigtimedwait().
 */

static int timer_arm_notify(int fd, const struct app_config_s *cfg,
                            bool periodic, int signo)
{
  int ret;

  ret = ioctl(fd, TCIOC_SETTIMEOUT, cfg->timeout_us);
  if (ret < 0)
    {
      printf("  Failed: TCIOC_SETTIMEOUT ret=%d errno=%d\n", ret, errno);
      return -errno;
    }

  ret = timer_set_notification(fd, periodic, signo);
  if (ret < 0)
    {
      printf("  Failed: TCIOC_NOTIFICATION ret=%d errno=%d\n", ret, errno);
      return -errno;
    }

  return 0;
}

static int timer_set_notification(int fd, bool periodic, int signo)
{
  struct timer_notify_s notify;
  int ret;

  memset(&notify, 0, sizeof(notify));
  notify.pid                  = getpid();
  notify.periodic             = periodic;
  notify.event.sigev_notify   = SIGEV_SIGNAL;
  notify.event.sigev_signo    = signo;
  notify.event.sigev_value.sival_int = 0;

  ret = ioctl(fd, TCIOC_NOTIFICATION,
              (unsigned long)((uintptr_t)&notify));
  if (ret < 0)
    {
      printf("  Failed: TCIOC_NOTIFICATION ret=%d errno=%d\n", ret, errno);
      return -errno;
    }

  return 0;
}

static int timer_arm_periodic(int fd, const struct app_config_s *cfg)
{
  return timer_arm_notify(fd, cfg, true, TIMER_SIGNO);
}

/* Wait for one timer expiry signal.  Returns 0 on fire, -ETIMEDOUT if the
 * signal did not arrive within (timeout_us + guard).
 */

static int timer_wait_signal(uint32_t timeout_us, sigset_t *set)
{
  struct timespec ts;
  siginfo_t       info;
  uint64_t        budget_us;
  int             ret;

  /* Generous guard: two full periods + 1s, so a healthy timer never trips
   * it but a lost interrupt is caught.
   */

  budget_us  = (uint64_t)timeout_us * 2ull + 1000000ull;
  ts.tv_sec  = budget_us / 1000000ull;
  ts.tv_nsec = (budget_us % 1000000ull) * 1000ull;

  ret = sigtimedwait(set, &info, &ts);
  if (ret < 0)
    {
      return -errno;
    }

  return 0;
}

static int timer_wait_fire(const struct app_config_s *cfg, sigset_t *set)
{
  return timer_wait_signal(cfg->timeout_us, set);
}

/* ---- optional GPIO toggle output (TIMER-001/002 oscilloscope point) ----
 *
 * When '-g <dev>' is given, the test toggles a board output GPIO once per
 * timer expiry, producing a square wave whose period is 2x the timer period.
 * A scope on that pin then measures the timer period independently of the
 * software CLOCK_MONOTONIC check.  Uses the standard NuttX GPIO char driver
 * (GPIOC_WRITE), so the app stays decoupled from the chip/board GPIO code.
 */

static int gpio_out_open(const struct app_config_s *cfg)
{
  int fd;

  if (!cfg->gpio_toggle)
    {
      return -1;
    }

  fd = open(cfg->gpio_devpath, O_RDWR);
  if (fd < 0)
    {
      printf("  WARN: GPIO toggle disabled, open %s failed errno=%d\n",
             cfg->gpio_devpath, errno);
      return -1;
    }

  /* Known-low baseline so the first toggle is a clean rising edge */

  ioctl(fd, GPIOC_WRITE, (unsigned long)0);
  return fd;
}

static void gpio_out_toggle(int fd, bool *level)
{
  if (fd < 0)
    {
      return;
    }

  *level = !*level;
  ioctl(fd, GPIOC_WRITE, (unsigned long)(*level ? 1 : 0));
}

static void gpio_out_close(int fd)
{
  if (fd >= 0)
    {
      ioctl(fd, GPIOC_WRITE, (unsigned long)0);  /* leave pin low */
      close(fd);
    }
}

/****************************************************************************
 * TIMER-001 : basic counting / overflow period accuracy
 ****************************************************************************/

static int run_case_001(int fd, const struct app_config_s *cfg)
{
  struct timer_status_s status;
  sigset_t set;
  double   t_prev;
  double   t_now;
  double   interval_us;
  double   err_us;
  double   tol_us;
  double   max_err_us = 0.0;
  int      gfd;
  bool     glvl = false;
  int      i;
  int      ret;

  printf("[TIMER-001] overflow period accuracy timeout=%luus rounds=%d "
         "tol=%.2f%%\n",
         (unsigned long)cfg->timeout_us, cfg->rounds, cfg->tol_pct);

  tol_us = (double)cfg->timeout_us * cfg->tol_pct / 100.0;

  sigemptyset(&set);
  sigaddset(&set, TIMER_SIGNO);
  sigprocmask(SIG_BLOCK, &set, NULL);

  ret = timer_arm_periodic(fd, cfg);
  if (ret < 0)
    {
      return ret;
    }

  ret = ioctl(fd, TCIOC_START, 0);
  if (ret < 0)
    {
      printf("  Failed: TCIOC_START ret=%d errno=%d\n", ret, errno);
      return -errno;
    }

  /* one-time sanity on the reported status */

  if (ioctl(fd, TCIOC_GETSTATUS,
            (unsigned long)((uintptr_t)&status)) >= 0)
    {
      printf("  status: flags=0x%lx timeout=%luus timeleft=%luus\n",
             (unsigned long)status.flags,
             (unsigned long)status.timeout,
             (unsigned long)status.timeleft);
    }

  /* optional scope output (no-op unless -g was given) */

  gfd = gpio_out_open(cfg);

  /* warm-up fire: use it only to set the measurement baseline so the
   * start->arm latency does not bias the first measured interval.
   */

  ret = timer_wait_fire(cfg, &set);
  if (ret < 0)
    {
      printf("  FAIL: first signal not received (errno=%d)\n", -ret);
      ioctl(fd, TCIOC_STOP, 0);
      gpio_out_close(gfd);
      return ret;
    }

  /* Sample the baseline BEFORE toggling: the GPIOC_WRITE ioctl adds variable
   * latency, and with a 10ms-tick CLOCK_MONOTONIC that latency can push the
   * timestamp across a tick boundary (+/-10ms quantization).  Toggling after
   * mono_now() keeps the software measurement as clean as the no-'-g' path.
   */

  t_prev = mono_now();
  gpio_out_toggle(gfd, &glvl);

  for (i = 1; i <= cfg->rounds; i++)
    {
      ret = timer_wait_fire(cfg, &set);
      if (ret < 0)
        {
          printf("  round %d FAIL: signal not received (errno=%d) LOST\n",
                 i, -ret);
          ioctl(fd, TCIOC_STOP, 0);
          gpio_out_close(gfd);
          return ret;
        }

      t_now       = mono_now();   /* sample before toggle (see warm-up note) */
      gpio_out_toggle(gfd, &glvl);
      interval_us = (t_now - t_prev) * 1e6;
      t_prev      = t_now;
      err_us      = interval_us - (double)cfg->timeout_us;

      if (err_us < 0)
        {
          err_us = -err_us;
        }

      if (err_us > max_err_us)
        {
          max_err_us = err_us;
        }

      if (cfg->verbose)
        {
          printf("  round %d interval=%.1fus err=%+.1fus\n",
                 i, interval_us, interval_us - (double)cfg->timeout_us);
        }

      /* In scope mode the oscilloscope is the instrument; the 10ms-tick
       * software clock cannot resolve a 0.5% tolerance, so do not hard-fail
       * on it. A lost signal is still caught by timer_wait_fire().
       */

      if (err_us > tol_us && !cfg->gpio_toggle)
        {
          printf("  round %d FAIL: interval=%.1fus err=%.1fus "
                 "> tol=%.1fus\n",
                 i, interval_us, err_us, tol_us);
          ioctl(fd, TCIOC_STOP, 0);
          gpio_out_close(gfd);
          return -EIO;
        }
    }

  ioctl(fd, TCIOC_STOP, 0);
  gpio_out_close(gfd);

  if (cfg->gpio_toggle)
    {
      /* scope mode: report the software estimate as reference only and defer
       * the verdict to the oscilloscope reading on the toggle pin.
       */

      printf("  RESULT(scope) sw max_err=%.1fus (%.3f%%) -- "
             "software clock is coarse (10ms tick), reference only\n",
             max_err_us, max_err_us * 100.0 / (double)cfg->timeout_us);
      printf("  [TIMER-001] PASS waveform on %s -- read period on scope "
             "(timer=%luus, square wave=%luus)\n\n",
             cfg->gpio_devpath, (unsigned long)cfg->timeout_us,
             (unsigned long)cfg->timeout_us * 2);
    }
  else
    {
      printf("  RESULT max_err=%.1fus (%.3f%%) tol=%.1fus (%.2f%%)\n",
             max_err_us, max_err_us * 100.0 / (double)cfg->timeout_us,
             tol_us, cfg->tol_pct);
      printf("  [TIMER-001] PASS accuracy within tolerance\n\n");
    }

  return 0;
}

/****************************************************************************
 * TIMER-002 : clock prescaler effect
 *
 * With a fixed compare value, the overflow period scales as (div+1):
 * div=39 -> period T; div=79 -> period ~2T. Measured purely in software
 * via CLOCK_MONOTONIC (no logic analyzer needed).
 ****************************************************************************/

/* Measure one overflow period (seconds) at a given clock divider. Arms
 * /dev/timer0 with the config's compare value and returns the interval
 * between two consecutive expiries.
 */

static int case002_measure(int fd, const struct app_config_s *cfg,
                           uint8_t div, double *period_out,
                           int gfd, bool *glvl)
{
  sigset_t set;
  double   t1;
  double   t2;
  int      k;
  int      ret;

  ioctl(fd, TCIOC_STOP, 0);

  ret = ioctl(fd, BL616CL_TCIOC_SETCLOCKDIV, (unsigned long)div);
  if (ret < 0)
    {
      printf("  FAIL: SETCLOCKDIV div=%u errno=%d\n", div, errno);
      return -errno;
    }

  sigemptyset(&set);
  sigaddset(&set, TIMER_SIGNO);
  sigprocmask(SIG_BLOCK, &set, NULL);

  ret = timer_arm_periodic(fd, cfg);   /* SETTIMEOUT + NOTIFICATION */
  if (ret < 0)
    {
      return ret;
    }

  ret = ioctl(fd, TCIOC_START, 0);
  if (ret < 0)
    {
      printf("  FAIL: TCIOC_START errno=%d\n", errno);
      return -errno;
    }

  /* warm-up fire (clears start->arm latency bias) */

  ret = timer_wait_fire(cfg, &set);
  if (ret < 0)
    {
      printf("  FAIL div=%u: warm-up signal lost\n", div);
      ioctl(fd, TCIOC_STOP, 0);
      return ret;
    }

  t1 = mono_now();              /* sample before toggle (avoid ioctl latency) */
  gpio_out_toggle(gfd, glvl);
  ret = timer_wait_fire(cfg, &set);
  if (ret < 0)
    {
      printf("  FAIL div=%u: measure signal lost\n", div);
      ioctl(fd, TCIOC_STOP, 0);
      return ret;
    }

  t2 = mono_now();              /* sample before toggle (avoid ioctl latency) */
  gpio_out_toggle(gfd, glvl);
  *period_out = t2 - t1;

  /* scope observation window: keep toggling so a scope sees a sustained
   * square wave (period 2x the timer period) at this divider. No-op when
   * -g was not given (gfd < 0).
   */

  if (gfd >= 0)
    {
      for (k = 0; k < CASE002_SCOPE_FIRES; k++)
        {
          if (timer_wait_fire(cfg, &set) < 0)
            {
              break;
            }

          gpio_out_toggle(gfd, glvl);
        }
    }

  ioctl(fd, TCIOC_STOP, 0);
  return 0;
}

static int run_case_002(const struct app_config_s *cfg)
{
  struct app_config_s lcfg;
  double  period_a;
  double  period_b;
  double  ratio;
  double  expected;
  double  lo;
  double  hi;
  uint8_t div_a = (uint8_t)cfg->div_a;
  uint8_t div_b = (uint8_t)cfg->div_b;
  int     fd;
  int     gfd;
  bool    glvl = false;
  int     ret;

  printf("[TIMER-002] clock prescaler effect "
         "(div %u vs %u)\n", div_a, div_b);

  /* fixed compare so the only variable between the two runs is the divider.
   * -t overrides the default compare value.
   */

  lcfg = *cfg;
  lcfg.timeout_us = cfg->t_set ? cfg->timeout_us : DEF_002_COMPARE_US;

  fd = timer_open(cfg->timer_devpath);
  if (fd < 0)
    {
      return fd;
    }

  /* optional scope output, shared across both divider measurements */

  gfd = gpio_out_open(cfg);

  ret = case002_measure(fd, &lcfg, div_a, &period_a, gfd, &glvl);
  if (ret < 0)
    {
      gpio_out_close(gfd);
      close(fd);
      return ret;
    }

  ret = case002_measure(fd, &lcfg, div_b, &period_b, gfd, &glvl);
  if (ret < 0)
    {
      gpio_out_close(gfd);
      close(fd);
      return ret;
    }

  gpio_out_close(gfd);

  /* restore default divider so /dev/timer0 keeps its us calibration */

  ioctl(fd, TCIOC_STOP, 0);
  ioctl(fd, BL616CL_TCIOC_SETCLOCKDIV, (unsigned long)DEF_002_DIV_A);
  close(fd);

  ratio    = period_b / period_a;
  expected = (double)(div_b + 1) / (double)(div_a + 1);
  lo       = expected * (1.0 - DEF_002_RATIO_TOL_PCT / 100.0);
  hi       = expected * (1.0 + DEF_002_RATIO_TOL_PCT / 100.0);

  printf("  div=%u period=%.4fs\n", div_a, period_a);
  printf("  div=%u period=%.4fs\n", div_b, period_b);
  printf("  ratio period_b/period_a=%.3f (expected %.3f, tol +/-%.0f%%)\n",
         ratio, expected, DEF_002_RATIO_TOL_PCT);

  if (ratio >= lo && ratio <= hi)
    {
      printf("  [TIMER-002] PASS prescaler takes effect\n\n");
      return 0;
    }

  printf("  [TIMER-002] FAIL ratio %.3f out of [%.3f, %.3f]\n\n",
         ratio, lo, hi);
  return -EIO;
}

/****************************************************************************
 * TIMER-003 : PWM frequency / duty precision
 ****************************************************************************/

static int run_case_003(const struct app_config_s *cfg)
{
  struct pwm_point_s
  {
    uint32_t freq;
    unsigned duty_pct;
  };

  struct pwm_point_s pts[3];
  struct pwm_info_s  info;
  int hold_s = cfg->pwm_hold_s;
  int npts;
  int fd;
  int i;
  int ret;

  /* -f selects single-point mode (use -D for its duty); otherwise sweep the
   * three default groups.
   */

  if (cfg->pwm_freq_set)
    {
      pts[0].freq = cfg->pwm_freq;
      pts[0].duty_pct = cfg->pwm_duty_pct;
      npts = 1;
    }
  else
    {
      pts[0].freq = 1000;  pts[0].duty_pct = 50;
      pts[1].freq = 10000; pts[1].duty_pct = 25;
      pts[2].freq = 100;   pts[2].duty_pct = 75;
      npts = 3;
    }

  printf("[TIMER-003] PWM freq/duty precision (measure on scope/LA)\n");

  fd = open(cfg->pwm_devpath, O_RDWR);
  if (fd < 0)
    {
      printf("  FAIL: open %s errno=%d\n", cfg->pwm_devpath, errno);
      return -errno;
    }

  for (i = 0; i < npts; i++)
    {
      info.frequency = pts[i].freq;
      info.duty      = PCT_TO_DUTY_B16(pts[i].duty_pct);

      ret = ioctl(fd, PWMIOC_SETCHARACTERISTICS,
                  (unsigned long)((uintptr_t)&info));
      if (ret < 0)
        {
          printf("  FAIL: PWMIOC_SETCHARACTERISTICS errno=%d\n", errno);
          close(fd);
          return -errno;
        }

      ret = ioctl(fd, PWMIOC_START, 0);
      if (ret < 0)
        {
          printf("  FAIL: PWMIOC_START errno=%d\n", errno);
          close(fd);
          return -errno;
        }

      printf("  step %d: freq=%luHz duty=%u%% on %s (GPIO28), hold %ds\n",
             i + 1, (unsigned long)pts[i].freq, pts[i].duty_pct,
             cfg->pwm_devpath, hold_s);
      printf("         -> verify with scope / logic analyzer\n");

      sleep(hold_s);  /* window for scope/logic-analyzer measurement */

      ioctl(fd, PWMIOC_STOP, 0);
    }

  close(fd);
  printf("  [TIMER-003] DONE "
         "(judge freq/duty error vs threshold manually)\n\n");
  return 0;
}

/****************************************************************************
 * TIMER-004 : PWM duty ramp / breathing LED
 ****************************************************************************/

static int run_case_004(const struct app_config_s *cfg)
{
  struct pwm_info_s info;
  int cycles   = cfg->n_set ? cfg->rounds : DEF_BREATH_CYCLES;
  int step_pct = cfg->breath_step_pct;
  int step_ms  = cfg->breath_step_ms;
  int fd;
  int ret;
  int cycle;
  int duty;

  printf("[TIMER-004] PWM breathing freq=%luHz cycles=%d step=%d%% "
         "interval=%dms\n",
         (unsigned long)cfg->pwm_freq, cycles, step_pct, step_ms);

  fd = open(cfg->pwm_devpath, O_RDWR);
  if (fd < 0)
    {
      printf("  FAIL: open %s errno=%d\n", cfg->pwm_devpath, errno);
      return -errno;
    }

  info.frequency = cfg->pwm_freq;   /* fixed; only duty ramps */
  info.duty      = 0;

  ret = ioctl(fd, PWMIOC_SETCHARACTERISTICS,
              (unsigned long)((uintptr_t)&info));
  if (ret < 0)
    {
      printf("  FAIL: PWMIOC_SETCHARACTERISTICS errno=%d\n", errno);
      close(fd);
      return -errno;
    }

  ret = ioctl(fd, PWMIOC_START, 0);
  if (ret < 0)
    {
      printf("  FAIL: PWMIOC_START errno=%d\n", errno);
      close(fd);
      return -errno;
    }

  for (cycle = 0; cycle < cycles; cycle++)
    {
      /* ramp up 0 -> 100 */

      for (duty = 0; duty <= 100; duty += step_pct)
        {
          info.duty = PCT_TO_DUTY_B16(duty);
          ioctl(fd, PWMIOC_SETCHARACTERISTICS,
                (unsigned long)((uintptr_t)&info));
          ioctl(fd, PWMIOC_START, 0);  /* freq unchanged -> fast duty update */

          if (cfg->verbose && (duty % 10 == 0))
            {
              printf("  cycle %d duty=%d%%\n", cycle + 1, duty);
            }

          usleep(step_ms * 1000);
        }

      /* ramp down 100 -> 0 */

      for (duty = 100; duty >= 0; duty -= step_pct)
        {
          info.duty = PCT_TO_DUTY_B16(duty);
          ioctl(fd, PWMIOC_SETCHARACTERISTICS,
                (unsigned long)((uintptr_t)&info));
          ioctl(fd, PWMIOC_START, 0);

          if (cfg->verbose && (duty % 10 == 0))
            {
              printf("  cycle %d duty=%d%%\n", cycle + 1, duty);
            }

          usleep(step_ms * 1000);
        }
    }

  ioctl(fd, PWMIOC_STOP, 0);
  close(fd);
  printf("  [TIMER-004] DONE (LED breathing + logic analyzer ramp)\n\n");
  return 0;
}

/****************************************************************************
 * TIMER-005 : lifecycle and rejected requests
 ****************************************************************************/

static int expect_ioctl_errno(int fd, int cmd, unsigned long arg,
                              int expected_errno, const char *operation)
{
  int ret;

  errno = 0;
  ret = ioctl(fd, cmd, arg);
  if (ret >= 0 || errno != expected_errno)
    {
      printf("  FAIL: %s ret=%d errno=%d, expected errno=%d\n",
             operation, ret, errno, expected_errno);
      return -EIO;
    }

  printf("  rejected: %s errno=%d\n", operation, expected_errno);
  return 0;
}

static int run_case_005(const struct app_config_s *cfg)
{
  struct app_config_s edge_cfg = *cfg;
  struct timer_status_s before;
  struct timer_status_s after;
  sigset_t set;
  int fd;
  int ret;

  printf("[TIMER-005] Lifecycle and rejected requests\n");

  fd = timer_open(cfg->timer_devpath);
  if (fd < 0)
    {
      return fd;
    }

  ret = ioctl(fd, TCIOC_GETSTATUS, (unsigned long)(uintptr_t)&before);
  if (ret < 0)
    {
      printf("  FAIL: initial TCIOC_GETSTATUS errno=%d\n", errno);
      goto fail;
    }

  ret = expect_ioctl_errno(fd, TCIOC_SETTIMEOUT, 1, EINVAL,
                           "timeout below hardware minimum");
  if (ret < 0)
    {
      goto fail;
    }

  ret = ioctl(fd, TCIOC_GETSTATUS, (unsigned long)(uintptr_t)&after);
  if (ret < 0 || after.timeout != before.timeout)
    {
      printf("  FAIL: rejected timeout changed state (%lu -> %lu)\n",
             (unsigned long)before.timeout, (unsigned long)after.timeout);
      ret = -EIO;
      goto fail;
    }

  ret = expect_ioctl_errno(fd, BL616CL_TCIOC_SETCLOCKDIV, 256, EINVAL,
                           "clock divider above 8-bit range");
  if (ret < 0)
    {
      goto fail;
    }

  edge_cfg.timeout_us = CASE005_TIMEOUT_US;
  sigemptyset(&set);
  sigaddset(&set, TIMER_SIGNO);
  sigprocmask(SIG_BLOCK, &set, NULL);

  ret = timer_arm_periodic(fd, &edge_cfg);
  if (ret < 0 || ioctl(fd, TCIOC_START, 0) < 0)
    {
      printf("  FAIL: could not start edge timer errno=%d\n", errno);
      ret = -errno;
      goto fail;
    }

  ret = expect_ioctl_errno(fd, TCIOC_START, 0, EBUSY,
                           "start while already active");
  if (ret < 0)
    {
      goto stop_fail;
    }

  ret = expect_ioctl_errno(fd, BL616CL_TCIOC_SETCLOCKDIV, 79, EBUSY,
                           "change divider while active");
  if (ret < 0)
    {
      goto stop_fail;
    }

  ret = ioctl(fd, TCIOC_SETTIMEOUT, CASE005_TIMEOUT_US / 2);
  if (ret < 0)
    {
      printf("  FAIL: live timeout update errno=%d\n", errno);
      ret = -errno;
      goto stop_fail;
    }

  edge_cfg.timeout_us = CASE005_TIMEOUT_US / 2;
  ret = timer_wait_fire(&edge_cfg, &set);
  if (ret < 0)
    {
      printf("  FAIL: no signal after live timeout update errno=%d\n", -ret);
      goto stop_fail;
    }

  ret = ioctl(fd, TCIOC_GETSTATUS, (unsigned long)(uintptr_t)&after);
  if (ret < 0 || (after.flags & TCFLAGS_ACTIVE) == 0 ||
      after.timeout != edge_cfg.timeout_us)
    {
      printf("  FAIL: active status after live update flags=0x%lx "
             "timeout=%lu\n", (unsigned long)after.flags,
             (unsigned long)after.timeout);
      ret = -EIO;
      goto stop_fail;
    }

  ret = ioctl(fd, TCIOC_STOP, 0);
  if (ret < 0)
    {
      printf("  FAIL: first stop errno=%d\n", errno);
      ret = -errno;
      goto fail;
    }

  ret = expect_ioctl_errno(fd, TCIOC_STOP, 0, ENODEV,
                           "stop while already inactive");
  if (ret < 0)
    {
      goto fail;
    }

  ret = ioctl(fd, TCIOC_GETSTATUS, (unsigned long)(uintptr_t)&after);
  if (ret < 0 || (after.flags & TCFLAGS_ACTIVE) != 0)
    {
      printf("  FAIL: timer still reports active after stop\n");
      ret = -EIO;
      goto fail;
    }

  (void)ioctl(fd, TCIOC_SETTIMEOUT, cfg->timeout_us);
  (void)ioctl(fd, BL616CL_TCIOC_SETCLOCKDIV, DEF_002_DIV_A);
  close(fd);
  printf("  [TIMER-005] PASS rejected requests preserved state; "
         "live update fired; lifecycle recovered\n\n");
  return 0;

stop_fail:
  (void)ioctl(fd, TCIOC_STOP, 0);
fail:
  (void)ioctl(fd, BL616CL_TCIOC_SETCLOCKDIV, DEF_002_DIV_A);
  close(fd);
  return ret < 0 ? ret : -EIO;
}

/****************************************************************************
 * TIMER-006 : tick ioctl conversion and boundaries
 ****************************************************************************/

static int run_case_006(const struct app_config_s *cfg)
{
  struct timer_status_s tick_status;
  struct timer_status_s usec_status;
  uint32_t tick_max;
  uint32_t usec_max;
  uint32_t rounded_usec = (uint32_t)USEC_PER_TICK + 1;
  int fd;
  int ret = -EIO;

  printf("[TIMER-006] Tick ioctl conversion and boundaries\n");
  fd = timer_open(cfg->timer_devpath);
  if (fd < 0)
    {
      return fd;
    }

  (void)ioctl(fd, TCIOC_STOP, 0);
  if (ioctl(fd, TCIOC_SETTIMEOUT, rounded_usec) < 0 ||
      ioctl(fd, TCIOC_TICK_GETSTATUS,
            (unsigned long)(uintptr_t)&tick_status) < 0 ||
      tick_status.timeout != 2)
    {
      printf("  FAIL: microsecond timeout did not round up to 2 ticks\n");
      goto out;
    }

  if (ioctl(fd, TCIOC_TICK_SETTIMEOUT, 3) < 0 ||
      ioctl(fd, TCIOC_TICK_GETSTATUS,
            (unsigned long)(uintptr_t)&tick_status) < 0 ||
      ioctl(fd, TCIOC_GETSTATUS,
            (unsigned long)(uintptr_t)&usec_status) < 0 ||
      tick_status.timeout != 3 ||
      usec_status.timeout != 3 * (uint32_t)USEC_PER_TICK)
    {
      printf("  FAIL: tick timeout/status conversion mismatch\n");
      goto out;
    }

  if (ioctl(fd, TCIOC_TICK_MAXTIMEOUT,
            (unsigned long)(uintptr_t)&tick_max) < 0 ||
      ioctl(fd, TCIOC_MAXTIMEOUT,
            (unsigned long)(uintptr_t)&usec_max) < 0 ||
      tick_max != usec_max / (uint32_t)USEC_PER_TICK)
    {
      printf("  FAIL: tick maximum mismatch\n");
      goto out;
    }

  if (expect_ioctl_errno(fd, TCIOC_TICK_SETTIMEOUT, 0, EINVAL,
                         "zero tick timeout") < 0 ||
      expect_ioctl_errno(fd, TCIOC_TICK_SETTIMEOUT, UINT32_MAX, ERANGE,
                         "tick timeout multiplication overflow") < 0)
    {
      goto out;
    }

  printf("  tick=%luus rounded=%luus->2 ticks set=3 ticks max=%lu\n",
         (unsigned long)USEC_PER_TICK, (unsigned long)rounded_usec,
         (unsigned long)tick_max);
  printf("  [TIMER-006] PASS tick conversion and boundaries\n\n");
  ret = 0;

out:
  (void)ioctl(fd, TCIOC_SETTIMEOUT, cfg->timeout_us);
  close(fd);
  return ret;
}

/****************************************************************************
 * TIMER-007 : poll and one-shot notification
 ****************************************************************************/

static int run_case_007(const struct app_config_s *cfg)
{
  struct app_config_s lcfg = *cfg;
  struct timer_status_s status;
  struct pollfd pfd;
  struct pollfd busy[2];
  sigset_t set;
  int fd;
  int ret = -EIO;

  printf("[TIMER-007] Poll and one-shot notification\n");
  lcfg.timeout_us = CASE005_TIMEOUT_US;
  fd = timer_open(cfg->timer_devpath);
  if (fd < 0)
    {
      return fd;
    }

  sigemptyset(&set);
  sigaddset(&set, TIMER_SIGNO);
  sigprocmask(SIG_BLOCK, &set, NULL);

  if (timer_arm_notify(fd, &lcfg, false, TIMER_SIGNO) < 0 ||
      ioctl(fd, TCIOC_START, 0) < 0)
    {
      printf("  FAIL: could not arm one-shot notification "
             "errno=%d\n", errno);
      goto out;
    }

  pfd.fd = fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  if (poll(&pfd, 1, 1000) != 1 || (pfd.revents & POLLIN) == 0)
    {
      printf("  FAIL: poll did not receive timer expiry revents=0x%lx\n",
             (unsigned long)pfd.revents);
      goto out;
    }

  if (timer_wait_signal(lcfg.timeout_us, &set) < 0 ||
      ioctl(fd, TCIOC_GETSTATUS, (unsigned long)(uintptr_t)&status) < 0 ||
      (status.flags & TCFLAGS_ACTIVE) != 0)
    {
      printf("  FAIL: one-shot signal/status contract failed\n");
      goto out;
    }

  memset(busy, 0, sizeof(busy));
  busy[0].fd = fd;
  busy[0].events = POLLIN;
  busy[1].fd = fd;
  busy[1].events = POLLIN;
  if (poll(busy, 2, 0) != 1 || (busy[1].revents & POLLERR) == 0)
    {
      printf("  FAIL: second poll waiter boundary revents=0x%lx/0x%lx\n",
             (unsigned long)busy[0].revents,
             (unsigned long)busy[1].revents);
      goto out;
    }

  printf("  poll revents=0x%lx second-waiter revents=0x%lx\n",
         (unsigned long)pfd.revents, (unsigned long)busy[1].revents);
  printf("  [TIMER-007] PASS poll, one-shot and single-waiter boundary\n\n");
  ret = 0;

out:
  (void)ioctl(fd, TCIOC_STOP, 0);
  close(fd);
  return ret;
}

/****************************************************************************
 * TIMER-008 : multi-fd shared state and close lifetime
 ****************************************************************************/

static int run_case_008(const struct app_config_s *cfg)
{
  struct app_config_s lcfg = *cfg;
  struct timer_status_s status;
  sigset_t set;
  int fd1;
  int fd2;
  int ret = -EIO;

  printf("[TIMER-008] Multi-fd shared state and close lifetime\n");
  lcfg.timeout_us = 60000;
  fd1 = timer_open(cfg->timer_devpath);
  if (fd1 < 0)
    {
      return fd1;
    }

  fd2 = timer_open(cfg->timer_devpath);
  if (fd2 < 0)
    {
      close(fd1);
      return fd2;
    }

  sigemptyset(&set);
  sigaddset(&set, TIMER_SIGNO);
  sigprocmask(SIG_BLOCK, &set, NULL);

  if (timer_arm_periodic(fd1, &lcfg) < 0 ||
      ioctl(fd2, TCIOC_GETSTATUS, (unsigned long)(uintptr_t)&status) < 0 ||
      status.timeout != lcfg.timeout_us || ioctl(fd1, TCIOC_START, 0) < 0)
    {
      printf("  FAIL: shared configuration/start contract failed\n");
      goto out_both;
    }

  close(fd1);
  fd1 = -1;
  if (timer_wait_signal(lcfg.timeout_us, &set) < 0 ||
      ioctl(fd2, TCIOC_GETSTATUS, (unsigned long)(uintptr_t)&status) < 0 ||
      (status.flags & TCFLAGS_ACTIVE) == 0)
    {
      printf("  FAIL: closing one fd stopped shared timer\n");
      goto out_both;
    }

  if (ioctl(fd2, TCIOC_STOP, 0) < 0)
    {
      printf("  FAIL: remaining fd could not stop timer errno=%d\n", errno);
      goto out_both;
    }

  printf("  shared timeout=%luus active-after-close=yes\n",
         (unsigned long)status.timeout);
  printf("  [TIMER-008] PASS shared state and close lifetime\n\n");
  ret = 0;

out_both:
  (void)ioctl(fd2, TCIOC_STOP, 0);
  if (fd1 >= 0)
    {
      close(fd1);
    }

  close(fd2);
  return ret;
}

/****************************************************************************
 * TIMER-009 : dual-instance isolation
 ****************************************************************************/

static int run_case_009(void)
{
  struct app_config_s cfg0;
  struct app_config_s cfg1;
  struct timer_status_s status0;
  struct timer_status_s status1;
  sigset_t set0;
  sigset_t set1;
  int fd0;
  int fd1;
  int ret = -EIO;

  printf("[TIMER-009] TIMER0/TIMER1 dual-instance isolation\n");
  memset(&cfg0, 0, sizeof(cfg0));
  memset(&cfg1, 0, sizeof(cfg1));
  cfg0.timeout_us = 40000;
  cfg1.timeout_us = 70000;

  fd0 = timer_open("/dev/timer0");
  if (fd0 < 0)
    {
      return fd0;
    }

  fd1 = timer_open("/dev/timer1");
  if (fd1 < 0)
    {
      close(fd0);
      return fd1;
    }

  sigemptyset(&set0);
  sigaddset(&set0, TIMER_SIGNO);
  sigemptyset(&set1);
  sigaddset(&set1, TIMER_SIGNO + 1);
  sigprocmask(SIG_BLOCK, &set0, NULL);
  sigprocmask(SIG_BLOCK, &set1, NULL);

  if (timer_arm_notify(fd0, &cfg0, true, TIMER_SIGNO) < 0 ||
      timer_arm_notify(fd1, &cfg1, true, TIMER_SIGNO + 1) < 0 ||
      ioctl(fd0, TCIOC_START, 0) < 0 || ioctl(fd1, TCIOC_START, 0) < 0 ||
      timer_wait_signal(cfg0.timeout_us, &set0) < 0 ||
      timer_wait_signal(cfg1.timeout_us, &set1) < 0)
    {
      printf("  FAIL: initial dual expiry failed errno=%d\n", errno);
      goto out;
    }

  cfg0.timeout_us = 30000;
  cfg1.timeout_us = 50000;
  if (ioctl(fd0, TCIOC_SETTIMEOUT, cfg0.timeout_us) < 0 ||
      ioctl(fd1, TCIOC_SETTIMEOUT, cfg1.timeout_us) < 0 ||
      timer_wait_signal(cfg0.timeout_us, &set0) < 0 ||
      timer_wait_signal(cfg1.timeout_us, &set1) < 0 ||
      ioctl(fd0, TCIOC_GETSTATUS, (unsigned long)(uintptr_t)&status0) < 0 ||
      ioctl(fd1, TCIOC_GETSTATUS, (unsigned long)(uintptr_t)&status1) < 0 ||
      status0.timeout != cfg0.timeout_us ||
      status1.timeout != cfg1.timeout_us)
    {
      printf("  FAIL: interleaved live update/status isolation failed\n");
      goto out;
    }

  if (ioctl(fd0, TCIOC_STOP, 0) < 0 ||
      timer_wait_signal(cfg1.timeout_us, &set1) < 0 ||
      ioctl(fd1, TCIOC_GETSTATUS, (unsigned long)(uintptr_t)&status1) < 0 ||
      (status1.flags & TCFLAGS_ACTIVE) == 0)
    {
      printf("  FAIL: stopping timer0 disturbed timer1\n");
      goto out;
    }

  printf("  timer0=%luus timer1=%luus timer1-active-after-timer0-stop=yes\n",
         (unsigned long)status0.timeout, (unsigned long)status1.timeout);
  printf("  [TIMER-009] PASS dual IRQ, state and stop isolation\n\n");
  ret = 0;

out:
  (void)ioctl(fd0, TCIOC_STOP, 0);
  (void)ioctl(fd1, TCIOC_STOP, 0);
  close(fd0);
  close(fd1);
  return ret;
}

/****************************************************************************
 * TIMER-010 : raw lower-half callback contract
 ****************************************************************************/

#ifdef CONFIG_BL616CL_TIMER_TEST
struct raw_callback_context_s
{
  volatile uint32_t count;
  uint32_t next_interval;
};

static bool timer_raw_callback(uint32_t *next_interval, void *arg)
{
  struct raw_callback_context_s *ctx = arg;

  ctx->count++;
  if (ctx->count == 1)
    {
      *next_interval = ctx->next_interval;
      return true;
    }

  return false;
}

static int timer_index_from_path(const char *devpath)
{
  size_t len = strlen(devpath);

  if (len == 0 || devpath[len - 1] < '0' || devpath[len - 1] > '1')
    {
      return -EINVAL;
    }

  return devpath[len - 1] - '0';
}
#endif

static int run_case_010(const struct app_config_s *cfg)
{
#ifdef CONFIG_BL616CL_TIMER_TEST
  struct raw_callback_context_s ctx;
  struct timer_lowerhalf_s *lower;
  struct timer_status_s status;
  int timer;
  int waits;
  int ret = -EIO;

  printf("[TIMER-010] Raw lower-half callback contract\n");
  memset(&status, 0, sizeof(status));
  timer = timer_index_from_path(cfg->timer_devpath);
  if (timer < 0)
    {
      printf("  FAIL: timer path must end in 0 or 1\n");
      return timer;
    }

  lower = bl616cl_timer_test_lower((uint8_t)timer);
  if (lower == NULL)
    {
      printf("  FAIL: selected timer lower is not built\n");
      return -ENODEV;
    }

  ctx.count = 0;
  ctx.next_interval = 30000;
  (void)TIMER_STOP(lower);
  if (TIMER_SETTIMEOUT(lower, 50000) < 0)
    {
      printf("  FAIL: raw settimeout failed\n");
      return -EIO;
    }

  TIMER_SETCALLBACK(lower, timer_raw_callback, &ctx);
  if (TIMER_START(lower) < 0)
    {
      printf("  FAIL: raw start failed\n");
      TIMER_SETCALLBACK(lower, NULL, NULL);
      return -EIO;
    }

  for (waits = 0; waits < 100 && ctx.count < 2; waits++)
    {
      usleep(10000);
    }

  if (TIMER_GETSTATUS(lower, &status) >= 0 && ctx.count == 2 &&
      (status.flags & TCFLAGS_ACTIVE) == 0 &&
      status.timeout == ctx.next_interval)
    {
      printf("  callbacks=2 next=%luus active=no\n",
             (unsigned long)status.timeout);
      printf("  [TIMER-010] PASS true reload, next interval and "
             "false stop\n\n");
      ret = 0;
    }
  else
    {
      printf("  FAIL: callbacks=%lu flags=0x%lx timeout=%lu\n",
             (unsigned long)ctx.count, (unsigned long)status.flags,
             (unsigned long)status.timeout);
    }

  (void)TIMER_STOP(lower);
  TIMER_SETCALLBACK(lower, NULL, NULL);
  (void)TIMER_SETTIMEOUT(lower, cfg->timeout_us);
  return ret;
#else
  UNUSED(cfg);
  printf("[TIMER-010] FAIL raw callback test is not enabled\n");
  return -ENOTSUP;
#endif
}

/****************************************************************************
 * main
 ****************************************************************************/

int main(int argc, char *argv[])
{
  struct app_config_s cfg;
  int  opt;
  int  ret;
  int  fd;
  int  executed = 0;
  int  passed   = 0;

  cfg.timer_devpath   = DEFAULT_TIMER_DEVPATH;
  cfg.pwm_devpath     = DEFAULT_PWM_DEVPATH;
  cfg.gpio_devpath    = DEFAULT_GPIO_DEVPATH;
  cfg.case_id         = CASE_ALL;
  cfg.timeout_us      = DEF_TIMEOUT_US;
  cfg.t_set           = false;
  cfg.rounds          = DEF_ROUNDS;
  cfg.n_set           = false;
  cfg.tol_pct         = DEF_TOL_PCT;
  cfg.div_a           = DEF_002_DIV_A;
  cfg.div_b           = DEF_002_DIV_B;
  cfg.pwm_freq        = DEF_PWM_FREQ;
  cfg.pwm_freq_set    = false;
  cfg.pwm_duty_pct    = DEF_PWM_DUTY_PCT;
  cfg.pwm_hold_s      = DEF_PWM_HOLD_S;
  cfg.breath_step_pct = DEF_BREATH_STEP_PCT;
  cfg.breath_step_ms  = DEF_BREATH_STEP_MS;
  cfg.verbose         = false;
  cfg.gpio_toggle     = false;

  while ((opt = getopt(argc, argv,
                       "c:d:p:g:t:n:e:a:b:f:D:w:s:i:vh")) != -1)
    {
      switch (opt)
        {
          case 'c': cfg.case_id       = optarg;                     break;
          case 'd': cfg.timer_devpath = optarg;                     break;
          case 'p': cfg.pwm_devpath   = optarg;                     break;
          case 'g': cfg.gpio_devpath  = optarg;
                    cfg.gpio_toggle   = true;                        break;
          case 't': cfg.timeout_us    = (uint32_t)strtoul(optarg, NULL, 0);
                    cfg.t_set         = true;                        break;
          case 'n': cfg.rounds        = atoi(optarg);
                    cfg.n_set         = true;                        break;
          case 'e': cfg.tol_pct       = strtod(optarg, NULL);       break;
          case 'a': cfg.div_a         = atoi(optarg);               break;
          case 'b': cfg.div_b         = atoi(optarg);               break;
          case 'f': cfg.pwm_freq      = (uint32_t)strtoul(optarg, NULL, 0);
                    cfg.pwm_freq_set  = true;                        break;
          case 'D': cfg.pwm_duty_pct  = (unsigned)atoi(optarg);     break;
          case 'w': cfg.pwm_hold_s    = atoi(optarg);               break;
          case 's': cfg.breath_step_pct = atoi(optarg);             break;
          case 'i': cfg.breath_step_ms  = atoi(optarg);             break;
          case 'v': cfg.verbose       = true;                       break;
          case 'h':
          default:  print_usage(argv[0]);                        return 0;
        }
    }

  if (cfg.timeout_us == 0 || cfg.rounds <= 0 || cfg.tol_pct <= 0.0)
    {
      printf("Invalid argument (timeout/rounds/tol must be > 0)\n");
      return ERROR;
    }

  if (cfg.div_a < 0 || cfg.div_a > 255 || cfg.div_b < 0 || cfg.div_b > 255)
    {
      printf("Invalid -a/-b divider (must be 0..255)\n");
      return ERROR;
    }

  if (cfg.pwm_freq == 0 || cfg.pwm_duty_pct > 100 || cfg.pwm_hold_s <= 0 ||
      cfg.breath_step_pct <= 0 || cfg.breath_step_pct > 100 ||
      cfg.breath_step_ms < 0)
    {
      printf("Invalid PWM arg (-f>0, -D 0..100, -w>0, -s 1..100, -i>=0)\n");
      return ERROR;
    }

  if (strcmp(cfg.case_id, CASE_ALL) != 0 &&
      strcmp(cfg.case_id, CASE_001) != 0 &&
      strcmp(cfg.case_id, CASE_002) != 0 &&
      strcmp(cfg.case_id, CASE_003) != 0 &&
      strcmp(cfg.case_id, CASE_004) != 0 &&
      strcmp(cfg.case_id, CASE_005) != 0 &&
      strcmp(cfg.case_id, CASE_006) != 0 &&
      strcmp(cfg.case_id, CASE_007) != 0 &&
      strcmp(cfg.case_id, CASE_008) != 0 &&
      strcmp(cfg.case_id, CASE_009) != 0 &&
      strcmp(cfg.case_id, CASE_010) != 0)
    {
      printf("Unsupported case id: %s\n", cfg.case_id);
      return ERROR;
    }

  printf("MCU Peripheral Timer Tests\n");
  printf("Case: %s Timer: %s PWM: %s\n",
         cfg.case_id, cfg.timer_devpath, cfg.pwm_devpath);
  if (cfg.gpio_toggle)
    {
      printf("GPIO toggle: %s (scope sees square wave, period = 2x timer "
             "period)\n", cfg.gpio_devpath);
    }

  /* TIMER-001 needs /dev/timer0 */

  if (strcmp(cfg.case_id, CASE_ALL) == 0 ||
      strcmp(cfg.case_id, CASE_001) == 0)
    {
      executed++;
      fd = timer_open(cfg.timer_devpath);
      if (fd < 0)
        {
          if (strcmp(cfg.case_id, CASE_001) == 0)
            {
              return ERROR;
            }
        }
      else
        {
          ret = run_case_001(fd, &cfg);
          close(fd);
          if (ret >= 0)
            {
              passed++;
            }
          else if (strcmp(cfg.case_id, CASE_001) == 0)
            {
              printf("Timer Summary: executed=%d passed=%d -> FAIL\n",
                     executed, passed);
              return ERROR;
            }
        }
    }

  /* TIMER-002 (prescaler, TODO) */

  if (strcmp(cfg.case_id, CASE_ALL) == 0 ||
      strcmp(cfg.case_id, CASE_002) == 0)
    {
      executed++;
      ret = run_case_002(&cfg);
      if (ret >= 0)
        {
          passed++;
        }
      else if (strcmp(cfg.case_id, CASE_002) == 0)
        {
          printf("Timer Summary: executed=%d passed=%d -> FAIL\n",
                 executed, passed);
          return ERROR;
        }
    }

  /* TIMER-005 (lifecycle and rejected requests) */

  if (strcmp(cfg.case_id, CASE_ALL) == 0 ||
      strcmp(cfg.case_id, CASE_005) == 0)
    {
      executed++;
      ret = run_case_005(&cfg);
      if (ret >= 0)
        {
          passed++;
        }
      else if (strcmp(cfg.case_id, CASE_005) == 0)
        {
          printf("Timer Summary: executed=%d passed=%d -> FAIL\n",
                 executed, passed);
          return ERROR;
        }
    }

  if (strcmp(cfg.case_id, CASE_ALL) == 0 ||
      strcmp(cfg.case_id, CASE_006) == 0)
    {
      executed++;
      ret = run_case_006(&cfg);
      if (ret >= 0)
        {
          passed++;
        }
      else if (strcmp(cfg.case_id, CASE_006) == 0)
        {
          printf("Timer Summary: executed=%d passed=%d -> FAIL\n",
                 executed, passed);
          return ERROR;
        }
    }

  if (strcmp(cfg.case_id, CASE_ALL) == 0 ||
      strcmp(cfg.case_id, CASE_007) == 0)
    {
      executed++;
      ret = run_case_007(&cfg);
      if (ret >= 0)
        {
          passed++;
        }
      else if (strcmp(cfg.case_id, CASE_007) == 0)
        {
          printf("Timer Summary: executed=%d passed=%d -> FAIL\n",
                 executed, passed);
          return ERROR;
        }
    }

  if (strcmp(cfg.case_id, CASE_ALL) == 0 ||
      strcmp(cfg.case_id, CASE_008) == 0)
    {
      executed++;
      ret = run_case_008(&cfg);
      if (ret >= 0)
        {
          passed++;
        }
      else if (strcmp(cfg.case_id, CASE_008) == 0)
        {
          printf("Timer Summary: executed=%d passed=%d -> FAIL\n",
                 executed, passed);
          return ERROR;
        }
    }

  if (strcmp(cfg.case_id, CASE_ALL) == 0 ||
      strcmp(cfg.case_id, CASE_009) == 0)
    {
      executed++;
      ret = run_case_009();
      if (ret >= 0)
        {
          passed++;
        }
      else if (strcmp(cfg.case_id, CASE_009) == 0)
        {
          printf("Timer Summary: executed=%d passed=%d -> FAIL\n",
                 executed, passed);
          return ERROR;
        }
    }

  if (strcmp(cfg.case_id, CASE_ALL) == 0 ||
      strcmp(cfg.case_id, CASE_010) == 0)
    {
      executed++;
      ret = run_case_010(&cfg);
      if (ret >= 0)
        {
          passed++;
        }
      else if (strcmp(cfg.case_id, CASE_010) == 0)
        {
          printf("Timer Summary: executed=%d passed=%d -> FAIL\n",
                 executed, passed);
          return ERROR;
        }
    }

  /* TIMER-003 (PWM precision) */

  if (strcmp(cfg.case_id, CASE_ALL) == 0 ||
      strcmp(cfg.case_id, CASE_003) == 0)
    {
      executed++;
      ret = run_case_003(&cfg);
      if (ret >= 0)
        {
          passed++;
        }
      else if (strcmp(cfg.case_id, CASE_003) == 0)
        {
          printf("Timer Summary: executed=%d passed=%d -> FAIL\n",
                 executed, passed);
          return ERROR;
        }
    }

  /* TIMER-004 (breathing LED): cycles default handled inside run_case_004
   * via cfg.n_set.
   */

  if (strcmp(cfg.case_id, CASE_ALL) == 0 ||
      strcmp(cfg.case_id, CASE_004) == 0)
    {
      executed++;
      ret = run_case_004(&cfg);
      if (ret >= 0)
        {
          passed++;
        }
      else if (strcmp(cfg.case_id, CASE_004) == 0)
        {
          printf("Timer Summary: executed=%d passed=%d -> FAIL\n",
                 executed, passed);
          return ERROR;
        }
    }

  printf("Timer Summary: executed=%d passed=%d failed=%d -> %s\n",
         executed, passed, executed - passed,
         (executed == passed) ? "PASS" : "FAIL");

  return (executed == passed) ? OK : ERROR;
}
