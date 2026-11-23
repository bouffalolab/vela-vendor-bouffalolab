/****************************************************************************
 * vendor/bouffalolab/apps/mcu_peripheral_tests/adc/adc_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <sys/param.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DEFAULT_ADC_DEVPATH "/dev/adc0"

#define CASE_SKIPPED        1

/* Full scale of a single-ended conversion, in millivolts */

#define ADC_FULL_SCALE_MV   3200

#define READ_BATCH          64

/* Accuracy (003) and noise (004): rounds of samples per channel */

#define ACCURACY_ROUNDS     3
#define ACCURACY_SAMPLES    256
#define ZERO_LIMIT_MV       10
#define POINT_LIMIT_MV      20
#define MAX_POINTS          10

/* Polled scans (002) and lifetime rounds (005) */

#define SCAN_COUNT          1000
#define SCAN_LIMIT_US       10000
#define LIFETIME_ROUNDS     20
#define RESTART_COUNT       200

/* Samples read after a close that is not the last one (005): more than
 * the 64-entry upper-half queue of the continuous mode can hold, so some
 * were converted after the close.
 */

#define AFTER_CLOSE_SAMPLES 128

/* Continuous scan (007): rounds, length and minimum samples per round */

#define STREAM_ROUNDS       3
#define STREAM_SECONDS      10
#define STREAM_MIN_SAMPLES  10000

/* Overrun cases (008, 009): repetitions; the reader stall that fills the
 * upper-half queue; the time the high priority thread keeps the work queue
 * from draining the 16-sample FIFO (6 ms to fill at XCLK/64).
 */

#define OVERRUN_REPEATS     3
#define QUEUE_STALL_MS      500
#define HOG_MS              50

/* Longest wait for data that is expected to arrive */

#define DATA_TIMEOUT_MS     1000

/* poll() wake-up (010): delay before the other thread triggers */

#define WAKE_DELAY_MS       200

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct app_config_s
{
  const char *devpath;
  const char *case_id;
  const char *points;           /* -e channel:mV list, NULL if not wired */
  int meter_mv;                 /* -t meter error added to the limit */
};

/* Position in the scan order and number of samples checked */

struct order_s
{
  unsigned int next;
  unsigned long count;
};

struct stats_s
{
  int64_t sum;
  int64_t sumsq;
  int32_t min;
  int32_t max;
  uint32_t n;
};

struct point_s
{
  int index;                    /* Position of the channel in g_channels */
  int mv;                       /* Applied voltage */
};

struct trigger_arg_s
{
  const char *devpath;
  int delay_ms;
  int result;
};

struct hog_arg_s
{
  int ms;
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

/* Channels selected on the board, in scan order */

static const uint8_t g_channels[] =
{
#ifdef CONFIG_AI_M64P_KIT_ADC_CH0
  0,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH1
  1,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH2
  2,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH3
  3,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH4
  4,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH5
  5,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH6
  6,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH7
  7,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH8
  8,
#endif
#ifdef CONFIG_AI_M64P_KIT_ADC_CH9
  9,
#endif
};

#define NCHANNELS ((int)nitems(g_channels))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void print_usage(const char *progname)
{
  printf("Usage: %s [-c id|all] [-d dev] [-e ch:mV[,ch:mV...]] "
         "[-t mV]\n", progname);
  printf("  -c <id>  Case 001-011 or all (default: all)\n");
  printf("  -d <dev> ADC device (default: %s)\n", DEFAULT_ADC_DEVPATH);
  printf("  -e <list> Voltages applied to channels for case 003,\n"
         "           e.g. 0:0,1:1650; without it 003 is skipped\n");
  printf("  -t <mV>  Meter error added to the %d mV limit of nonzero\n"
         "           points (default: 0)\n", POINT_LIMIT_MV);
}

static uint64_t now_us(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int adc_open(const struct app_config_s *cfg, int oflags)
{
  int fd;

  fd = open(cfg->devpath, O_RDONLY | oflags);
  if (fd < 0)
    {
      printf("  FAIL: open %s errno=%d\n", cfg->devpath, errno);
      return -errno;
    }

  return fd;
}

static int adc_trigger(int fd)
{
  if (ioctl(fd, ANIOC_TRIGGER, 0) < 0)
    {
      return -errno;
    }

  return 0;
}

/* Read up to max messages; returns the count or a negated errno */

static int adc_read(int fd, struct adc_msg_s *msg, int max)
{
  ssize_t nbytes;

  nbytes = read(fd, msg, max * sizeof(*msg));
  if (nbytes < 0)
    {
      return -errno;
    }

  if (nbytes % sizeof(*msg) != 0)
    {
      printf("  FAIL: read returned %zd bytes, not whole messages\n",
             nbytes);
      return -EIO;
    }

  return nbytes / sizeof(*msg);
}

/* Wait up to timeout_ms for data on fd, then read it */

static int adc_read_wait(int fd, struct adc_msg_s *msg, int max,
                         int timeout_ms)
{
  struct pollfd pfd;
  int ret;

  pfd.fd = fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  ret = poll(&pfd, 1, timeout_ms);
  if (ret < 0)
    {
      return -errno;
    }

  if (ret == 0)
    {
      return -ETIMEDOUT;
    }

  return adc_read(fd, msg, max);
}

/* Check that the messages follow the scan order and are in range, and add
 * them to stats (one entry per scan position) if given.
 */

static int check_msgs(struct order_s *order, const struct adc_msg_s *msg,
                      int n, struct stats_s *stats)
{
  struct stats_s *st;
  int32_t mv;
  int i;

  for (i = 0; i < n; i++)
    {
      mv = msg[i].am_data;
      if (msg[i].am_channel != g_channels[order->next])
        {
          printf("  FAIL: sample %lu is channel %u, expected %u\n",
                 order->count, msg[i].am_channel,
                 g_channels[order->next]);
          return -EIO;
        }

      if (mv < 0 || mv > ADC_FULL_SCALE_MV)
        {
          printf("  FAIL: sample %lu channel %u is %" PRId32 " mV\n",
                 order->count, msg[i].am_channel, mv);
          return -EIO;
        }

      if (stats != NULL)
        {
          st = &stats[order->next];
          if (st->n == 0 || mv < st->min)
            {
              st->min = mv;
            }

          if (st->n == 0 || mv > st->max)
            {
              st->max = mv;
            }

          st->sum += mv;
          st->sumsq += (int64_t)mv * mv;
          st->n++;
        }

      order->next = (order->next + 1) % NCHANNELS;
      order->count++;
    }

  return 0;
}

static int expect_errno(int ret, int expected, const char *operation)
{
  if (ret != -expected)
    {
      printf("  FAIL: %s ret=%d expected=%d\n", operation, ret, -expected);
      return -EIO;
    }

  return 0;
}

#ifdef CONFIG_BL616CL_ADC_POLL
/* Read everything queued; the queue must hold count messages in order */

static int drain_expect(int fd, struct order_s *order, unsigned long count)
{
  struct adc_msg_s msg[READ_BATCH];
  unsigned long got = 0;
  int ret;

  for (; ; )
    {
      ret = adc_read(fd, msg, READ_BATCH);
      if (ret == -EAGAIN)
        {
          break;
        }

      if (ret < 0)
        {
          printf("  FAIL: drain read ret=%d\n", ret);
          return ret;
        }

      got += ret;
      ret = check_msgs(order, msg, ret, NULL);
      if (ret < 0)
        {
          return ret;
        }
    }

  if (got != count)
    {
      printf("  FAIL: queue held %lu samples, expected %lu\n", got, count);
      return -EIO;
    }

  return 0;
}
#endif /* CONFIG_BL616CL_ADC_POLL */

/* Collect `per_channel` samples of every channel into stats */

static int collect(const struct app_config_s *cfg, int per_channel,
                   struct stats_s *stats)
{
  struct adc_msg_s msg[READ_BATCH];
  struct order_s order;
  unsigned long total = (unsigned long)per_channel * NCHANNELS;
  int fd;
  int ret = 0;

  memset(stats, 0, NCHANNELS * sizeof(*stats));
  memset(&order, 0, sizeof(order));

  fd = adc_open(cfg, 0);
  if (fd < 0)
    {
      return fd;
    }

#ifdef CONFIG_BL616CL_ADC_IRQ
  ret = adc_trigger(fd);
  if (ret < 0)
    {
      printf("  FAIL: start ret=%d\n", ret);
      goto out;
    }
#endif

  while (order.count < total)
    {
#ifdef CONFIG_BL616CL_ADC_POLL
      ret = adc_trigger(fd);
      if (ret < 0)
        {
          printf("  FAIL: trigger ret=%d\n", ret);
          goto out;
        }
#endif

      ret = adc_read(fd, msg, MIN(READ_BATCH, total - order.count));
      if (ret < 0)
        {
          printf("  FAIL: read ret=%d after %lu samples\n", ret,
                 order.count);
          goto out;
        }

      ret = check_msgs(&order, msg, ret, stats);
      if (ret < 0)
        {
          goto out;
        }
    }

out:
  close(fd);
  return ret;
}

static void print_stats(int index, const struct stats_s *st)
{
  double mean = (double)st->sum / st->n;
  double var = (double)st->sumsq / st->n - mean * mean;
  double sd = 0.0;

  /* Newton steps for the square root; libm may not be linked */

  if (var > 0.0)
    {
      int i;

      sd = var;
      for (i = 0; i < 30; i++)
        {
          sd = 0.5 * (sd + var / sd);
        }
    }

  printf("    CH%u: n=%" PRIu32 " mean=%.1f mV sd=%.2f mV "
         "min=%" PRId32 " max=%" PRId32 " p-p=%" PRId32 "\n",
         g_channels[index], st->n, mean, sd, st->min, st->max,
         st->max - st->min);
}

static int parse_points(const char *list, struct point_s *points)
{
  const char *p = list;
  char *end;
  long ch;
  long mv;
  int n = 0;
  int i;

  while (*p != '\0')
    {
      ch = strtol(p, &end, 10);
      if (end == p || *end != ':' || n >= MAX_POINTS)
        {
          return -EINVAL;
        }

      p = end + 1;
      mv = strtol(p, &end, 10);
      if (end == p || mv < 0 || mv > ADC_FULL_SCALE_MV ||
          (*end != ',' && *end != '\0'))
        {
          return -EINVAL;
        }

      for (i = 0; i < NCHANNELS; i++)
        {
          if (g_channels[i] == ch)
            {
              break;
            }
        }

      if (i == NCHANNELS)
        {
          printf("  channel %ld is not selected on this board\n", ch);
          return -EINVAL;
        }

      points[n].index = i;
      points[n].mv = mv;
      n++;
      p = *end == ',' ? end + 1 : end;
    }

  return n;
}

/****************************************************************************
 * Name: run_case_001
 *
 * Description:
 *   The node reports the configured channel count, and holds no data before
 *   the first trigger.
 *
 ****************************************************************************/

static int run_case_001(const struct app_config_s *cfg)
{
  struct adc_msg_s msg[READ_BATCH];
  int fd;
  int ret;

  printf("[ADC-001] node, channel count and empty queue\n");

  fd = adc_open(cfg, O_NONBLOCK);
  if (fd < 0)
    {
      return fd;
    }

  ret = ioctl(fd, ANIOC_GET_NCHANNELS, 0);
  if (ret != NCHANNELS)
    {
      printf("  FAIL: ANIOC_GET_NCHANNELS=%d errno=%d expected=%d\n",
             ret, ret < 0 ? errno : 0, NCHANNELS);
      ret = -EIO;
      goto out;
    }

  ret = expect_errno(adc_read(fd, msg, READ_BATCH), EAGAIN,
                     "read before trigger");

out:
  close(fd);
  if (ret == 0)
    {
      printf("  [ADC-001] PASS channels=%d\n\n", NCHANNELS);
    }

  return ret;
}

/****************************************************************************
 * Name: run_case_002
 *
 * Description:
 *   Polled mode: every trigger queues exactly one sample of each channel,
 *   in scan order, within SCAN_LIMIT_US.
 *
 ****************************************************************************/

static int run_case_002(const struct app_config_s *cfg)
{
#ifdef CONFIG_BL616CL_ADC_POLL
  struct adc_msg_s msg[READ_BATCH];
  struct order_s order;
  uint64_t total = 0;
  uint64_t worst = 0;
  uint64_t t0;
  uint64_t dt;
  int fd;
  int ret = 0;
  int i;

  printf("[ADC-002] %d polled scans\n", SCAN_COUNT);

  fd = adc_open(cfg, O_NONBLOCK);
  if (fd < 0)
    {
      return fd;
    }

  memset(&order, 0, sizeof(order));
  for (i = 0; i < SCAN_COUNT; i++)
    {
      t0 = now_us();
      ret = adc_trigger(fd);
      dt = now_us() - t0;
      if (ret < 0)
        {
          printf("  FAIL: trigger %d ret=%d\n", i, ret);
          goto out;
        }

      total += dt;
      worst = MAX(worst, dt);

      ret = adc_read(fd, msg, READ_BATCH);
      if (ret != NCHANNELS)
        {
          printf("  FAIL: scan %d read %d samples, expected %d\n",
                 i, ret, NCHANNELS);
          ret = -EIO;
          goto out;
        }

      ret = check_msgs(&order, msg, NCHANNELS, NULL);
      if (ret < 0)
        {
          goto out;
        }
    }

  ret = expect_errno(adc_read(fd, msg, READ_BATCH), EAGAIN,
                     "read after the last scan");
  if (ret == 0 && worst > SCAN_LIMIT_US)
    {
      printf("  FAIL: slowest trigger %" PRIu64 " us\n", worst);
      ret = -EIO;
    }

out:
  close(fd);
  if (ret == 0)
    {
      printf("  trigger avg=%" PRIu64 " us max=%" PRIu64 " us\n",
             total / SCAN_COUNT, worst);
      printf("  [ADC-002] PASS samples=%lu\n\n", order.count);
    }

  return ret;
#else
  printf("[ADC-002] SKIP: polled mode only\n\n");
  return CASE_SKIPPED;
#endif
}

/****************************************************************************
 * Name: run_case_003
 *
 * Description:
 *   Known voltages: the mean of every round is within ZERO_LIMIT_MV of a
 *   grounded input and within POINT_LIMIT_MV plus the meter error of any
 *   other point, and higher voltages read higher.
 *
 ****************************************************************************/

static int run_case_003(const struct app_config_s *cfg)
{
  struct point_s points[MAX_POINTS];
  struct stats_s stats[NCHANNELS];
  int32_t mean[MAX_POINTS];
  int npoints;
  int failures = 0;
  int limit;
  int round;
  int ret;
  int i;
  int j;

  if (cfg->points == NULL)
    {
      printf("[ADC-003] SKIP: no -e voltages given\n\n");
      return CASE_SKIPPED;
    }

  npoints = parse_points(cfg->points, points);
  if (npoints <= 0)
    {
      printf("[ADC-003] FAIL: bad -e list '%s'\n", cfg->points);
      return -EINVAL;
    }

  printf("[ADC-003] known voltages, %d rounds x %d samples\n",
         ACCURACY_ROUNDS, ACCURACY_SAMPLES);

  for (round = 0; round < ACCURACY_ROUNDS; round++)
    {
      ret = collect(cfg, ACCURACY_SAMPLES, stats);
      if (ret < 0)
        {
          return ret;
        }

      printf("  round %d\n", round + 1);
      for (i = 0; i < npoints; i++)
        {
          const struct stats_s *st = &stats[points[i].index];

          mean[i] = (int32_t)((st->sum + (int64_t)st->n / 2) / st->n);
          limit = points[i].mv == 0 ? ZERO_LIMIT_MV :
                                      POINT_LIMIT_MV + cfg->meter_mv;
          print_stats(points[i].index, st);
          printf("      applied=%d mV error=%" PRId32 " mV limit=%d %s\n",
                 points[i].mv, mean[i] - points[i].mv, limit,
                 abs(mean[i] - points[i].mv) <= limit ? "ok" : "FAIL");
          if (abs(mean[i] - points[i].mv) > limit)
            {
              failures++;
            }
        }

      for (i = 0; i < npoints; i++)
        {
          for (j = 0; j < npoints; j++)
            {
              if (points[i].mv < points[j].mv && mean[i] >= mean[j])
                {
                  printf("  FAIL: %d mV reads %" PRId32 ", not below "
                         "%d mV reading %" PRId32 "\n", points[i].mv,
                         mean[i], points[j].mv, mean[j]);
                  failures++;
                }
            }
        }
    }

  if (failures > 0)
    {
      return -EIO;
    }

  printf("  [ADC-003] PASS points=%d\n\n", npoints);
  return 0;
}

/****************************************************************************
 * Name: run_case_004
 *
 * Description:
 *   Noise report of every channel over the accuracy rounds. Only read
 *   errors fail; the figures describe whatever drives the inputs.
 *
 ****************************************************************************/

static int run_case_004(const struct app_config_s *cfg)
{
  struct stats_s stats[NCHANNELS];
  int round;
  int ret;
  int i;

  printf("[ADC-004] noise, %d rounds x %d samples (report only)\n",
         ACCURACY_ROUNDS, ACCURACY_SAMPLES);

  for (round = 0; round < ACCURACY_ROUNDS; round++)
    {
      ret = collect(cfg, ACCURACY_SAMPLES, stats);
      if (ret < 0)
        {
          return ret;
        }

      printf("  round %d\n", round + 1);
      for (i = 0; i < NCHANNELS; i++)
        {
          print_stats(i, &stats[i]);
        }
    }

  printf("  [ADC-004] PASS\n\n");
  return 0;
}

/****************************************************************************
 * Name: run_case_005
 *
 * Description:
 *   Two descriptors share one queue; the last close drops the queue and,
 *   in the continuous mode, stops the scan, so a new open finds no stale
 *   data. A second start of a running scan is refused.
 *
 ****************************************************************************/

static int run_case_005(const struct app_config_s *cfg)
{
  struct adc_msg_s msg[READ_BATCH];
  struct order_s order;
#ifdef CONFIG_BL616CL_ADC_IRQ
  unsigned long after;
#endif
  int fda = -1;
  int fdb = -1;
  int fdc = -1;
  int round;
  int ret = 0;

  printf("[ADC-005] shared queue and close/reopen, %d rounds\n",
         LIFETIME_ROUNDS);

  for (round = 0; round < LIFETIME_ROUNDS && ret == 0; round++)
    {
      memset(&order, 0, sizeof(order));
      fda = adc_open(cfg, O_NONBLOCK);
      fdb = adc_open(cfg, O_NONBLOCK);
      if (fda < 0 || fdb < 0)
        {
          ret = -EIO;
          break;
        }

#ifdef CONFIG_BL616CL_ADC_POLL
      /* A scan triggered on one descriptor is read on the other */

      ret = adc_trigger(fda);
      if (ret == 0)
        {
          ret = drain_expect(fdb, &order, NCHANNELS);
        }

      /* A close that is not the last keeps the queue */

      if (ret == 0)
        {
          ret = adc_trigger(fda);
        }

      close(fda);
      fda = -1;
      if (ret == 0)
        {
          ret = drain_expect(fdb, &order, NCHANNELS);
        }

      /* Leave a scan queued: the last close must drop it */

      if (ret == 0)
        {
          ret = adc_trigger(fdb);
        }
#else
      ret = adc_trigger(fda);
      if (ret == 0)
        {
          ret = expect_errno(adc_trigger(fdb), EBUSY, "second start");
        }

      if (ret == 0)
        {
          ret = adc_read_wait(fdb, msg, READ_BATCH, DATA_TIMEOUT_MS);
          ret = ret > 0 ? check_msgs(&order, msg, ret, NULL) : -EIO;
        }

      /* Not the last close: the scan goes on */

      close(fda);
      fda = -1;
      after = order.count + AFTER_CLOSE_SAMPLES;
      while (ret == 0 && order.count < after)
        {
          ret = adc_read_wait(fdb, msg, READ_BATCH, DATA_TIMEOUT_MS);
          ret = ret > 0 ? check_msgs(&order, msg, ret, NULL) : -EIO;
        }
#endif

      close(fdb);
      fdb = -1;
      if (ret < 0)
        {
          printf("  FAIL: round %d before reopen\n", round + 1);
          break;
        }

      /* After the last close nothing is queued and nothing arrives */

      fdc = adc_open(cfg, O_NONBLOCK);
      if (fdc < 0)
        {
          ret = fdc;
          break;
        }

      ret = expect_errno(adc_read(fdc, msg, READ_BATCH), EAGAIN,
                         "read after reopen");
      usleep(50 * 1000);
      if (ret == 0)
        {
          ret = expect_errno(adc_read(fdc, msg, READ_BATCH), EAGAIN,
                             "read 50 ms after reopen");
        }

      /* The device works again, from the first channel */

      memset(&order, 0, sizeof(order));
      if (ret == 0)
        {
          ret = adc_trigger(fdc);
        }

      if (ret == 0)
        {
          ret = adc_read_wait(fdc, msg, READ_BATCH, DATA_TIMEOUT_MS);
          ret = ret > 0 ? check_msgs(&order, msg, ret, NULL) : -EIO;
        }

      close(fdc);
      fdc = -1;
      if (ret < 0)
        {
          printf("  FAIL: round %d after reopen\n", round + 1);
        }
    }

  if (fda >= 0)
    {
      close(fda);
    }

  if (fdb >= 0)
    {
      close(fdb);
    }

  if (ret == 0)
    {
      printf("  [ADC-005] PASS rounds=%d\n\n", LIFETIME_ROUNDS);
    }

  return ret;
}

/****************************************************************************
 * Name: run_case_006
 *
 * Description:
 *   Polled mode: once the queue cannot take another scan, the trigger
 *   fails with EOVERFLOW and queues nothing; the queued scans stay intact
 *   and the next trigger after reading them succeeds.
 *
 ****************************************************************************/

static int run_case_006(const struct app_config_s *cfg)
{
#ifdef CONFIG_BL616CL_ADC_POLL
  struct adc_msg_s msg[READ_BATCH];
  struct order_s order;
  int scans;
  int fd;
  int ret = 0;

  printf("[ADC-006] queue full in polled mode\n");

  fd = adc_open(cfg, O_NONBLOCK);
  if (fd < 0)
    {
      return fd;
    }

  for (scans = 0; scans < 100; scans++)
    {
      ret = adc_trigger(fd);
      if (ret < 0)
        {
          break;
        }
    }

  if (ret != -EOVERFLOW || scans < 2)
    {
      printf("  FAIL: trigger %d ret=%d, expected EOVERFLOW after at "
             "least 2 scans\n", scans + 1, ret);
      ret = -EIO;
      goto out;
    }

  memset(&order, 0, sizeof(order));
  ret = drain_expect(fd, &order, (unsigned long)scans * NCHANNELS);
  if (ret == 0)
    {
      ret = adc_trigger(fd);
    }

  if (ret == 0)
    {
      ret = adc_read(fd, msg, READ_BATCH);
      ret = ret == NCHANNELS ? check_msgs(&order, msg, ret, NULL) : -EIO;
    }

out:
  close(fd);
  if (ret == 0)
    {
      printf("  [ADC-006] PASS queued scans=%d\n\n", scans);
    }

  return ret;
#else
  printf("[ADC-006] SKIP: polled mode only\n\n");
  return CASE_SKIPPED;
#endif
}

#ifdef CONFIG_BL616CL_ADC_IRQ
/* Read until read() fails; the failure must be one EIO, then EAGAIN.
 * poll() does not report an overrun once the queue is empty, so this polls
 * read() itself. Returns the number of samples read before the EIO or a
 * negated errno.
 */

static int read_until_overrun(int fd, struct order_s *order)
{
  struct adc_msg_s msg[READ_BATCH];
  uint64_t end = now_us() + DATA_TIMEOUT_MS * 1000ull;
  int got = 0;
  int ret;

  for (; ; )
    {
      if (now_us() > end)
        {
          printf("  FAIL: no EIO within %d ms, %d samples read\n",
                 DATA_TIMEOUT_MS, got);
          return -EIO;
        }

      ret = adc_read(fd, msg, READ_BATCH);
      if (ret == -EAGAIN)
        {
          usleep(10 * 1000);
          continue;
        }

      if (ret < 0)
        {
          break;
        }

      got += ret;
      ret = check_msgs(order, msg, ret, NULL);
      if (ret < 0)
        {
          return ret;
        }
    }

  if (expect_errno(ret, EIO, "read after the loss") < 0 ||
      expect_errno(adc_read(fd, msg, READ_BATCH), EAGAIN,
                   "read after EIO") < 0)
    {
      return -EIO;
    }

  /* The scan has stopped */

  usleep(100 * 1000);
  if (expect_errno(adc_read(fd, msg, READ_BATCH), EAGAIN,
                   "read 100 ms after EIO") < 0)
    {
      return -EIO;
    }

  return got;
}

/* Start again after an overrun; data comes from the first channel */

static int restart_after_overrun(int fd)
{
  struct adc_msg_s msg[READ_BATCH];
  struct order_s order;
  int ret;

  memset(&order, 0, sizeof(order));
  ret = adc_trigger(fd);
  if (ret == 0)
    {
      ret = adc_read_wait(fd, msg, READ_BATCH, DATA_TIMEOUT_MS);
      ret = ret > 0 ? check_msgs(&order, msg, ret, NULL) : -EIO;
    }

  if (ret < 0)
    {
      printf("  FAIL: restart after overrun ret=%d\n", ret);
    }

  return ret;
}

static void *hog_thread(void *arg)
{
  struct hog_arg_s *hog = arg;
  uint64_t end = now_us() + (uint64_t)hog->ms * 1000;

  while (now_us() < end)
    {
    }

  return NULL;
}

/* Keep every lower priority thread, the high priority work queue too, off
 * the CPU for ms milliseconds.
 */

static int hog_cpu(int ms)
{
  struct sched_param param;
  struct hog_arg_s hog;
  pthread_attr_t attr;
  pthread_t thread;
  int ret;

  hog.ms = ms;
  param.sched_priority = sched_get_priority_max(SCHED_FIFO);

  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

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
      ret = pthread_create(&thread, &attr, hog_thread, &hog);
    }

  pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      printf("  FAIL: hog thread create ret=%d\n", ret);
      return -ret;
    }

  pthread_join(thread, NULL);
  return 0;
}
#endif /* CONFIG_BL616CL_ADC_IRQ */

/****************************************************************************
 * Name: run_case_007
 *
 * Description:
 *   Continuous mode: STREAM_ROUNDS scans of STREAM_SECONDS each deliver at
 *   least STREAM_MIN_SAMPLES samples in scan order without a loss.
 *
 ****************************************************************************/

static int run_case_007(const struct app_config_s *cfg)
{
#ifdef CONFIG_BL616CL_ADC_IRQ
  struct adc_msg_s msg[READ_BATCH];
  struct order_s order;
  uint64_t end;
  int round;
  int fd;
  int ret = 0;

  printf("[ADC-007] continuous scan, %d x %d s\n", STREAM_ROUNDS,
         STREAM_SECONDS);

  for (round = 0; round < STREAM_ROUNDS && ret == 0; round++)
    {
      memset(&order, 0, sizeof(order));
      fd = adc_open(cfg, 0);
      if (fd < 0)
        {
          return fd;
        }

      ret = adc_trigger(fd);
      end = now_us() + STREAM_SECONDS * 1000000ull;
      while (ret == 0 && now_us() < end)
        {
          ret = adc_read(fd, msg, READ_BATCH);
          ret = ret > 0 ? check_msgs(&order, msg, ret, NULL) :
                          (ret < 0 ? ret : -EIO);
        }

      close(fd);
      printf("  round %d: samples=%lu (%lu/s) ret=%d\n", round + 1,
             order.count, order.count / STREAM_SECONDS, ret);
      if (ret == 0 && order.count < STREAM_MIN_SAMPLES)
        {
          printf("  FAIL: fewer than %d samples\n", STREAM_MIN_SAMPLES);
          ret = -EIO;
        }
    }

  if (ret == 0)
    {
      printf("  [ADC-007] PASS\n\n");
    }

  return ret;
#else
  printf("[ADC-007] SKIP: continuous mode only\n\n");
  return CASE_SKIPPED;
#endif
}

/****************************************************************************
 * Name: run_case_008
 *
 * Description:
 *   Continuous mode: a reader that stalls lets the upper-half queue fill.
 *   The scan stops; read() returns the queued samples in order, one EIO,
 *   then EAGAIN; a new trigger starts again.
 *
 ****************************************************************************/

static int run_case_008(const struct app_config_s *cfg)
{
#ifdef CONFIG_BL616CL_ADC_IRQ
  struct order_s order;
  int repeat;
  int fd;
  int ret = 0;

  printf("[ADC-008] queue full in continuous mode, %d times\n",
         OVERRUN_REPEATS);

  fd = adc_open(cfg, O_NONBLOCK);
  if (fd < 0)
    {
      return fd;
    }

  for (repeat = 0; repeat < OVERRUN_REPEATS && ret >= 0; repeat++)
    {
      memset(&order, 0, sizeof(order));
      ret = adc_trigger(fd);
      if (ret < 0)
        {
          printf("  FAIL: start ret=%d\n", ret);
          break;
        }

      usleep(QUEUE_STALL_MS * 1000);
      ret = read_until_overrun(fd, &order);
      if (ret >= 0)
        {
          printf("  repeat %d: %d samples before EIO\n", repeat + 1, ret);
        }
    }

  if (ret >= 0)
    {
      ret = restart_after_overrun(fd);
    }

  close(fd);
  if (ret >= 0)
    {
      printf("  [ADC-008] PASS\n\n");
      ret = 0;
    }

  return ret;
#else
  printf("[ADC-008] SKIP: continuous mode only\n\n");
  return CASE_SKIPPED;
#endif
}

/****************************************************************************
 * Name: run_case_009
 *
 * Description:
 *   Continuous mode: a thread above the high priority work queue keeps it
 *   from draining the hardware FIFO until the FIFO overruns. The scan
 *   stops with the samples taken before the overrun, one EIO, then EAGAIN;
 *   with CONFIG_DEBUG_ANALOG_WARN the driver logs "FIFO overrun".
 *
 ****************************************************************************/

static int run_case_009(const struct app_config_s *cfg)
{
#ifdef CONFIG_BL616CL_ADC_IRQ
  struct adc_msg_s msg[READ_BATCH];
  struct order_s order;
  int repeat;
  int fd;
  int ret = 0;

  printf("[ADC-009] hardware FIFO overrun, %d times\n", OVERRUN_REPEATS);

  fd = adc_open(cfg, O_NONBLOCK);
  if (fd < 0)
    {
      return fd;
    }

  for (repeat = 0; repeat < OVERRUN_REPEATS && ret >= 0; repeat++)
    {
      memset(&order, 0, sizeof(order));
      ret = adc_trigger(fd);
      if (ret == 0)
        {
          ret = adc_read_wait(fd, msg, READ_BATCH, DATA_TIMEOUT_MS);
          ret = ret > 0 ? check_msgs(&order, msg, ret, NULL) : -EIO;
        }

      if (ret == 0)
        {
          ret = hog_cpu(HOG_MS);
        }

      if (ret == 0)
        {
          ret = read_until_overrun(fd, &order);
        }

      /* Only the 16 FIFO samples (and what was queued before the stall)
       * can arrive; a full upper-half queue would mean the wrong loss.
       */

      if (ret >= READ_BATCH - 1)
        {
          printf("  FAIL: %d samples before EIO, the queue filled\n", ret);
          ret = -EIO;
        }

      if (ret >= 0)
        {
          printf("  repeat %d: %d samples after the stall\n",
                 repeat + 1, ret);
        }
    }

  if (ret >= 0)
    {
      ret = restart_after_overrun(fd);
    }

  close(fd);
  if (ret >= 0)
    {
      printf("  [ADC-009] PASS\n\n");
      ret = 0;
    }

  return ret;
#else
  printf("[ADC-009] SKIP: continuous mode only\n\n");
  return CASE_SKIPPED;
#endif
}

static void *trigger_thread(void *arg)
{
  struct trigger_arg_s *trig = arg;
  int fd;

  usleep(trig->delay_ms * 1000);
  fd = open(trig->devpath, O_RDONLY | O_NONBLOCK);
  if (fd < 0)
    {
      trig->result = -errno;
      return NULL;
    }

  trig->result = adc_trigger(fd);
  close(fd);
  return NULL;
}

/****************************************************************************
 * Name: run_case_010
 *
 * Description:
 *   poll() on an empty queue times out, and wakes with POLLIN when another
 *   thread triggers a scan.
 *
 ****************************************************************************/

static int run_case_010(const struct app_config_s *cfg)
{
  struct adc_msg_s msg[READ_BATCH];
  struct trigger_arg_s trig;
  struct order_s order;
  struct pollfd pfd;
  pthread_t thread;
  uint64_t t0;
  uint64_t waited;
  int fd;
  int ret;

  printf("[ADC-010] poll() wake-up\n");

  fd = adc_open(cfg, O_NONBLOCK);
  if (fd < 0)
    {
      return fd;
    }

  pfd.fd = fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  ret = poll(&pfd, 1, 100);
  if (ret != 0)
    {
      printf("  FAIL: poll on an empty queue ret=%d revents=0x%x\n", ret,
             (unsigned int)pfd.revents);
      close(fd);
      return -EIO;
    }

  trig.devpath = cfg->devpath;
  trig.delay_ms = WAKE_DELAY_MS;
  trig.result = -EINPROGRESS;
  ret = pthread_create(&thread, NULL, trigger_thread, &trig);
  if (ret != 0)
    {
      printf("  FAIL: trigger thread create ret=%d\n", ret);
      close(fd);
      return -ret;
    }

  t0 = now_us();
  pfd.revents = 0;
  ret = poll(&pfd, 1, WAKE_DELAY_MS + DATA_TIMEOUT_MS);
  waited = (now_us() - t0) / 1000;
  pthread_join(thread, NULL);

  if (ret != 1 || (pfd.revents & POLLIN) == 0 || trig.result != 0 ||
      waited < WAKE_DELAY_MS / 2)
    {
      printf("  FAIL: poll ret=%d revents=0x%x after %" PRIu64 " ms, "
             "trigger ret=%d\n", ret, (unsigned int)pfd.revents, waited,
             trig.result);
      close(fd);
      return -EIO;
    }

  memset(&order, 0, sizeof(order));
  ret = adc_read(fd, msg, READ_BATCH);
  ret = ret > 0 ? check_msgs(&order, msg, ret, NULL) : -EIO;
  close(fd);
  if (ret == 0)
    {
      printf("  [ADC-010] PASS woke after %" PRIu64 " ms\n\n", waited);
    }

  return ret;
}

/****************************************************************************
 * Name: run_case_011
 *
 * Description:
 *   Open, start, read and close in a tight loop: every start right after a
 *   stop must deliver the first channel first. In the continuous mode a
 *   stop does not end the scan round, and a quick restart used to deliver
 *   the rest of that round first.
 *
 ****************************************************************************/

static int run_case_011(const struct app_config_s *cfg)
{
  struct adc_msg_s msg[READ_BATCH];
  struct order_s order;
  int i;
  int fd;
  int ret = 0;

  printf("[ADC-011] start right after stop, %d times\n", RESTART_COUNT);

  for (i = 0; i < RESTART_COUNT && ret == 0; i++)
    {
      memset(&order, 0, sizeof(order));
      fd = adc_open(cfg, O_NONBLOCK);
      if (fd < 0)
        {
          return fd;
        }

      ret = adc_trigger(fd);
      if (ret == 0)
        {
          ret = adc_read_wait(fd, msg, READ_BATCH, DATA_TIMEOUT_MS);
          ret = ret > 0 ? check_msgs(&order, msg, ret, NULL) : -EIO;
        }

      close(fd);
      if (ret < 0)
        {
          printf("  FAIL: start %d\n", i + 1);
        }
    }

  if (ret == 0)
    {
      printf("  [ADC-011] PASS starts=%d\n\n", RESTART_COUNT);
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  static const struct case_entry_s cases[] = {
    { "001", run_case_001 },
    { "002", run_case_002 },
    { "003", run_case_003 },
    { "004", run_case_004 },
    { "005", run_case_005 },
    { "006", run_case_006 },
    { "007", run_case_007 },
    { "008", run_case_008 },
    { "009", run_case_009 },
    { "010", run_case_010 },
    { "011", run_case_011 },
  };

  struct app_config_s cfg;
  bool found = false;
  int executed = 0;
  int passed = 0;
  int skipped = 0;
  int opt;
  int ret;
  int i;

  cfg.devpath = DEFAULT_ADC_DEVPATH;
  cfg.case_id = "all";
  cfg.points = NULL;
  cfg.meter_mv = 0;

  while ((opt = getopt(argc, argv, "c:d:e:t:h")) != -1)
    {
      switch (opt)
        {
          case 'c':
            cfg.case_id = optarg;
            break;
          case 'd':
            cfg.devpath = optarg;
            break;
          case 'e':
            cfg.points = optarg;
            break;
          case 't':
            cfg.meter_mv = atoi(optarg);
            break;
          case 'h':
          default:
            print_usage(argv[0]);
            return OK;
        }
    }

  if (cfg.meter_mv < 0)
    {
      printf("Invalid meter error: %d\n", cfg.meter_mv);
      return ERROR;
    }

  printf("MCU Peripheral ADC Tests\n");
  printf("Case: %s Device: %s Mode: %s ADC clock: XCLK/%d\n",
         cfg.case_id, cfg.devpath,
#ifdef CONFIG_BL616CL_ADC_POLL
         "polled scan",
#else
         "continuous scan",
#endif
         CONFIG_BL616CL_ADC_CLK_DIV);
  printf("Channels in scan order:");
  for (i = 0; i < NCHANNELS; i++)
    {
      printf(" CH%u", g_channels[i]);
    }

  printf("\n\n");

  for (i = 0; i < (int)nitems(cases); i++)
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
          printf("  [ADC-%s] FAIL ret=%d\n\n", cases[i].id, ret);
        }
    }

  if (!found)
    {
      printf("Unsupported case id: %s\n", cfg.case_id);
      print_usage(argv[0]);
      return ERROR;
    }

  printf("ADC Summary: executed=%d passed=%d skipped=%d failed=%d -> %s\n",
         executed, passed, skipped, executed - passed - skipped,
         executed == passed + skipped ? "PASS" : "FAIL");

  return executed == passed + skipped ? OK : ERROR;
}
