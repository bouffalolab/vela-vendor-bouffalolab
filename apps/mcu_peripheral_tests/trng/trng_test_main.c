/****************************************************************************
 * apps/vendor/bouffalolab/apps/mcu_peripheral_tests/trng/trng_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <sys/random.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TRNG_BLOCK_SIZE    32
#define TRNG_STATS_SIZE    4096
#define TRNG_MAX_READ_SIZE 257
#define TRNG_CANARY_HEAD   0x5a
#define TRNG_CANARY_TAIL   0xa5
#define TRNG_INVALID_IOCTL 0x7fffffff

#ifdef CONFIG_DEV_RANDOM
#define TRNG_DEVICE "/dev/random"
#else
#define TRNG_DEVICE "/dev/urandom"
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct trng_worker_s
{
  int id;
  int result;
  size_t bytes;
  uint32_t checksum;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const size_t g_lengths[] =
{
  0, 1, 3, 4, 31, 32, 33, 255, 256, 257
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void trng_usage(FAR const char *progname)
{
  printf("Usage: %s <all|lengths|api|stats|concurrent>\n", progname);
}

static bool trng_all_value(FAR const uint8_t *buffer, size_t len,
                           uint8_t value)
{
  size_t i;

  for (i = 0; i < len; i++)
    {
      if (buffer[i] != value)
        {
          return false;
        }
    }

  return true;
}

static uint32_t trng_checksum(FAR const uint8_t *buffer, size_t len)
{
  uint32_t hash = UINT32_C(2166136261);
  size_t i;

  for (i = 0; i < len; i++)
    {
      hash ^= buffer[i];
      hash *= UINT32_C(16777619);
    }

  return hash;
}

static int trng_read_exact(int fd, FAR uint8_t *buffer, size_t len)
{
  ssize_t nread = read(fd, buffer, len);

  if (nread != (ssize_t)len)
    {
      printf("TRNG_TEST read FAIL requested=%zu actual=%zd errno=%d\n",
             len, nread, errno);
      return -1;
    }

  return 0;
}

static int trng_test_lengths(void)
{
  uint32_t guarded_words[(TRNG_MAX_READ_SIZE + 3 + sizeof(uint32_t) - 1) /
                         sizeof(uint32_t)];
  FAR uint8_t *guarded = (FAR uint8_t *)guarded_words;
  FAR uint8_t *buffer = &guarded[1];
  int fd;
  size_t i;

  if (((uintptr_t)buffer & (sizeof(uint32_t) - 1)) == 0)
    {
      printf("TRNG_TEST lengths FAIL buffer-aligned address=%p\n", buffer);
      return -1;
    }

  fd = open(TRNG_DEVICE, O_RDONLY);
  if (fd < 0)
    {
      printf("TRNG_TEST lengths FAIL open errno=%d\n", errno);
      return -1;
    }

  for (i = 0; i < sizeof(g_lengths) / sizeof(g_lengths[0]); i++)
    {
      size_t len = g_lengths[i];

      memset(guarded_words, 0xcc, sizeof(guarded_words));
      guarded[0] = TRNG_CANARY_HEAD;
      guarded[len + 1] = TRNG_CANARY_TAIL;

      if (trng_read_exact(fd, buffer, len) < 0 ||
          guarded[0] != TRNG_CANARY_HEAD ||
          guarded[len + 1] != TRNG_CANARY_TAIL)
        {
          printf("TRNG_TEST lengths FAIL len=%zu head=%02x tail=%02x\n",
                 len, guarded[0], guarded[len + 1]);
          close(fd);
          return -1;
        }

      if (len >= TRNG_BLOCK_SIZE &&
          (trng_all_value(buffer, len, 0x00) ||
           trng_all_value(buffer, len, 0xff)))
        {
          printf("TRNG_TEST lengths FAIL len=%zu fixed-value\n", len);
          close(fd);
          return -1;
        }

      printf("TRNG_TEST length PASS len=%zu align=%lu checksum=%08" PRIx32
             "\n",
             len, (unsigned long)((uintptr_t)buffer & 3),
             trng_checksum(buffer, len));
    }

  close(fd);
  printf("TRNG_TEST RESULT case=lengths PASS count=%zu\n",
         sizeof(g_lengths) / sizeof(g_lengths[0]));
  return 0;
}

static int trng_test_api(void)
{
  uint8_t buffer[TRNG_MAX_READ_SIZE];
  struct pollfd pfd;
  ssize_t nread;
  int fd;
  int ret;

  fd = open(TRNG_DEVICE, O_RDONLY | O_NONBLOCK);
  if (fd < 0)
    {
      printf("TRNG_TEST api FAIL open errno=%d\n", errno);
      return -1;
    }

  pfd.fd = fd;
  pfd.events = POLLIN | POLLOUT;
  pfd.revents = 0;
  ret = poll(&pfd, 1, 0);
  if (ret != 1 || (pfd.revents & POLLIN) == 0 ||
      (pfd.revents & (POLLOUT | POLLERR | POLLHUP | POLLNVAL)) != 0)
    {
      printf("TRNG_TEST api FAIL poll ret=%d revents=%04lx errno=%d\n",
             ret, (unsigned long)pfd.revents, errno);
      close(fd);
      return -1;
    }

  errno = 0;
  ret = ioctl(fd, TRNG_INVALID_IOCTL, 0UL);
  if (ret != -1 || errno != ENOTTY)
    {
      printf("TRNG_TEST api FAIL ioctl ret=%d errno=%d\n", ret, errno);
      close(fd);
      return -1;
    }

  close(fd);

#ifdef CONFIG_DEV_RANDOM
  nread = getrandom(buffer, sizeof(buffer), GRND_RANDOM | GRND_NONBLOCK);
  if (nread != (ssize_t)sizeof(buffer) ||
      trng_all_value(buffer, sizeof(buffer), 0x00) ||
      trng_all_value(buffer, sizeof(buffer), 0xff))
    {
      printf("TRNG_TEST api FAIL getrandom ret=%zd errno=%d\n",
             nread, errno);
      return -1;
    }
#endif

#ifdef CONFIG_DEV_URANDOM_ARCH
  nread = getrandom(buffer, sizeof(buffer), 0);
  if (nread != (ssize_t)sizeof(buffer))
    {
      printf("TRNG_TEST api FAIL urandom ret=%zd errno=%d\n",
             nread, errno);
      return -1;
    }
#endif

  printf("TRNG_TEST RESULT case=api PASS poll=%04lx checksum=%08" PRIx32
         "\n",
         (unsigned long)pfd.revents, trng_checksum(buffer, sizeof(buffer)));
  return 0;
}

static int trng_test_stats(void)
{
  FAR uint8_t *buffer;
  struct timespec start;
  struct timespec end;
  uint64_t elapsed_us;
  size_t repeated = 0;
  size_t fixed = 0;
  size_t ones = 0;
  size_t i;
  size_t j;
  int fd;

  buffer = malloc(TRNG_STATS_SIZE);
  if (buffer == NULL)
    {
      printf("TRNG_TEST stats FAIL malloc size=%d\n", TRNG_STATS_SIZE);
      return -1;
    }

  fd = open(TRNG_DEVICE, O_RDONLY);
  if (fd < 0)
    {
      printf("TRNG_TEST stats FAIL open errno=%d\n", errno);
      free(buffer);
      return -1;
    }

  clock_gettime(CLOCK_MONOTONIC, &start);
  if (trng_read_exact(fd, buffer, TRNG_STATS_SIZE) < 0)
    {
      close(fd);
      free(buffer);
      return -1;
    }

  clock_gettime(CLOCK_MONOTONIC, &end);
  close(fd);

  for (i = 0; i < TRNG_STATS_SIZE; i++)
    {
      ones += (size_t)__builtin_popcount((unsigned int)buffer[i]);
    }

  for (i = 0; i < TRNG_STATS_SIZE; i += TRNG_BLOCK_SIZE)
    {
      if (trng_all_value(&buffer[i], TRNG_BLOCK_SIZE, 0x00) ||
          trng_all_value(&buffer[i], TRNG_BLOCK_SIZE, 0xff))
        {
          fixed++;
        }

      for (j = 0; j < i; j += TRNG_BLOCK_SIZE)
        {
          if (memcmp(&buffer[j], &buffer[i], TRNG_BLOCK_SIZE) == 0)
            {
              repeated++;
            }
        }
    }

  elapsed_us = (uint64_t)(end.tv_sec - start.tv_sec) * UINT64_C(1000000);
  if (end.tv_nsec >= start.tv_nsec)
    {
      elapsed_us += (uint64_t)(end.tv_nsec - start.tv_nsec) / UINT64_C(1000);
    }
  else
    {
      elapsed_us -= UINT64_C(1000000);
      elapsed_us += (uint64_t)(UINT64_C(1000000000) + end.tv_nsec -
                               start.tv_nsec) /
                    UINT64_C(1000);
    }

  if (fixed != 0 || repeated != 0 ||
      ones < TRNG_STATS_SIZE * 8 * 35 / 100 ||
      ones > TRNG_STATS_SIZE * 8 * 65 / 100)
    {
      printf("TRNG_TEST stats FAIL bytes=%zu ones=%zu fixed=%zu "
             "repeated=%zu elapsed_us=%" PRIu64 "\n",
             (size_t)TRNG_STATS_SIZE, ones, fixed, repeated, elapsed_us);
      free(buffer);
      return -1;
    }

  printf("TRNG_TEST RESULT case=stats PASS bytes=%zu ones=%zu "
         "ones_permille=%zu fixed=%zu repeated=%zu elapsed_us=%" PRIu64
         " checksum=%08" PRIx32 "\n",
         (size_t)TRNG_STATS_SIZE, ones,
         ones * 1000 / (TRNG_STATS_SIZE * 8), fixed, repeated, elapsed_us,
         trng_checksum(buffer, TRNG_STATS_SIZE));
  free(buffer);
  return 0;
}

static FAR void *trng_worker(FAR void *arg)
{
  static const size_t lengths[] =
  {
    1, 31, 32, 33, 127, 255, 257
  };

  FAR struct trng_worker_s *worker = arg;
  uint8_t buffer[TRNG_MAX_READ_SIZE];
  uint32_t hash = UINT32_C(2166136261);
  int fd;
  int i;

  fd = open(TRNG_DEVICE, O_RDONLY);
  if (fd < 0)
    {
      worker->result = -errno;
      return NULL;
    }

  for (i = 0; i < CONFIG_BL_MCU_PERIPHERAL_TESTS_TRNG_ITERATIONS; i++)
    {
      size_t len = lengths[(worker->id + i) %
                           (sizeof(lengths) / sizeof(lengths[0]))];

      if (trng_read_exact(fd, buffer, len) < 0 ||
          (len >= TRNG_BLOCK_SIZE &&
           (trng_all_value(buffer, len, 0x00) ||
            trng_all_value(buffer, len, 0xff))))
        {
          worker->result = -EIO;
          close(fd);
          return NULL;
        }

      hash ^= trng_checksum(buffer, len);
      hash *= UINT32_C(16777619);
      worker->bytes += len;
    }

  close(fd);
  worker->checksum = hash;
  worker->result = 0;
  return NULL;
}

static int trng_test_concurrent(void)
{
  struct trng_worker_s workers[CONFIG_BL_MCU_PERIPHERAL_TESTS_TRNG_THREADS];
  pthread_t threads[CONFIG_BL_MCU_PERIPHERAL_TESTS_TRNG_THREADS];
  size_t total = 0;
  int created = 0;
  int ret = 0;
  int i;

  memset(workers, 0, sizeof(workers));
  for (i = 0; i < CONFIG_BL_MCU_PERIPHERAL_TESTS_TRNG_THREADS; i++)
    {
      workers[i].id = i;
      ret = pthread_create(&threads[i], NULL, trng_worker, &workers[i]);
      if (ret != 0)
        {
          printf("TRNG_TEST concurrent FAIL create=%d error=%d\n", i, ret);
          break;
        }

      created++;
    }

  for (i = 0; i < created; i++)
    {
      int joinret = pthread_join(threads[i], NULL);

      if (joinret != 0 || workers[i].result != 0)
        {
          printf("TRNG_TEST concurrent FAIL worker=%d join=%d result=%d\n",
                 i, joinret, workers[i].result);
          ret = -1;
        }

      total += workers[i].bytes;
      printf("TRNG_TEST worker=%d bytes=%zu checksum=%08" PRIx32 "\n",
             i, workers[i].bytes, workers[i].checksum);
    }

  if (created != CONFIG_BL_MCU_PERIPHERAL_TESTS_TRNG_THREADS || ret != 0)
    {
      return -1;
    }

  printf("TRNG_TEST RESULT case=concurrent PASS threads=%d iterations=%d "
         "bytes=%zu\n",
         CONFIG_BL_MCU_PERIPHERAL_TESTS_TRNG_THREADS,
         CONFIG_BL_MCU_PERIPHERAL_TESTS_TRNG_ITERATIONS, total);
  return 0;
}

static int trng_run_case(FAR const char *name)
{
  if (strcmp(name, "lengths") == 0)
    {
      return trng_test_lengths();
    }

  if (strcmp(name, "api") == 0)
    {
      return trng_test_api();
    }

  if (strcmp(name, "stats") == 0)
    {
      return trng_test_stats();
    }

  if (strcmp(name, "concurrent") == 0)
    {
      return trng_test_concurrent();
    }

  return -EINVAL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  static const char *cases[] =
  {
    "lengths", "api", "stats", "concurrent"
  };

  int ret = 0;
  size_t i;

  if (argc != 2)
    {
      trng_usage(argv[0]);
      return EXIT_FAILURE;
    }

  printf("TRNG_TEST BEGIN case=%s\n", argv[1]);

  if (strcmp(argv[1], "all") != 0)
    {
      ret = trng_run_case(argv[1]);
      if (ret == -EINVAL)
        {
          trng_usage(argv[0]);
        }

      return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
      if (trng_run_case(cases[i]) < 0)
        {
          ret = -1;
        }
    }

  printf("TRNG_TEST RESULT case=all %s\n", ret == 0 ? "PASS" : "FAIL");
  return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
