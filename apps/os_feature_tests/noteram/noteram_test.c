/****************************************************************************
 * apps/vendor/bouffalolab/apps/os_feature_tests/noteram/noteram_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/note/note_driver.h>

#define NOTERAM_PATH "/dev/note/ram"

static void print_usage(const char *progname)
{
  printf("Usage: %s <status|clear|mode on|mode off|burst [count]>\n",
         progname);
}

static int noteram_open(void)
{
  int fd = open(NOTERAM_PATH, O_RDONLY);

  if (fd < 0)
    {
      fprintf(stderr, "NOTERAM_TEST open failed: %d\n", errno);
    }

  return fd;
}

static int print_status(void)
{
  unsigned int mode;
  unsigned int unread;
  int fd;

  fd = noteram_open();
  if (fd < 0)
    {
      return -errno;
    }

  if (ioctl(fd, NOTE_GETMODE, (unsigned long)(uintptr_t)&mode) < 0 ||
      ioctl(fd, FIONREAD, (unsigned long)(uintptr_t)&unread) < 0)
    {
      int ret = -errno;
      close(fd);
      fprintf(stderr, "NOTERAM_TEST status failed: %d\n", errno);
      return ret;
    }

  close(fd);
  printf("NOTERAM_TEST STATUS mode=%u unread=%u\n", mode, unread);
  return 0;
}

static int clear_buffer(void)
{
  int fd = noteram_open();
  if (fd < 0)
    {
      return -errno;
    }

  if (ioctl(fd, NOTE_CLEAR, 0) < 0)
    {
      int ret = -errno;
      close(fd);
      fprintf(stderr, "NOTERAM_TEST clear failed: %d\n", errno);
      return ret;
    }

  close(fd);
  printf("NOTERAM_TEST CLEAR complete\n");
  return 0;
}

static int set_mode(const char *arg)
{
  unsigned int mode;
  int fd;

  if (strcmp(arg, "on") == 0)
    {
      mode = NOTE_MODE_OVERWRITE_ENABLE;
    }
  else if (strcmp(arg, "off") == 0)
    {
      mode = NOTE_MODE_OVERWRITE_DISABLE;
    }
  else
    {
      return -EINVAL;
    }

  fd = noteram_open();
  if (fd < 0)
    {
      return -errno;
    }

  if (ioctl(fd, NOTE_SETMODE, (unsigned long)(uintptr_t)&mode) < 0)
    {
      int ret = -errno;
      close(fd);
      fprintf(stderr, "NOTERAM_TEST set mode failed: %d\n", errno);
      return ret;
    }

  close(fd);
  printf("NOTERAM_TEST MODE mode=%u\n", mode);
  return 0;
}

static int burst_notes(const char *arg)
{
  unsigned long count;
  char *end;
  unsigned long i;

  errno = 0;
  count = strtoul(arg, &end, 0);
  if (errno != 0 || *arg == '\0' || *end != '\0' || count == 0)
    {
      return -EINVAL;
    }

  for (i = 0; i < count; i++)
    {
      sched_yield();
    }

  printf("NOTERAM_TEST BURST count=%lu\n", count);
  return print_status();
}

int main(int argc, char *argv[])
{
  int ret;

  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      ret = print_status();
    }
  else if (argc == 2 && strcmp(argv[1], "clear") == 0)
    {
      ret = clear_buffer();
    }
  else if (argc == 3 && strcmp(argv[1], "mode") == 0)
    {
      ret = set_mode(argv[2]);
    }
  else if (argc == 3 && strcmp(argv[1], "burst") == 0)
    {
      ret = burst_notes(argv[2]);
    }
  else
    {
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }

  return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
