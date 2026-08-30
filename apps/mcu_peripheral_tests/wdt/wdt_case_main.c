/****************************************************************************
 * apps/vendor/bouffalolab/apps/mcu_peripheral_tests/wdt/wdt_case_main.c
 *
 * MCU Peripheral WDT (Watchdog) Test Cases
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/boardctl.h>
#include <sys/ioctl.h>

#include <nuttx/timers/watchdog.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DEFAULT_WDT_DEVPATH    "/dev/watchdog0"

#define CASE_001               "001"
#define CASE_002               "002"
#define CASE_003               "003"
#define CASE_004               "004"
#define CASE_005               "005"
#define CASE_ALL               "all"

#define DEFAULT_TIMEOUT_MS     3000
#define DEFAULT_PINGTIME_MS    9000
#define DEFAULT_PINGDELAY_MS   1000
#define BL616CL_WDT_MAXTIMEOUT 63999
#define CASE003_MIN_TIMEOUT_MS 100

/* Safety guard for case 001: if the device fails to reset, give up after
 * this many multiples of the configured timeout instead of hanging forever.
 */

#define CASE001_GUARD_MULTIPLE 3

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct app_config_s
{
  const char *devpath;
  const char *case_id;
  uint32_t timeout;   /* Watchdog timeout in milliseconds */
  uint32_t pingtime;  /* Total feeding duration for case 002 (ms) */
  uint32_t pingdelay; /* Interval between feeds for case 002 (ms) */
  bool status_only;   /* Case 001: only report previous reset cause */
  bool verbose;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void print_usage(const char *progname);
static int read_reset_cause(enum boardioc_reset_cause_e *cause);
static uint64_t now_ms(void);
static int open_wdt_device(const char *devpath);
static int run_case_001(const struct app_config_s *cfg);
static int run_case_002(const struct app_config_s *cfg);
static int run_case_003(const struct app_config_s *cfg);
static int run_case_004(const struct app_config_s *cfg);
static int run_case_005(const struct app_config_s *cfg);
static int run_selected_cases(const struct app_config_s *cfg);

#ifdef CONFIG_BL616CL_WDT_CAPTURE
static volatile unsigned int g_capture_count[2];

static int capture_callback_0(int irq, void *context, void *arg)
{
  (void)irq;
  (void)context;
  (void)arg;
  g_capture_count[0]++;
  return OK;
}

static int capture_callback_1(int irq, void *context, void *arg)
{
  (void)irq;
  (void)context;
  (void)arg;
  g_capture_count[1]++;
  return OK;
}
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void print_usage(const char *progname)
{
  printf("Usage: %s [options]\n", progname);
  printf("Options:\n");
  printf("  -c <id>    Case id: 001..005, all (default: all)\n");
  printf("             001: timeout reset "
         "(two-stage, reboots the device)\n");
  printf("             002: periodic keepalive, no reset\n");
  printf("             003: boundaries, live timeout and lifecycle\n");
  printf("             004: interrupt capture and reset restore\n");
  printf("             005: automonitor stability and handoff\n");
  printf("             all: run 002/004/003, then reset with 001\n");
  printf("  -d <dev>   Watchdog device path (default: %s)\n",
         DEFAULT_WDT_DEVPATH);
  printf("  -t <ms>    Watchdog timeout in ms (default: %d)\n",
         DEFAULT_TIMEOUT_MS);
  printf("  -p <ms>    Case 002 total feeding duration "
         "in ms (default: %d)\n",
         DEFAULT_PINGTIME_MS);
  printf("  -i <ms>    Case 002 feed interval in ms (default: %d)\n",
         DEFAULT_PINGDELAY_MS);
  printf("  -s         Case 001: only report previous reset cause, "
         "do NOT\n");
  printf("             arm the watchdog (non-destructive confirmation)\n");
  printf("  -v         Verbose output\n");
  printf("  -h         Show this help\n");
}

/****************************************************************************
 * Name: read_reset_cause
 *
 * Description:
 *   Query the cause of the last reset via boardctl. On success, *cause holds
 *   the reset cause enumeration.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int read_reset_cause(enum boardioc_reset_cause_e *cause)
{
  struct boardioc_reset_cause_s c;
  int ret;

  memset(&c, 0, sizeof(c));

  ret = boardctl(BOARDIOC_RESET_CAUSE, (uintptr_t)&c);
  if (ret < 0)
    {
      return -errno;
    }

  *cause = c.cause;
  return OK;
}

/****************************************************************************
 * Name: now_ms
 *
 * Description:
 *   Return a monotonic timestamp in milliseconds.
 *
 ****************************************************************************/

static uint64_t now_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

static int open_wdt_device(const char *devpath)
{
  int fd = open(devpath, O_RDONLY);

  if (fd < 0)
    {
      printf("Failed to open %s: errno=%d\n", devpath, errno);
      return -errno;
    }

  return fd;
}

/****************************************************************************
 * Name: run_case_001
 *
 * Description:
 *   WDT-001 timeout reset. Each run first reports the previous reset cause
 *   (so the result of the round that armed the watchdog is shown exactly
 *   once), then arms the watchdog again and stops feeding it to trigger a
 *   fresh reset for the current round. Issuing WDIOC_START hands the
 *   watchdog over to user space and stops the kernel auto-monitor, so the
 *   timer expires and resets the chip.
 *
 *   Running it repeatedly therefore keeps confirming the previous round and
 *   triggering the next one, without needing a manual reboot in between.
 *
 *   With status_only (-s) the command stops after reporting the previous
 *   reset cause and does NOT arm the watchdog: a non-destructive way to
 *   confirm that the last reset was caused by the watchdog.
 *
 ****************************************************************************/

static int run_case_001(const struct app_config_s *cfg)
{
  enum boardioc_reset_cause_e cause = BOARDIOC_RESETCAUSE_NONE;
  bool prev_was_wdt;
  uint64_t deadline;
  int fd;
  int ret;

  printf("[WDT-001] Timeout reset (report previous round, then trigger)\n");

  ret = read_reset_cause(&cause);
  if (ret < 0)
    {
      printf("  Failed: boardctl(RESET_CAUSE) ret=%d\n", ret);
      return ret;
    }

  /* Report the result of the previous round (shown once per boot). */

  prev_was_wdt = (cause == BOARDIOC_RESETCAUSE_SYS_RWDT);
  if (prev_was_wdt)
    {
      printf("  PASS: previous reset cause = WATCHDOG (SYS_RWDT)\n");
    }
  else
    {
      printf("  Previous reset cause = %d (not WATCHDOG)\n", (int)cause);
    }

  /* status_only: confirm and stop, do not arm a new reset. */

  if (cfg->status_only)
    {
      printf("  status-only: watchdog not armed\n\n");
      return prev_was_wdt ? 0 : -EAGAIN;
    }

  /* Arm the watchdog and let it expire to reset the device this round. */

  fd = open_wdt_device(cfg->devpath);
  if (fd < 0)
    {
      return fd;
    }

  ret = ioctl(fd, WDIOC_SETTIMEOUT, (unsigned long)cfg->timeout);
  if (ret < 0)
    {
      printf("  Failed: ioctl(WDIOC_SETTIMEOUT) errno=%d\n", errno);
      close(fd);
      return -errno;
    }

  ret = ioctl(fd, WDIOC_START, 0);
  if (ret < 0)
    {
      printf("  Failed: ioctl(WDIOC_START) errno=%d\n", errno);
      close(fd);
      return -errno;
    }

  printf("  Arming watchdog (%lums), NOT feeding. Device will reset now.\n",
         (unsigned long)cfg->timeout);
  printf("  >>> After reboot, re-run 'mcu_wdt_test -c 001' to confirm this"
         " round (or '-c 001 -s' to confirm without resetting) <<<\n");
  fflush(stdout);

  /* Do not feed the watchdog; wait for the hardware reset. The guard below
   * only fires if the device unexpectedly fails to reset.
   */

  deadline = now_ms() + (uint64_t)cfg->timeout * CASE001_GUARD_MULTIPLE;
  while (now_ms() < deadline)
    {
      usleep(100000);
    }

  printf("[WDT-001] FAIL: watchdog did not reset "
         "the device within %lums\n\n",
         (unsigned long)(cfg->timeout * CASE001_GUARD_MULTIPLE));
  close(fd);
  return -ETIMEDOUT;
}

/****************************************************************************
 * Name: run_case_002
 *
 * Description:
 *   WDT-002 periodic keepalive: feed the watchdog for the whole feeding
 *   duration and confirm the device does not reset. WDIOC_START stops the
 *   kernel auto-monitor, so this case fully owns the watchdog; it must
 *   WDIOC_STOP on exit, otherwise the device would reset shortly after the
 *   command returns to NSH.
 *
 ****************************************************************************/

static int run_case_002(const struct app_config_s *cfg)
{
  struct watchdog_status_s status;
  uint64_t start;
  uint64_t elapsed = 0;
  unsigned int feeds = 0;
  int fd;
  int ret;

  memset(&status, 0, sizeof(status));
  printf("[WDT-002] Periodic keepalive, no reset\n");
  printf("  Device: %s Timeout: %lums Interval: %lums Duration: %lums\n",
         cfg->devpath, (unsigned long)cfg->timeout,
         (unsigned long)cfg->pingdelay, (unsigned long)cfg->pingtime);

  if (cfg->pingdelay >= cfg->timeout)
    {
      printf("  Failed: feed interval (%lums) must be < timeout (%lums)\n\n",
             (unsigned long)cfg->pingdelay, (unsigned long)cfg->timeout);
      return -EINVAL;
    }

  fd = open_wdt_device(cfg->devpath);
  if (fd < 0)
    {
      return fd;
    }

  ret = ioctl(fd, WDIOC_SETTIMEOUT, (unsigned long)cfg->timeout);
  if (ret < 0)
    {
      printf("  Failed: ioctl(WDIOC_SETTIMEOUT) errno=%d\n", errno);
      close(fd);
      return -errno;
    }

  ret = ioctl(fd, WDIOC_START, 0);
  if (ret < 0)
    {
      printf("  Failed: ioctl(WDIOC_START) errno=%d\n", errno);
      close(fd);
      return -errno;
    }

  start = now_ms();

  while (elapsed < cfg->pingtime)
    {
      usleep(cfg->pingdelay * 1000);

      ret = ioctl(fd, WDIOC_KEEPALIVE, 0);
      if (ret < 0)
        {
          printf("  Failed: ioctl(WDIOC_KEEPALIVE) errno=%d\n", errno);
          ioctl(fd, WDIOC_STOP, 0);
          close(fd);
          return -errno;
        }

      feeds++;
      elapsed = now_ms() - start;

      if (cfg->verbose &&
          ioctl(fd, WDIOC_GETSTATUS, (unsigned long)&status) >= 0)
        {
          printf("  fed #%u elapsed=%lums timeleft=%lums\n",
                 feeds, (unsigned long)elapsed,
                 (unsigned long)status.timeleft);
        }
      else
        {
          printf("  fed #%u elapsed=%lums\n", feeds, (unsigned long)elapsed);
        }

      fflush(stdout);
    }

  /* Survived the whole feeding window without a reset: feeding works.
   * Stop the watchdog so the device is not reset after we return.
   */

  ret = ioctl(fd, WDIOC_STOP, 0);
  if (ret < 0)
    {
      printf("  Warning: ioctl(WDIOC_STOP) errno=%d\n", errno);
    }

  close(fd);

  printf("  PASS: fed %u times over %lums, no reset; watchdog stopped\n\n",
         feeds, (unsigned long)elapsed);
  return 0;
}

/****************************************************************************
 * Name: run_case_003
 *
 * Description:
 *   Exercise lifecycle operations in deliberately surprising orders and
 *   verify timeout boundaries and live reconfiguration preserve state.
 *   This case always stops the watchdog before returning.
 *
 ****************************************************************************/

static int run_case_003(const struct app_config_s *cfg)
{
  struct watchdog_status_s status;
  uint32_t alternate_timeout;
  uint32_t first_timeleft;
  int second_fd = -1;
  int fd;
  int ret;

  memset(&status, 0, sizeof(status));
  printf("[WDT-003] Boundaries, live timeout and lifecycle, no reset\n");

  if (cfg->timeout < CASE003_MIN_TIMEOUT_MS)
    {
      printf("  FAIL: lifecycle timeout must be at least %dms\n",
             CASE003_MIN_TIMEOUT_MS);
      return -ERANGE;
    }

  fd = open_wdt_device(cfg->devpath);
  if (fd < 0)
    {
      return fd;
    }

  errno = 0;
  ret = ioctl(fd, WDIOC_SETTIMEOUT, 0);
  if (ret >= 0 || errno != ERANGE)
    {
      printf("  FAIL: zero timeout ret=%d errno=%d, expected ERANGE\n",
             ret, errno);
      ret = -EIO;
      goto fail;
    }

  errno = 0;
  ret = ioctl(fd, WDIOC_SETTIMEOUT, BL616CL_WDT_MAXTIMEOUT + 1);
  if (ret >= 0 || errno != ERANGE)
    {
      printf("  FAIL: oversized timeout ret=%d errno=%d, expected ERANGE\n",
             ret, errno);
      ret = -EIO;
      goto fail;
    }

  if (ioctl(fd, WDIOC_SETTIMEOUT, 1) < 0 ||
      ioctl(fd, WDIOC_SETTIMEOUT, BL616CL_WDT_MAXTIMEOUT) < 0)
    {
      printf("  FAIL: valid timeout boundary errno=%d\n", errno);
      ret = -errno;
      goto fail;
    }

  ret = ioctl(fd, WDIOC_SETTIMEOUT, (unsigned long)cfg->timeout);
  if (ret < 0 || ioctl(fd, WDIOC_START, 0) < 0)
    {
      printf("  FAIL: configure/start errno=%d\n", errno);
      ret = -errno;
      goto fail;
    }

  errno = 0;
  ret = ioctl(fd, WDIOC_START, 0);
  if (ret >= 0 || errno != EBUSY)
    {
      printf("  FAIL: duplicate start ret=%d errno=%d, expected EBUSY\n",
             ret, errno);
      ret = -EIO;
      goto stop_fail;
    }

  alternate_timeout = cfg->timeout == BL616CL_WDT_MAXTIMEOUT ?
                      cfg->timeout - 1 : cfg->timeout + 1;
  ret = ioctl(fd, WDIOC_SETTIMEOUT, alternate_timeout);
  if (ret < 0)
    {
      printf("  FAIL: live timeout change errno=%d\n", errno);
      ret = -errno;
      goto stop_fail;
    }

  ret = ioctl(fd, WDIOC_GETSTATUS, (unsigned long)(uintptr_t)&status);
  if (ret < 0 || (status.flags & WDFLAGS_ACTIVE) == 0 ||
      status.timeout != alternate_timeout ||
      status.timeleft > alternate_timeout)
    {
      printf("  FAIL: live update flags=0x%lx timeout=%lu timeleft=%lu\n",
             (unsigned long)status.flags, (unsigned long)status.timeout,
             (unsigned long)status.timeleft);
      ret = -EIO;
      goto stop_fail;
    }

  first_timeleft = status.timeleft;
  usleep(20000);
  ret = ioctl(fd, WDIOC_GETSTATUS, (unsigned long)(uintptr_t)&status);
  if (ret < 0 || status.timeleft >= first_timeleft)
    {
      printf("  FAIL: timeleft did not decrease first=%lu second=%lu\n",
             (unsigned long)first_timeleft,
             (unsigned long)status.timeleft);
      ret = -EIO;
      goto stop_fail;
    }

  ret = ioctl(fd, WDIOC_SETTIMEOUT, (unsigned long)cfg->timeout);
  if (ret < 0)
    {
      printf("  FAIL: restore live timeout errno=%d\n", errno);
      ret = -errno;
      goto stop_fail;
    }

  second_fd = open_wdt_device(cfg->devpath);
  if (second_fd < 0)
    {
      ret = second_fd;
      goto stop_fail;
    }

  ret = ioctl(second_fd, WDIOC_GETSTATUS,
              (unsigned long)(uintptr_t)&status);
  if (ret < 0 || (status.flags & WDFLAGS_ACTIVE) == 0 ||
      status.timeout != cfg->timeout)
    {
      printf("  FAIL: second fd did not share active state flags=0x%lx "
             "timeout=%lu\n", (unsigned long)status.flags,
             (unsigned long)status.timeout);
      ret = -EIO;
      goto second_stop_fail;
    }

  close(second_fd);
  second_fd = -1;

  ret = ioctl(fd, WDIOC_GETSTATUS, (unsigned long)(uintptr_t)&status);
  if (ret < 0 || (status.flags & WDFLAGS_ACTIVE) == 0)
    {
      printf("  FAIL: closing second fd stopped shared watchdog\n");
      ret = -EIO;
      goto stop_fail;
    }

  second_fd = open_wdt_device(cfg->devpath);
  if (second_fd < 0)
    {
      ret = second_fd;
      goto stop_fail;
    }

  if (ioctl(second_fd, WDIOC_KEEPALIVE, 0) < 0 ||
      ioctl(second_fd, WDIOC_STOP, 0) < 0)
    {
      printf("  FAIL: second fd KEEPALIVE/STOP errno=%d\n", errno);
      ret = -errno;
      goto second_fail;
    }

  close(second_fd);
  second_fd = -1;

  /* Repeated STOP and KEEPALIVE while inactive must be harmless and must not
   * accidentally arm the watchdog.
   */

  if (ioctl(fd, WDIOC_STOP, 0) < 0 || ioctl(fd, WDIOC_KEEPALIVE, 0) < 0)
    {
      printf("  FAIL: inactive STOP/KEEPALIVE errno=%d\n", errno);
      ret = -errno;
      goto fail;
    }

  ret = ioctl(fd, WDIOC_GETSTATUS, (unsigned long)(uintptr_t)&status);
  if (ret < 0 || (status.flags & WDFLAGS_ACTIVE) != 0 ||
      status.timeout != cfg->timeout)
    {
      printf("  FAIL: inactive lifecycle changed state flags=0x%lx "
             "timeout=%lu\n", (unsigned long)status.flags,
             (unsigned long)status.timeout);
      ret = -EIO;
      goto fail;
    }

  close(fd);
  printf("  PASS: boundaries rejected; live update and lifecycle "
         "preserved shared state\n\n");
  return 0;

second_stop_fail:
  (void)ioctl(second_fd, WDIOC_STOP, 0);
second_fail:
  close(second_fd);
stop_fail:
  (void)ioctl(fd, WDIOC_STOP, 0);
fail:
  close(fd);
  return ret < 0 ? ret : -EIO;
}

/****************************************************************************
 * Name: run_case_004
 ****************************************************************************/

static int run_case_004(const struct app_config_s *cfg)
{
#ifdef CONFIG_BL616CL_WDT_CAPTURE
  struct watchdog_capture_s capture;
  struct watchdog_status_s status;
  uint32_t timeleft;
  uint64_t deadline;
  int fd = -1;
  int ret = -EIO;
  bool captured = false;

  memset(&status, 0, sizeof(status));
  printf("[WDT-004] Interrupt capture and reset restore\n");
  fd = open_wdt_device(cfg->devpath);
  if (fd < 0)
    {
      return fd;
    }

  ret = ioctl(fd, WDIOC_SETTIMEOUT, (unsigned long)cfg->timeout);
  if (ret < 0)
    {
      printf("  Failed: ioctl(WDIOC_SETTIMEOUT) errno=%d\n", errno);
      ret = -errno;
      goto cleanup;
    }

  g_capture_count[0] = 0;
  g_capture_count[1] = 0;
  capture.newhandler = capture_callback_0;
  capture.oldhandler = NULL;
  ret = ioctl(fd, WDIOC_CAPTURE, (unsigned long)(uintptr_t)&capture);
  captured = ret >= 0;
  if (ret < 0 || capture.oldhandler != NULL)
    {
      printf("  FAIL: capture install ret=%d old=%p errno=%d\n", ret,
             capture.oldhandler, errno);
      ret = -EIO;
      goto cleanup;
    }

  ret = ioctl(fd, WDIOC_START, 0);
  if (ret < 0)
    {
      printf("  FAIL: capture start errno=%d\n", errno);
      ret = -errno;
      goto cleanup;
    }

  deadline = now_ms() + (uint64_t)cfg->timeout * 2;
  while (g_capture_count[0] == 0 && now_ms() < deadline)
    {
      usleep(10000);
    }

  ret = ioctl(fd, WDIOC_GETSTATUS, (unsigned long)(uintptr_t)&status);
  if (ret < 0 || (status.flags & WDFLAGS_CAPTURE) == 0 ||
      (status.flags & WDFLAGS_ACTIVE) == 0 ||
      (status.flags & WDFLAGS_RESET) != 0 || g_capture_count[0] != 1)
    {
      printf("  FAIL: capture count=%u flags=0x%lx errno=%d\n",
             g_capture_count[0], (unsigned long)status.flags, errno);
      ret = -EIO;
      goto cleanup;
    }

  printf("  callback0 count=%u flags=0x%lx\n", g_capture_count[0],
         (unsigned long)status.flags);

  if (ioctl(fd, WDIOC_KEEPALIVE, 0) < 0)
    {
      printf("  FAIL: capture keepalive errno=%d\n", errno);
      ret = -errno;
      goto cleanup;
    }

  ret = ioctl(fd, WDIOC_GETSTATUS, (unsigned long)(uintptr_t)&status);
  if (ret < 0)
    {
      printf("  FAIL: status before replace errno=%d\n", errno);
      ret = -errno;
      goto cleanup;
    }

  timeleft = status.timeleft;

  capture.newhandler = capture_callback_1;
  ret = ioctl(fd, WDIOC_CAPTURE, (unsigned long)(uintptr_t)&capture);
  if (ret < 0 || capture.oldhandler != capture_callback_0)
    {
      printf("  FAIL: capture replace ret=%d old=%p errno=%d\n", ret,
             capture.oldhandler, errno);
      ret = -EIO;
      goto cleanup;
    }

  ret = ioctl(fd, WDIOC_GETSTATUS, (unsigned long)(uintptr_t)&status);
  if (ret < 0 || status.timeleft > timeleft)
    {
      printf("  FAIL: handler replace extended deadline %lu -> %lu\n",
             (unsigned long)timeleft, (unsigned long)status.timeleft);
      ret = -EIO;
      goto cleanup;
    }

  deadline = now_ms() + (uint64_t)cfg->timeout * 2;
  while (g_capture_count[1] == 0 && now_ms() < deadline)
    {
      usleep(10000);
    }

  if (g_capture_count[0] != 1 || g_capture_count[1] != 1)
    {
      printf("  FAIL: capture replace counts=%u/%u\n",
             g_capture_count[0], g_capture_count[1]);
      ret = -EIO;
      goto cleanup;
    }

  ret = ioctl(fd, WDIOC_GETSTATUS, (unsigned long)(uintptr_t)&status);
  if (ret < 0)
    {
      printf("  FAIL: status before restore errno=%d\n", errno);
      ret = -errno;
      goto cleanup;
    }

  timeleft = status.timeleft;
  capture.newhandler = NULL;
  ret = ioctl(fd, WDIOC_CAPTURE, (unsigned long)(uintptr_t)&capture);
  if (ret < 0 || capture.oldhandler != capture_callback_1)
    {
      printf("  FAIL: capture restore ret=%d old=%p errno=%d\n", ret,
             capture.oldhandler, errno);
      ret = -EIO;
      goto cleanup;
    }

  captured = false;

  ret = ioctl(fd, WDIOC_GETSTATUS, (unsigned long)(uintptr_t)&status);
  if (ret < 0 || (status.flags & WDFLAGS_RESET) == 0 ||
      (status.flags & WDFLAGS_CAPTURE) != 0 || status.timeleft > timeleft)
    {
      printf("  FAIL: reset restore flags=0x%lx timeleft=%lu/%lu errno=%d\n",
             (unsigned long)status.flags, (unsigned long)timeleft,
             (unsigned long)status.timeleft, errno);
      ret = -EIO;
      goto cleanup;
    }

  ret = ioctl(fd, WDIOC_STOP, 0);
  printf("  %s: handlers replaced; capture cancelled; reset restored\n\n",
         ret == 0 ? "PASS" : "FAIL");
cleanup:
  if (captured)
    {
      capture.newhandler = NULL;
      (void)ioctl(fd, WDIOC_CAPTURE, (unsigned long)(uintptr_t)&capture);
    }

  (void)ioctl(fd, WDIOC_STOP, 0);
  close(fd);
  return ret;
#else
  printf("[WDT-004] SKIP: BL616CL_WDT_CAPTURE is disabled\n\n");
  return -ENOTSUP;
#endif
}

/****************************************************************************
 * Name: run_case_005
 ****************************************************************************/

static int run_case_005(const struct app_config_s *cfg)
{
#ifdef CONFIG_WATCHDOG_AUTOMONITOR
  struct watchdog_status_s status;
  enum boardioc_reset_cause_e cause = BOARDIOC_RESETCAUSE_NONE;
  uint64_t deadline;
  int fd;
  int ret;

  memset(&status, 0, sizeof(status));
  printf("[WDT-005] Automonitor stability and handoff\n");
  ret = read_reset_cause(&cause);
  if (ret < 0)
    {
      return ret;
    }

  if (cfg->status_only)
    {
      bool prev_was_wdt = cause == BOARDIOC_RESETCAUSE_SYS_RWDT;

      if (prev_was_wdt)
        {
          printf("  PASS: previous reset cause = WATCHDOG (SYS_RWDT)\n");
        }
      else
        {
          printf("  Previous reset cause = %d (not WATCHDOG)\n",
                 (int)cause);
        }

      printf("  status-only: automonitor handoff not performed\n\n");
      return prev_was_wdt ? 0 : -EAGAIN;
    }

  fd = open_wdt_device(cfg->devpath);
  if (fd < 0)
    {
      return fd;
    }

  ret = ioctl(fd, WDIOC_GETSTATUS, (unsigned long)(uintptr_t)&status);
  if (ret < 0 || (status.flags & WDFLAGS_ACTIVE) == 0)
    {
      printf("  FAIL: automonitor not active flags=0x%lx errno=%d\n",
             (unsigned long)status.flags, errno);
      close(fd);
      return -EIO;
    }

  printf("  monitor active flags=0x%lx; waiting %lums\n",
         (unsigned long)status.flags, (unsigned long)cfg->pingtime);
  usleep(cfg->pingtime * 1000);
  ret = ioctl(fd, WDIOC_GETSTATUS, (unsigned long)(uintptr_t)&status);
  if (ret < 0 || (status.flags & WDFLAGS_ACTIVE) == 0)
    {
      printf("  FAIL: monitor did not survive wait flags=0x%lx errno=%d\n",
             (unsigned long)status.flags, errno);
      close(fd);
      return -EIO;
    }

  ret = ioctl(fd, WDIOC_SETTIMEOUT, (unsigned long)cfg->timeout);
  if (ret < 0)
    {
      printf("  FAIL: handoff timeout errno=%d\n", errno);
      close(fd);
      return -errno;
    }

  ret = ioctl(fd, WDIOC_START, 0);
  if (ret < 0)
    {
      printf("  FAIL: handoff START errno=%d\n", errno);
      close(fd);
      return -errno;
    }

  printf("  handoff complete; no KEEPALIVE, device should reset\n");
  fflush(stdout);
  deadline = now_ms() + (uint64_t)cfg->timeout * CASE001_GUARD_MULTIPLE;
  while (now_ms() < deadline)
    {
      usleep(100000);
    }

  printf("  FAIL: watchdog did not reset after handoff\n\n");
  close(fd);
  return -ETIMEDOUT;
#else
  printf("[WDT-005] SKIP: WATCHDOG_AUTOMONITOR is disabled\n\n");
  return -ENOTSUP;
#endif
}

static int run_selected_cases(const struct app_config_s *cfg)
{
  int ret;

  if (strcmp(cfg->case_id, CASE_001) == 0)
    {
      return run_case_001(cfg);
    }

  if (strcmp(cfg->case_id, CASE_002) == 0)
    {
      return run_case_002(cfg);
    }

  if (strcmp(cfg->case_id, CASE_003) == 0)
    {
      return run_case_003(cfg);
    }

  if (strcmp(cfg->case_id, CASE_004) == 0)
    {
      return run_case_004(cfg);
    }

  if (strcmp(cfg->case_id, CASE_005) == 0)
    {
      return run_case_005(cfg);
    }

  /* "all": run the non-destructive feed case first, then the reset case
   * (which reboots the device and does not return).
   */

  ret = run_case_002(cfg);
  if (ret < 0)
    {
      return ret;
    }

  ret = run_case_004(cfg);
  if (ret < 0 && ret != -ENOTSUP)
    {
      return ret;
    }

  ret = run_case_003(cfg);
  if (ret < 0)
    {
      return ret;
    }

  return run_case_001(cfg);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  struct app_config_s cfg;
  int opt;
  int ret;

  cfg.devpath = DEFAULT_WDT_DEVPATH;
  cfg.case_id = CASE_ALL;
  cfg.timeout = DEFAULT_TIMEOUT_MS;
  cfg.pingtime = DEFAULT_PINGTIME_MS;
  cfg.pingdelay = DEFAULT_PINGDELAY_MS;
  cfg.status_only = false;
  cfg.verbose = false;

  while ((opt = getopt(argc, argv, "c:d:t:p:i:svh")) != -1)
    {
      switch (opt)
        {
          case 'c':
            cfg.case_id = optarg;
            break;
          case 'd':
            cfg.devpath = optarg;
            break;
          case 't':
            cfg.timeout = (uint32_t)strtoul(optarg, NULL, 0);
            break;
          case 'p':
            cfg.pingtime = (uint32_t)strtoul(optarg, NULL, 0);
            break;
          case 'i':
            cfg.pingdelay = (uint32_t)strtoul(optarg, NULL, 0);
            break;
          case 's':
            cfg.status_only = true;
            break;
          case 'v':
            cfg.verbose = true;
            break;
          case 'h':
          default:
            print_usage(argv[0]);
            return 0;
        }
    }

  if (strcmp(cfg.case_id, CASE_ALL) != 0 &&
      strcmp(cfg.case_id, CASE_001) != 0 &&
      strcmp(cfg.case_id, CASE_002) != 0 &&
      strcmp(cfg.case_id, CASE_003) != 0 &&
      strcmp(cfg.case_id, CASE_004) != 0 &&
      strcmp(cfg.case_id, CASE_005) != 0)
    {
      printf("Unsupported case id: %s\n", cfg.case_id);
      return ERROR;
    }

  if (cfg.timeout < 1 || cfg.timeout > BL616CL_WDT_MAXTIMEOUT)
    {
      printf("Invalid timeout: %lu (1..%d ms)\n",
             (unsigned long)cfg.timeout, BL616CL_WDT_MAXTIMEOUT);
      return ERROR;
    }

  printf("MCU Peripheral WDT Tests\n");
  printf("Case: %s Device: %s Timeout: %lums\n",
         cfg.case_id, cfg.devpath, (unsigned long)cfg.timeout);

  ret = run_selected_cases(&cfg);
  if (ret < 0)
    {
      printf("WDT case execution failed: ret=%d\n", ret);
      return ERROR;
    }

  printf("All selected WDT cases passed\n");
  return OK;
}
