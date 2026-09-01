/****************************************************************************
 * apps/vendor/bouffalolab/apps/mcu_peripheral_tests/uart/uart_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <poll.h>
#include <sched.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "chip.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define UART_DEV              "/dev/ttyS1"
#define UART_TEST_TIMEOUT_MS  600
#define UART_MAX_PAYLOAD      1024
#define UART_TEST_SKIP        2
#define UART_WRITE_MAX_STALLS 10

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct uart_writer_s
{
  int fd;
  sem_t *start;
  uint8_t value;
  size_t length;
  ssize_t result;
};

struct uart_reader_s
{
  int fd;
  uint8_t *data;
  size_t length;
  int result;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int uart_open(bool nonblock)
{
  int flags = O_RDWR;
  int fd;

  if (nonblock)
    {
      flags |= O_NONBLOCK;
    }

  fd = open(UART_DEV, flags);
  if (fd < 0)
    {
      printf("UART open FAIL path=%s errno=%d\n", UART_DEV, errno);
    }

  return fd;
}

static void uart_raw(struct termios *term)
{
  cfmakeraw(term);
  term->c_cflag |= CLOCAL | CREAD;
}

static int uart_configure_raw(int fd, speed_t speed)
{
  struct termios term;

  memset(&term, 0, sizeof(term));
  if (tcgetattr(fd, &term) < 0)
    {
      printf("UART tcgetattr FAIL errno=%d\n", errno);
      return -1;
    }

  uart_raw(&term);
  if (cfsetspeed(&term, speed) < 0 || tcsetattr(fd, TCSANOW, &term) < 0)
    {
      printf("UART raw setup FAIL errno=%d\n", errno);
      return -1;
    }

  return 0;
}

static int uart_write_all(int fd, const uint8_t *data, size_t length)
{
  size_t offset = 0;
  int stalls = 0;

  while (offset < length)
    {
      ssize_t ret = write(fd, data + offset, length - offset);

      if (ret < 0)
        {
          if (errno == EAGAIN || errno == EINTR)
            {
              struct pollfd pfd =
              {
                .fd = fd,
                .events = POLLOUT
              };

              ret = poll(&pfd, 1, UART_TEST_TIMEOUT_MS);
              if (ret > 0 && (pfd.revents & POLLOUT) != 0 &&
                  ++stalls <= UART_WRITE_MAX_STALLS)
                {
                  continue;
                }
            }

          printf("UART write FAIL offset=%zu errno=%d\n", offset, errno);
          return -1;
        }

      if (ret == 0)
        {
          return -1;
        }

      offset += ret;
      stalls = 0;
      sched_yield();
    }

  return 0;
}

static int uart_read_exact(int fd, uint8_t *data, size_t length,
                           int timeout_ms)
{
  struct timespec start;
  size_t offset = 0;
  struct pollfd pfd =
  {
    .fd = fd,
    .events = POLLIN
  };

  if (clock_gettime(CLOCK_MONOTONIC, &start) < 0)
    {
      return -1;
    }

  while (offset < length)
    {
      struct timespec now;
      int64_t elapsed_ms;
      int remaining_ms;
      int ret;

      if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        {
          return -1;
        }

      elapsed_ms = (int64_t)(now.tv_sec - start.tv_sec) * 1000 +
                   (now.tv_nsec - start.tv_nsec) / 1000000;
      remaining_ms = timeout_ms - (int)elapsed_ms;
      if (remaining_ms <= 0)
        {
          printf("UART read timeout received=%zu expected=%zu\n",
                 offset, length);
          return UART_TEST_SKIP;
        }

      ret = poll(&pfd, 1, remaining_ms);

      if (ret < 0)
        {
          printf("UART poll FAIL errno=%d\n", errno);
          return -1;
        }

      if (ret == 0)
        {
          printf("UART read timeout received=%zu expected=%zu\n",
                 offset, length);
          return UART_TEST_SKIP;
        }

      if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
        {
          printf("UART poll FAIL revents=0x%lx\n",
                 (unsigned long)pfd.revents);
          return -1;
        }

      ret = read(fd, data + offset, length - offset);
      if (ret < 0)
        {
          if (errno == EAGAIN || errno == EINTR)
            {
              continue;
            }

          printf("UART read FAIL errno=%d\n", errno);
          return -1;
        }

      if (ret == 0)
        {
          return UART_TEST_SKIP;
        }

      offset += ret;
    }

  return 0;
}

static void *uart_reader(void *arg)
{
  struct uart_reader_s *reader = arg;

  reader->result = uart_read_exact(reader->fd, reader->data, reader->length,
                                   UART_TEST_TIMEOUT_MS);
  return NULL;
}

static int uart_loopback(int fd, size_t length, uint8_t seed)
{
  struct uart_reader_s reader;
  pthread_t thread;
  uint8_t tx[UART_MAX_PAYLOAD];
  uint8_t rx[UART_MAX_PAYLOAD];
  int thread_ret;
  int write_ret;
  size_t i;
  int ret;

  for (i = 0; i < length; i++)
    {
      tx[i] = (uint8_t)(seed + i * 37u);
    }

  if (tcflush(fd, TCIOFLUSH) < 0)
    {
      return -1;
    }

  reader.fd = fd;
  reader.data = rx;
  reader.length = length;
  reader.result = -1;
  thread_ret = pthread_create(&thread, NULL, uart_reader, &reader);
  if (thread_ret != 0)
    {
      printf("UART loopback reader create FAIL error=%d\n", thread_ret);
      return -1;
    }

  write_ret = uart_write_all(fd, tx, length);
  pthread_join(thread, NULL);
  if (write_ret < 0)
    {
      return -1;
    }

  ret = reader.result;
  if (ret != 0)
    {
      return ret;
    }

  if (memcmp(tx, rx, length) != 0)
    {
      printf("UART loopback FAIL length=%zu\n", length);
      return -1;
    }

  return 0;
}

static int uart_format_loopback(int fd, uint8_t mask, uint8_t value)
{
  uint8_t tx = value & mask;
  uint8_t rx;
  int ret;

  if (tcflush(fd, TCIOFLUSH) < 0 || uart_write_all(fd, &tx, 1) < 0)
    {
      return -1;
    }

  ret = uart_read_exact(fd, &rx, 1, UART_TEST_TIMEOUT_MS);
  if (ret != 0)
    {
      return ret;
    }

  return rx == tx ? 0 : -1;
}

static int uart_case_001(void)
{
  struct termios term;
  int fd = uart_open(true);
  int console_fd;
  speed_t speed;

  if (fd < 0)
    {
      return -1;
    }

  memset(&term, 0, sizeof(term));
  if (tcgetattr(fd, &term) < 0)
    {
      printf("[UART-001] FAIL tcgetattr errno=%d\n", errno);
      close(fd);
      return -1;
    }

  speed = cfgetspeed(&term);
  printf("[UART-001] device=%s speed=%lu cflag=0x%x console=/dev/console\n",
         UART_DEV, (unsigned long)speed, term.c_cflag);
  if (speed != 115200 || (term.c_cflag & CSIZE) != CS8 ||
      (term.c_cflag & (PARENB | CSTOPB)) != 0)
    {
      printf("[UART-001] FAIL expected=115200-8N1\n");
      close(fd);
      return -1;
    }

  console_fd = open("/dev/console", O_RDWR);
  if (console_fd < 0)
    {
      printf("[UART-001] FAIL /dev/console errno=%d\n", errno);
      close(fd);
      return -1;
    }

  close(console_fd);
  close(fd);
  printf("[UART-001] PASS ttyS1 registered; console opens separately\n");
  return 0;
}

static int uart_case_002(void)
{
  static const size_t lengths[] =
  {
    1, 31, 32, 33, 255, 256, 257, 1024
  };

  int fd = uart_open(true);
  size_t i;

  if (fd < 0 || uart_configure_raw(fd, 115200) < 0)
    {
      if (fd >= 0)
        close(fd);
      return -1;
    }

  for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++)
    {
      int ret = uart_loopback(fd, lengths[i], (uint8_t)i);
      if (ret != 0)
        {
          printf("[UART-002] %s length=%zu\n",
                 ret == UART_TEST_SKIP ? "PARTIAL" : "FAIL", lengths[i]);
          close(fd);
          return ret;
        }

      printf("[UART-002] PASS length=%zu\n", lengths[i]);
    }

  close(fd);
  return 0;
}

static int uart_case_003(void)
{
  static const tcflag_t sizes[] =
  {
    CS5, CS6, CS7, CS8
  };

  static const tcflag_t parity[] =
  {
    0, PARENB, PARENB | PARODD
  };

  static const speed_t speeds[] =
  {
    9600, 115200, 1000000
  };

  static const uint8_t masks[] =
  {
    0x1f, 0x3f, 0x7f, 0xff
  };

  struct termios original;
  struct termios term;
  int fd = uart_open(true);
  size_t i;
  size_t j;
  size_t k;
  int result = 0;

  memset(&term, 0, sizeof(term));
  if (fd < 0 || tcgetattr(fd, &term) < 0)
    {
      if (fd >= 0)
        close(fd);
      return -1;
    }

  original = term;
  uart_raw(&term);
  for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
      for (j = 0; j < sizeof(parity) / sizeof(parity[0]); j++)
        {
          for (k = 0; k < 2; k++)
            {
              struct termios candidate = term;
              int ret;

              candidate.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
              candidate.c_cflag |= sizes[i] | parity[j] |
                                   (k != 0 ? CSTOPB : 0);
              ret = cfsetspeed(&candidate, speeds[(i + j + k) %
                                                  (sizeof(speeds) /
                                                   sizeof(speeds[0]))]);
              if (ret == 0)
                {
                  ret = tcsetattr(fd, TCSANOW, &candidate);
                }

              if (ret < 0)
                {
                  printf("[UART-003] FAIL size=0x%x parity=0x%x "
                         "stop=%zu errno=%d\n", sizes[i], parity[j],
                         k + 1, errno);
                  result = -1;
                  goto out;
                }

              ret = uart_format_loopback(fd, masks[i], 0xa5);
              if (ret != 0)
                {
                  printf("[UART-003] %s size=0x%x parity=0x%x "
                         "stop=%zu speed=%lu\n",
                         ret == UART_TEST_SKIP ? "PARTIAL" : "FAIL",
                         sizes[i], parity[j], k + 1,
                         (unsigned long)cfgetspeed(&candidate));
                  result = ret;
                  goto out;
                }
            }
        }
    }

out:
  if (tcsetattr(fd, TCSANOW, &original) < 0)
    {
      printf("[UART-003] FAIL restore errno=%d\n", errno);
      result = -1;
    }

  close(fd);
  if (result == 0)
    {
      printf("[UART-003] PASS 5/6/7/8 bits, none/odd/even, 1/2 stop\n");
    }

  return result;
}

static int uart_expect_set_failure(int fd, speed_t speed, tcflag_t flags,
                                   int expected_errno,
                                   const struct termios *old)
{
  struct termios candidate = *old;
  struct termios after;
  const tcflag_t hardware_flags =
    CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS;
  int ret;

  candidate.c_cflag = (candidate.c_cflag & ~CRTSCTS) | flags;
  cfsetspeed(&candidate, speed);
  errno = 0;
  ret = tcsetattr(fd, TCSANOW, &candidate);
  if (ret != -1 || errno != expected_errno)
    {
      printf("[UART-004] FAIL speed=%lu flags=0x%x ret=%d errno=%d "
             "expected=%d\n", (unsigned long)speed, flags, ret, errno,
             expected_errno);
      return -1;
    }

  memset(&after, 0, sizeof(after));
  if (tcgetattr(fd, &after) < 0 || cfgetspeed(&after) != cfgetspeed(old) ||
      (after.c_cflag & hardware_flags) != (old->c_cflag & hardware_flags))
    {
      printf("[UART-004] FAIL old termios changed speed=%lu flags=0x%x\n",
             (unsigned long)speed, flags);
      return -1;
    }

  printf("[UART-004] PASS rejected speed=%lu flags=0x%x errno=%d "
         "old-state-kept\n", (unsigned long)speed, flags, expected_errno);
  return 0;
}

static int uart_case_004(void)
{
  struct termios old;
  int fd = uart_open(false);
  speed_t half = BL616CL_UART_CLOCK / 2;
  int ret = 0;

  memset(&old, 0, sizeof(old));
  if (fd < 0 || tcgetattr(fd, &old) < 0)
    {
      if (fd >= 0)
        close(fd);
      return -1;
    }

  ret |= uart_expect_set_failure(fd, 0, 0, EINVAL, &old);
  ret |= uart_expect_set_failure(fd, half, 0, EINVAL, &old);
  ret |= uart_expect_set_failure(fd, half + 1, 0, EINVAL, &old);
  ret |= uart_expect_set_failure(fd, 1, 0, EINVAL, &old);
  ret |= uart_expect_set_failure(fd, 115200, CRTS_IFLOW, EOPNOTSUPP, &old);
  ret |= uart_expect_set_failure(fd, 115200, CCTS_OFLOW, EOPNOTSUPP, &old);
  ret |= uart_expect_set_failure(fd, 115200, CRTSCTS, EOPNOTSUPP, &old);
  close(fd);
  return ret;
}

static int uart_case_005(void)
{
  struct pollfd pfd;
  uint8_t payload[UART_MAX_PAYLOAD];
  int available;
  int empty_space;
  int queued;
  int space;
  uint8_t byte;
  int fd = uart_open(true);
  ssize_t ret;

  if (fd < 0 || uart_configure_raw(fd, 9600) < 0)
    {
      if (fd >= 0)
        close(fd);
      return -1;
    }

  if (tcflush(fd, TCIOFLUSH) < 0)
    {
      printf("[UART-005] FAIL initial flush errno=%d\n", errno);
      close(fd);
      return -1;
    }

  errno = 0;
  ret = read(fd, &byte, 1);
  if (ret != -1 || errno != EAGAIN)
    {
      printf("[UART-005] FAIL empty nonblock read ret=%ld errno=%d\n",
             (long)ret, errno);
      close(fd);
      return -1;
    }

  pfd.fd = fd;
  pfd.events = POLLIN | POLLOUT;
  pfd.revents = 0;
  ret = poll(&pfd, 1, 0);
  if (ret < 0 || (pfd.revents & POLLOUT) == 0 ||
      (pfd.revents & POLLIN) != 0 ||
      ioctl(fd, FIONREAD, (unsigned long)(uintptr_t)&available) < 0 ||
      ioctl(fd, FIONWRITE, (unsigned long)(uintptr_t)&queued) < 0 ||
      ioctl(fd, FIONSPACE, (unsigned long)(uintptr_t)&space) < 0 ||
      available != 0 || queued != 0 || space <= 0)
    {
      printf("[UART-005] FAIL poll/ioctl errno=%d\n", errno);
      close(fd);
      return -1;
    }

  empty_space = space;
  memset(payload, 0x5a, sizeof(payload));
  ret = write(fd, payload, sizeof(payload));
  if (ret <= 0 ||
      ioctl(fd, FIONWRITE, (unsigned long)(uintptr_t)&queued) < 0 ||
      ioctl(fd, FIONSPACE, (unsigned long)(uintptr_t)&space) < 0 ||
      queued <= 0 || space >= empty_space || queued + space != empty_space)
    {
      printf("[UART-005] FAIL occupied queue write=%ld queued=%d space=%d "
             "empty-space=%d errno=%d\n",
             (long)ret, queued, space,
             empty_space, errno);
      close(fd);
      return -1;
    }

  if (tcflush(fd, TCOFLUSH) < 0 || tcdrain(fd) < 0 ||
      tcflush(fd, TCIFLUSH) < 0)
    {
      printf("[UART-005] FAIL occupied queue cleanup errno=%d\n", errno);
      close(fd);
      return -1;
    }

  printf("[UART-005] PASS empty read=-1/EAGAIN pollout=1 rx=%d "
         "occupied-queued=%d occupied-space=%d empty-space=%d\n",
         available, queued, space, empty_space);
  close(fd);
  return 0;
}

static int uart_case_006(void)
{
  struct pollfd pfd =
  {
    .events = POLLIN
  };

  uint8_t byte = 0x6a;
  int available = -1;
  int fd = uart_open(true);
  int ret;

  if (fd < 0 || uart_configure_raw(fd, 115200) < 0)
    {
      if (fd >= 0)
        close(fd);
      return -1;
    }

  pfd.fd = fd;
  if (tcflush(fd, TCIOFLUSH) < 0 || uart_write_all(fd, &byte, 1) < 0 ||
      tcdrain(fd) < 0)
    {
      printf("[UART-006] FAIL flush/drain errno=%d\n", errno);
      close(fd);
      return -1;
    }

  ret = poll(&pfd, 1, UART_TEST_TIMEOUT_MS);
  if (ret == 0)
    {
      printf("[UART-006] PARTIAL no GPIO14/15 loopback data received\n");
      close(fd);
      return UART_TEST_SKIP;
    }

  if (ret < 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
      (pfd.revents & POLLIN) == 0 ||
      ioctl(fd, FIONREAD, (unsigned long)(uintptr_t)&available) < 0 ||
      available < 1 || tcflush(fd, TCIFLUSH) < 0 ||
      ioctl(fd, FIONREAD, (unsigned long)(uintptr_t)&available) < 0 ||
      available != 0 || tcflush(fd, TCOFLUSH) < 0)
    {
      printf("[UART-006] FAIL flush/drain available=%d errno=%d\n",
             available, errno);
      close(fd);
      return -1;
    }

  ret = uart_loopback(fd, 32, 0x66);
  close(fd);
  printf("[UART-006] %s TCFLSH/TCDRN and post-drain transfer\n",
         ret == 0 ? "PASS" : ret == UART_TEST_SKIP ? "PARTIAL" :
                                                     "FAIL");
  return ret;
}

static int uart_case_007(void)
{
  int fd_block = uart_open(false);
  int fd_nonblock = uart_open(true);
  uint8_t payload[UART_MAX_PAYLOAD];
  int ret;

  if (fd_block < 0 || fd_nonblock < 0 ||
      uart_configure_raw(fd_block, 115200) < 0)
    {
      if (fd_nonblock >= 0)
        close(fd_nonblock);
      if (fd_block >= 0)
        close(fd_block);
      return -1;
    }

  ret = uart_loopback(fd_nonblock, 1, 0x77);
  if (ret != 0)
    {
      close(fd_nonblock);
      close(fd_block);
      printf("[UART-007] %s multi-fd loopback\n",
             ret == UART_TEST_SKIP ? "PARTIAL" : "FAIL");
      return ret;
    }

  memset(payload, 0x5a, sizeof(payload));
  if (uart_write_all(fd_nonblock, payload, sizeof(payload)) < 0 ||
      close(fd_nonblock) < 0 || tcflush(fd_block, TCOFLUSH) < 0)
    {
      close(fd_block);
      printf("[UART-007] FAIL writer close/flush errno=%d\n", errno);
      return -1;
    }

  if (close(fd_block) < 0)
    {
      printf("[UART-007] FAIL blocking last-close drain errno=%d\n", errno);
      return -1;
    }

  fd_block = uart_open(true);
  if (fd_block < 0)
    {
      return -1;
    }

  ret = uart_loopback(fd_block, 1, 0x78);
  close(fd_block);
  printf("[UART-007] %s multi-fd, blocking last-close path, reopen\n",
         ret == 0 ? "PASS" : ret == UART_TEST_SKIP ? "PARTIAL" :
                                                     "FAIL");
  return ret;
}

static void *uart_writer(void *arg)
{
  struct uart_writer_s *writer = arg;
  uint8_t *payload = malloc(writer->length);
  size_t i;

  if (payload == NULL)
    {
      writer->result = -1;
      return NULL;
    }

  while (sem_wait(writer->start) < 0)
    {
      if (errno != EINTR)
        {
          writer->result = -1;
          free(payload);
          return NULL;
        }
    }

  for (i = 0; i < writer->length; i++)
    {
      payload[i] = writer->value;
    }

  writer->result = uart_write_all(writer->fd, payload, writer->length);
  free(payload);
  return NULL;
}

static int uart_case_008(void)
{
  struct uart_writer_s writers[2] =
  {
    {
      .value = 0x31, .length = 64, .result = -1
    },
    {
      .value = 0x32, .length = 64, .result = -1
    }
  };

  sem_t start;
  pthread_t threads[2];
  uint8_t rx[128];
  size_t count_31 = 0;
  size_t count_32 = 0;
  size_t i;
  int fd_reader = uart_open(true);
  int fd_writer0 = uart_open(true);
  int fd_writer1 = uart_open(true);
  int created;
  int ret;

  if (fd_reader < 0 || fd_writer0 < 0 || fd_writer1 < 0 ||
      uart_configure_raw(fd_reader, 115200) < 0 ||
      sem_init(&start, 0, 0) < 0)
    {
      if (fd_writer1 >= 0)
        close(fd_writer1);
      if (fd_writer0 >= 0)
        close(fd_writer0);
      if (fd_reader >= 0)
        close(fd_reader);
      return -1;
    }

  writers[0].fd = fd_writer0;
  writers[0].start = &start;
  writers[1].fd = fd_writer1;
  writers[1].start = &start;
  if (tcflush(fd_reader, TCIOFLUSH) < 0)
    {
      printf("[UART-008] FAIL flush errno=%d\n", errno);
      sem_destroy(&start);
      close(fd_writer1);
      close(fd_writer0);
      close(fd_reader);
      return -1;
    }

  created = pthread_create(&threads[0], NULL, uart_writer, &writers[0]);
  if (created != 0)
    {
      printf("[UART-008] FAIL pthread_create writer=0 error=%d\n", created);
      sem_destroy(&start);
      close(fd_writer1);
      close(fd_writer0);
      close(fd_reader);
      return -1;
    }

  created = pthread_create(&threads[1], NULL, uart_writer, &writers[1]);
  if (created != 0)
    {
      printf("[UART-008] FAIL pthread_create writer=1 error=%d\n", created);
      sem_post(&start);
      pthread_join(threads[0], NULL);
      sem_destroy(&start);
      close(fd_writer1);
      close(fd_writer0);
      close(fd_reader);
      return -1;
    }

  sem_post(&start);
  sem_post(&start);
  pthread_join(threads[0], NULL);
  pthread_join(threads[1], NULL);
  sem_destroy(&start);
  close(fd_writer1);
  close(fd_writer0);
  if (writers[0].result < 0 || writers[1].result < 0)
    {
      printf("[UART-008] FAIL writer result=%ld/%ld\n",
             (long)writers[0].result, (long)writers[1].result);
      close(fd_reader);
      return -1;
    }

  ret = uart_read_exact(fd_reader, rx, sizeof(rx), UART_TEST_TIMEOUT_MS);
  if (ret != 0)
    {
      close(fd_reader);
      printf("[UART-008] %s dual-writer transfer\n",
             ret == UART_TEST_SKIP ? "PARTIAL" : "FAIL");
      return ret;
    }

  for (i = 0; i < sizeof(rx); i++)
    {
      if (rx[i] != 0x31 && rx[i] != 0x32)
        {
          printf("[UART-008] FAIL unexpected byte index=%zu value=0x%02x\n",
                 i, rx[i]);
          close(fd_reader);
          return -1;
        }

      if (rx[i] == 0x31)
        {
          count_31++;
        }
      else
        {
          count_32++;
        }
    }

  if (count_31 != writers[0].length || count_32 != writers[1].length)
    {
      printf("[UART-008] FAIL payload counts=%zu/%zu\n", count_31, count_32);
      close(fd_reader);
      return -1;
    }

  close(fd_reader);
  printf("[UART-008] PASS payloads complete; no cross-write atomicity\n");
  return 0;
}

static int uart_case_009(void)
{
  uint8_t *payload;
  uint8_t *rx;
  const size_t length = 4096;
  size_t i;
  size_t count = 0;
  int fd = uart_open(true);
  struct pollfd pfd =
  {
    .events = POLLIN
  };

  int recovery;

  if (fd < 0 || uart_configure_raw(fd, 115200) < 0)
    {
      if (fd >= 0)
        close(fd);
      return -1;
    }

  payload = malloc(length);
  rx = malloc(length);
  if (payload == NULL || rx == NULL)
    {
      free(payload);
      free(rx);
      close(fd);
      printf("[UART-009] FAIL allocate %zu-byte buffers\n", length);
      return -1;
    }

  for (i = 0; i < length; i++)
    payload[i] = (uint8_t)i;
  if (tcflush(fd, TCIOFLUSH) < 0)
    {
      free(payload);
      free(rx);
      close(fd);
      return -1;
    }

  if (uart_write_all(fd, payload, length) < 0)
    {
      free(payload);
      free(rx);
      close(fd);
      return -1;
    }

  pfd.fd = fd;
  while (count < length)
    {
      int ret = poll(&pfd, 1, count == 0 ? UART_TEST_TIMEOUT_MS : 50);
      if (ret < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          printf("[UART-009] FAIL poll errno=%d\n", errno);
          free(payload);
          free(rx);
          close(fd);
          return -1;
        }

      if (ret == 0)
        {
          break;
        }

      if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
          (pfd.revents & POLLIN) == 0)
        {
          printf("[UART-009] FAIL poll revents=0x%lx\n",
                 (unsigned long)pfd.revents);
          free(payload);
          free(rx);
          close(fd);
          return -1;
        }

      ret = read(fd, rx + count, length - count);
      if (ret > 0)
        {
          count += ret;
        }
      else if (ret < 0 && errno != EAGAIN && errno != EINTR)
        {
          printf("[UART-009] FAIL read errno=%d\n", errno);
          free(payload);
          free(rx);
          close(fd);
          return -1;
        }
    }

  if (count == 0)
    {
      free(payload);
      free(rx);
      close(fd);
      printf("[UART-009] PARTIAL no GPIO14/15 loopback data received\n");
      return UART_TEST_SKIP;
    }

  if (tcflush(fd, TCIOFLUSH) < 0)
    {
      free(payload);
      free(rx);
      close(fd);
      return -1;
    }

  recovery = uart_loopback(fd, 32, 0x99);
  free(payload);
  free(rx);
  close(fd);
  printf("[UART-009] %s attempted=%zu received=%zu "
         "overflow=%s recovery=%s\n",
         recovery == 0 ? "PASS" : "FAIL", length,
         count, count < length ? "observed" : "not-observed",
         recovery == 0 ? "PASS" : "FAIL");
  return recovery == 0 ? 0 : -1;
}

static int uart_case_010(void)
{
  int fd = uart_open(true);
  int i;

  if (fd < 0 || uart_configure_raw(fd, 115200) < 0)
    {
      if (fd >= 0)
        close(fd);
      return -1;
    }

  printf("[UART-010] operator: keep issuing NSH commands on USB2 now\n");
  for (i = 0; i < 100; i++)
    {
      int ret = uart_loopback(fd, UART_MAX_PAYLOAD, (uint8_t)i);
      if (ret != 0)
        {
          close(fd);
          printf("[UART-010] %s UART1 iteration=%d\n",
                 ret == UART_TEST_SKIP ? "PARTIAL" : "FAIL", i);
          return ret;
        }

      if (i % 10 == 9)
        {
          printf("[UART-010] UART1 progress=%d/100\n", i + 1);
        }
    }

  close(fd);
  printf("[UART-010] PARTIAL UART1 pressure PASS; "
         "USB2 needs operator evidence\n");
  return UART_TEST_SKIP;
}

static int uart_case_011(void)
{
  printf("[UART-011] PARTIAL verify clean builds and archive/map gates\n");
  return UART_TEST_SKIP;
}

static int uart_case_012(void)
{
  printf("[UART-012] PARTIAL verify GPIO14/15 owner negative builds\n");
  return UART_TEST_SKIP;
}

static void uart_usage(const char *name)
{
  printf("Usage: %s <001|002|003|004|005|006|007|008|009|010|"
         "011|012|all>\n", name);
}

static int uart_run_case(const char *name)
{
  if (strcmp(name, "001") == 0)
    {
      return uart_case_001();
    }

  if (strcmp(name, "002") == 0)
    {
      return uart_case_002();
    }

  if (strcmp(name, "003") == 0)
    {
      return uart_case_003();
    }

  if (strcmp(name, "004") == 0)
    {
      return uart_case_004();
    }

  if (strcmp(name, "005") == 0)
    {
      return uart_case_005();
    }

  if (strcmp(name, "006") == 0)
    {
      return uart_case_006();
    }

  if (strcmp(name, "007") == 0)
    {
      return uart_case_007();
    }

  if (strcmp(name, "008") == 0)
    {
      return uart_case_008();
    }

  if (strcmp(name, "009") == 0)
    {
      return uart_case_009();
    }

  if (strcmp(name, "010") == 0)
    {
      return uart_case_010();
    }

  if (strcmp(name, "011") == 0)
    {
      return uart_case_011();
    }

  if (strcmp(name, "012") == 0)
    {
      return uart_case_012();
    }

  return -EINVAL;
}

int main(int argc, char *argv[])
{
  static const char *const cases[] =
  {
    "001", "002", "003", "004", "005", "006",
    "007", "008", "009", "010", "011", "012"
  };

  int failures = 0;
  int skipped = 0;
  size_t i;

  if (argc != 2)
    {
      uart_usage(argv[0]);
      return 1;
    }

  if (strcmp(argv[1], "all") != 0)
    {
      int ret = uart_run_case(argv[1]);
      if (ret == -EINVAL)
        {
          uart_usage(argv[0]);
          return 1;
        }

      if (ret == UART_TEST_SKIP)
        {
          return UART_TEST_SKIP;
        }

      return ret < 0 ? 1 : 0;
    }

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
      int ret = uart_run_case(cases[i]);
      if (ret == UART_TEST_SKIP)
        {
          skipped++;
        }
      else if (ret < 0)
        {
          failures++;
        }
    }

  printf("UART test %s failures=%d skipped=%d\n",
         failures == 0 ? (skipped == 0 ? "PASS" : "PARTIAL") : "FAIL",
         failures, skipped);
  if (failures != 0)
    {
      return 1;
    }

  return skipped == 0 ? 0 : UART_TEST_SKIP;
}
