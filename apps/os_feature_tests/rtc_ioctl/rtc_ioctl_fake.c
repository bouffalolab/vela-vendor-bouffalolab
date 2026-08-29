/****************************************************************************
 * apps/vendor/bouffalolab/apps/os_feature_tests/rtc_ioctl/rtc_ioctl_fake.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <nuttx/timers/rtc.h>

#include "rtc_ioctl_test.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl_rtc_ioctl_test_lower_s
{
  FAR const struct rtc_ops_s *ops;
  struct bl_rtc_ioctl_test_snapshot_s snapshot;
  struct rtc_time time;
#ifdef CONFIG_RTC_ALARM
  struct rtc_time alarm_time;
#endif
  bool have_set_time;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bl_rtc_ioctl_test_rdtime(FAR struct rtc_lowerhalf_s *lower,
                                    FAR struct rtc_time *rtctime);
static int bl_rtc_ioctl_test_settime(FAR struct rtc_lowerhalf_s *lower,
                                     FAR const struct rtc_time *rtctime);
static bool bl_rtc_ioctl_test_havesettime(
  FAR struct rtc_lowerhalf_s *lower);
#ifdef CONFIG_RTC_ALARM
static int bl_rtc_ioctl_test_setalarm(
  FAR struct rtc_lowerhalf_s *lower,
  FAR const struct lower_setalarm_s *alarminfo);
static int bl_rtc_ioctl_test_setrelative(
  FAR struct rtc_lowerhalf_s *lower,
  FAR const struct lower_setrelative_s *alarminfo);
static int bl_rtc_ioctl_test_cancelalarm(FAR struct rtc_lowerhalf_s *lower,
                                         int alarmid);
static int bl_rtc_ioctl_test_rdalarm(
  FAR struct rtc_lowerhalf_s *lower,
  FAR struct lower_rdalarm_s *alarminfo);
#endif
#ifdef CONFIG_RTC_PERIODIC
static int bl_rtc_ioctl_test_setperiodic(
  FAR struct rtc_lowerhalf_s *lower,
  FAR const struct lower_setperiodic_s *alarminfo);
static int bl_rtc_ioctl_test_cancelperiodic(
  FAR struct rtc_lowerhalf_s *lower, int id);
#endif
#ifdef CONFIG_RTC_IOCTL
static int bl_rtc_ioctl_test_ioctl(FAR struct rtc_lowerhalf_s *lower,
                                   int cmd, unsigned long arg);
#endif
static int bl_rtc_ioctl_test_destroy(FAR struct rtc_lowerhalf_s *lower);
static int bl_rtc_initialize_sentinel_ioctl(FAR struct file *filep,
                                            int cmd,
                                            unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct rtc_ops_s g_bl_rtc_ioctl_test_ops =
{
  .rdtime = bl_rtc_ioctl_test_rdtime,
  .settime = bl_rtc_ioctl_test_settime,
  .havesettime = bl_rtc_ioctl_test_havesettime,
#ifdef CONFIG_RTC_ALARM
  .setalarm = bl_rtc_ioctl_test_setalarm,
  .setrelative = bl_rtc_ioctl_test_setrelative,
  .cancelalarm = bl_rtc_ioctl_test_cancelalarm,
  .rdalarm = bl_rtc_ioctl_test_rdalarm,
#endif
#ifdef CONFIG_RTC_PERIODIC
  .setperiodic = bl_rtc_ioctl_test_setperiodic,
  .cancelperiodic = bl_rtc_ioctl_test_cancelperiodic,
#endif
#ifdef CONFIG_RTC_IOCTL
  .ioctl = bl_rtc_ioctl_test_ioctl,
#endif
  .destroy = bl_rtc_ioctl_test_destroy,
};

static const struct file_operations g_bl_rtc_initialize_sentinel_fops =
{
  .ioctl = bl_rtc_initialize_sentinel_ioctl,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static const struct rtc_ops_s g_bl_rtc_ioctl_test_missing_ops;

static struct bl_rtc_ioctl_test_lower_s g_bl_rtc_ioctl_test_lower =
{
  .ops = &g_bl_rtc_ioctl_test_ops,
};

static struct rtc_lowerhalf_s g_bl_rtc_ioctl_test_missing_lower =
{
  .ops = &g_bl_rtc_ioctl_test_missing_ops,
};

static struct rtc_lowerhalf_s g_bl_rtc_initialize_null_ops_lower;
static struct rtc_lowerhalf_s g_bl_rtc_initialize_empty_lower =
{
  .ops = &g_bl_rtc_ioctl_test_missing_ops,
};

static struct bl_rtc_ioctl_test_lower_s g_bl_rtc_initialize_invalid_lower =
{
  .ops = &g_bl_rtc_ioctl_test_ops,
};

static struct bl_rtc_ioctl_test_lower_s g_bl_rtc_initialize_owner_lower =
{
  .ops = &g_bl_rtc_ioctl_test_ops,
};

static struct bl_rtc_ioctl_test_lower_s
  g_bl_rtc_initialize_challenger_lower =
{
  .ops = &g_bl_rtc_ioctl_test_ops,
};

static struct bl_rtc_ioctl_test_lower_s g_bl_rtc_initialize_boundary_lower =
{
  .ops = &g_bl_rtc_ioctl_test_ops,
};

static FAR struct bl_rtc_ioctl_test_lower_s *
bl_rtc_ioctl_test_from_lower(FAR struct rtc_lowerhalf_s *lower)
{
  return (FAR struct bl_rtc_ioctl_test_lower_s *)lower;
}

static void bl_rtc_ioctl_test_record_method(
  FAR struct bl_rtc_ioctl_test_lower_s *priv,
  enum bl_rtc_ioctl_test_method_e method)
{
  priv->snapshot.previous_method = priv->snapshot.last_method;
  priv->snapshot.last_method = method;
}

static int bl_rtc_ioctl_test_rdtime(FAR struct rtc_lowerhalf_s *lower,
                                    FAR struct rtc_time *rtctime)
{
  FAR struct bl_rtc_ioctl_test_lower_s *priv =
    bl_rtc_ioctl_test_from_lower(lower);

  bl_rtc_ioctl_test_record_method(priv, BL_RTC_IOCTL_TEST_METHOD_RDTIME);
  priv->snapshot.rdtime_calls++;
  if (rtctime == NULL)
    {
      priv->snapshot.null_argument_calls++;
      return -EFAULT;
    }

  *rtctime = priv->time;
  return OK;
}

static int bl_rtc_ioctl_test_settime(FAR struct rtc_lowerhalf_s *lower,
                                     FAR const struct rtc_time *rtctime)
{
  FAR struct bl_rtc_ioctl_test_lower_s *priv =
    bl_rtc_ioctl_test_from_lower(lower);

  bl_rtc_ioctl_test_record_method(priv, BL_RTC_IOCTL_TEST_METHOD_SETTIME);
  priv->snapshot.settime_calls++;
  if (rtctime == NULL)
    {
      priv->snapshot.null_argument_calls++;
      return -EFAULT;
    }

  priv->snapshot.last_time = *rtctime;
  priv->time = *rtctime;
  priv->have_set_time = true;
  return OK;
}

static bool bl_rtc_ioctl_test_havesettime(
  FAR struct rtc_lowerhalf_s *lower)
{
  FAR struct bl_rtc_ioctl_test_lower_s *priv =
    bl_rtc_ioctl_test_from_lower(lower);

  bl_rtc_ioctl_test_record_method(priv,
                                  BL_RTC_IOCTL_TEST_METHOD_HAVESETTIME);
  priv->snapshot.havesettime_calls++;
  return priv->have_set_time;
}

#ifdef CONFIG_RTC_ALARM
static int bl_rtc_ioctl_test_setalarm(
  FAR struct rtc_lowerhalf_s *lower,
  FAR const struct lower_setalarm_s *alarminfo)
{
  FAR struct bl_rtc_ioctl_test_lower_s *priv =
    bl_rtc_ioctl_test_from_lower(lower);

  bl_rtc_ioctl_test_record_method(priv, BL_RTC_IOCTL_TEST_METHOD_SETALARM);
  priv->snapshot.setalarm_calls++;
  if (alarminfo == NULL)
    {
      priv->snapshot.null_argument_calls++;
      return -EFAULT;
    }

  priv->snapshot.last_id = alarminfo->id;
  priv->snapshot.setalarm_context_valid =
    alarminfo->cb != NULL && alarminfo->priv != NULL;
  priv->snapshot.last_alarm_time = alarminfo->time;
  priv->alarm_time = alarminfo->time;
  return OK;
}

static int bl_rtc_ioctl_test_setrelative(
  FAR struct rtc_lowerhalf_s *lower,
  FAR const struct lower_setrelative_s *alarminfo)
{
  FAR struct bl_rtc_ioctl_test_lower_s *priv =
    bl_rtc_ioctl_test_from_lower(lower);

  bl_rtc_ioctl_test_record_method(priv,
                                  BL_RTC_IOCTL_TEST_METHOD_SETRELATIVE);
  priv->snapshot.setrelative_calls++;
  if (alarminfo == NULL)
    {
      priv->snapshot.null_argument_calls++;
      return -EFAULT;
    }

  priv->snapshot.last_id = alarminfo->id;
  priv->snapshot.setrelative_context_valid =
    alarminfo->cb != NULL && alarminfo->priv != NULL;
  priv->snapshot.last_reltime = alarminfo->reltime;
  return OK;
}

static int bl_rtc_ioctl_test_cancelalarm(FAR struct rtc_lowerhalf_s *lower,
                                         int alarmid)
{
  FAR struct bl_rtc_ioctl_test_lower_s *priv =
    bl_rtc_ioctl_test_from_lower(lower);

  bl_rtc_ioctl_test_record_method(priv,
                                  BL_RTC_IOCTL_TEST_METHOD_CANCELALARM);
  priv->snapshot.cancelalarm_calls++;
  priv->snapshot.last_id = alarmid;
  return OK;
}

static int bl_rtc_ioctl_test_rdalarm(
  FAR struct rtc_lowerhalf_s *lower,
  FAR struct lower_rdalarm_s *alarminfo)
{
  FAR struct bl_rtc_ioctl_test_lower_s *priv =
    bl_rtc_ioctl_test_from_lower(lower);

  bl_rtc_ioctl_test_record_method(priv, BL_RTC_IOCTL_TEST_METHOD_RDALARM);
  priv->snapshot.rdalarm_calls++;
  if (alarminfo == NULL || alarminfo->time == NULL)
    {
      priv->snapshot.null_argument_calls++;
      return -EFAULT;
    }

  priv->snapshot.last_id = alarminfo->id;
  priv->snapshot.rdalarm_context_valid = alarminfo->priv != NULL;
  *alarminfo->time = priv->alarm_time;
  return OK;
}
#endif

#ifdef CONFIG_RTC_PERIODIC
static int bl_rtc_ioctl_test_setperiodic(
  FAR struct rtc_lowerhalf_s *lower,
  FAR const struct lower_setperiodic_s *alarminfo)
{
  FAR struct bl_rtc_ioctl_test_lower_s *priv =
    bl_rtc_ioctl_test_from_lower(lower);

  bl_rtc_ioctl_test_record_method(priv,
                                  BL_RTC_IOCTL_TEST_METHOD_SETPERIODIC);
  priv->snapshot.setperiodic_calls++;
  if (alarminfo == NULL)
    {
      priv->snapshot.null_argument_calls++;
      return -EFAULT;
    }

  priv->snapshot.last_id = alarminfo->id;
  priv->snapshot.setperiodic_context_valid =
    alarminfo->cb != NULL && alarminfo->priv != NULL;
  priv->snapshot.last_period = alarminfo->period;
  return OK;
}

static int bl_rtc_ioctl_test_cancelperiodic(
  FAR struct rtc_lowerhalf_s *lower, int id)
{
  FAR struct bl_rtc_ioctl_test_lower_s *priv =
    bl_rtc_ioctl_test_from_lower(lower);

  bl_rtc_ioctl_test_record_method(priv,
                                  BL_RTC_IOCTL_TEST_METHOD_CANCELPERIODIC);
  priv->snapshot.cancelperiodic_calls++;
  priv->snapshot.last_id = id;
  return OK;
}
#endif

#ifdef CONFIG_RTC_IOCTL
static int bl_rtc_ioctl_test_ioctl(FAR struct rtc_lowerhalf_s *lower,
                                   int cmd, unsigned long arg)
{
  FAR struct bl_rtc_ioctl_test_lower_s *priv =
    bl_rtc_ioctl_test_from_lower(lower);

  bl_rtc_ioctl_test_record_method(priv, BL_RTC_IOCTL_TEST_METHOD_IOCTL);
  priv->snapshot.ioctl_calls++;
  priv->snapshot.last_cmd = cmd;
  priv->snapshot.last_arg = arg;
  return BL_RTC_IOCTL_TEST_PRIVATE_RESULT;
}
#endif

static int bl_rtc_ioctl_test_destroy(FAR struct rtc_lowerhalf_s *lower)
{
  FAR struct bl_rtc_ioctl_test_lower_s *priv =
    bl_rtc_ioctl_test_from_lower(lower);

  priv->snapshot.destroy_calls++;
  return OK;
}

static int bl_rtc_initialize_sentinel_ioctl(FAR struct file *filep,
                                            int cmd,
                                            unsigned long arg)
{
  (void)filep;
  (void)cmd;
  (void)arg;
  return BL_RTC_IOCTL_TEST_PRIVATE_RESULT;
}

static void bl_rtc_initialize_reset_lower(
  FAR struct bl_rtc_ioctl_test_lower_s *lower)
{
  memset(&lower->snapshot, 0, sizeof(lower->snapshot));
  memset(&lower->time, 0, sizeof(lower->time));
#ifdef CONFIG_RTC_ALARM
  memset(&lower->alarm_time, 0, sizeof(lower->alarm_time));
#endif
  lower->have_set_time = false;
  lower->snapshot.guard_head = BL_RTC_IOCTL_TEST_GUARD_HEAD;
  lower->snapshot.guard_tail = BL_RTC_IOCTL_TEST_GUARD_TAIL;
  lower->snapshot.last_id = -1;
}

static int bl_rtc_initialize_sentinel_check(FAR const char *path,
                                            FAR bool *alive)
{
  int fd;
  int ret;
  int errcode;

  fd = open(path, O_RDWR);
  if (fd < 0)
    {
      return -errno;
    }

  ret = ioctl(fd, RTC_RD_TIME, 0);
  *alive = ret == BL_RTC_IOCTL_TEST_PRIVATE_RESULT;

  errcode = errno;
  ret = close(fd);
  if (ret < 0)
    {
      return -errno;
    }

  errno = errcode;
  return OK;
}

static int bl_rtc_initialize_invalid_case(int minor,
                                          FAR struct rtc_lowerhalf_s *lower,
                                          FAR const char *path,
                                          FAR int *ret,
                                          FAR bool *sentinel_ok)
{
  int check_ret;
  int cleanup_ret;

  cleanup_ret = register_driver(path, &g_bl_rtc_initialize_sentinel_fops,
                                0666, NULL);
  if (cleanup_ret < 0)
    {
      return cleanup_ret;
    }

  *ret = rtc_initialize(minor, lower);
  check_ret = bl_rtc_initialize_sentinel_check(path, sentinel_ok);

  cleanup_ret = unregister_driver(path);
  if (cleanup_ret < 0)
    {
      return cleanup_ret;
    }

  return check_ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bl_rtc_ioctl_test_lower_reset(void)
{
  memset(&g_bl_rtc_ioctl_test_lower.snapshot, 0,
         sizeof(g_bl_rtc_ioctl_test_lower.snapshot));
  memset(&g_bl_rtc_ioctl_test_lower.time, 0,
         sizeof(g_bl_rtc_ioctl_test_lower.time));
#ifdef CONFIG_RTC_ALARM
  memset(&g_bl_rtc_ioctl_test_lower.alarm_time, 0,
         sizeof(g_bl_rtc_ioctl_test_lower.alarm_time));
#endif
  g_bl_rtc_ioctl_test_lower.have_set_time = false;
  g_bl_rtc_ioctl_test_lower.snapshot.guard_head =
    BL_RTC_IOCTL_TEST_GUARD_HEAD;
  g_bl_rtc_ioctl_test_lower.snapshot.guard_tail =
    BL_RTC_IOCTL_TEST_GUARD_TAIL;
  g_bl_rtc_ioctl_test_lower.snapshot.last_id = -1;
}

int bl_rtc_ioctl_test_lower_snapshot(
  FAR struct bl_rtc_ioctl_test_snapshot_s *snapshot)
{
  if (snapshot == NULL)
    {
      return -EINVAL;
    }

  *snapshot = g_bl_rtc_ioctl_test_lower.snapshot;
  return OK;
}

int bl_rtc_initialize_test_run(
  FAR struct bl_rtc_initialize_test_result_s *result)
{
  static const char *const invalid_paths[] = {
    "/dev/rtc90",
    "/dev/rtc91",
    "/dev/rtc-1",
    "/dev/rtc1000",
    "/dev/rtc-2147483648",
    "/dev/rtc2147483647",
  };

  struct mallinfo heap;
  struct rtc_time time;
  int cleanup_ret = OK;
  int empty_fd = -1;
  int fd1 = -1;
  int fd2 = -1;
  int ret;
  int i;

  if (result == NULL)
    {
      return -EINVAL;
    }

  memset(result, 0, sizeof(*result));
  memset(result->invalid_ret, 0xff, sizeof(result->invalid_ret));
  result->register_ret = -1;
  result->open1_ret = -1;
  result->open2_ret = -1;
  result->read_ret = -1;
  result->duplicate_ret = -1;
  result->owner_after_conflict_ret = -1;
  result->unlink_ret = -1;
  result->open_after_unlink_ret = -1;
  result->old_fd_ret = -1;
  result->close1_ret = -1;
  result->close2_ret = -1;
  result->boundary_register_ret = -1;
  result->boundary_unlink_ret = -1;
  result->empty_register_ret = -1;
  result->empty_open_ret = -1;
  result->empty_ioctl_ret = -1;
  result->empty_close_ret = -1;
  result->empty_unlink_ret = -1;

  bl_rtc_initialize_reset_lower(&g_bl_rtc_initialize_invalid_lower);
  ret = bl_rtc_initialize_invalid_case(90, NULL, invalid_paths[0],
                                       &result->invalid_ret[0],
                                       &result->invalid_sentinel_ok[0]);
  if (ret < 0)
    {
      return ret;
    }

  ret = bl_rtc_initialize_invalid_case(
    91, &g_bl_rtc_initialize_null_ops_lower, invalid_paths[1],
    &result->invalid_ret[1], &result->invalid_sentinel_ok[1]);
  if (ret < 0)
    {
      return ret;
    }

  ret = bl_rtc_initialize_invalid_case(
    -1, (FAR struct rtc_lowerhalf_s *)&g_bl_rtc_initialize_invalid_lower,
    invalid_paths[2], &result->invalid_ret[2],
    &result->invalid_sentinel_ok[2]);
  if (ret < 0)
    {
      return ret;
    }

  ret = bl_rtc_initialize_invalid_case(
    1000, (FAR struct rtc_lowerhalf_s *)&g_bl_rtc_initialize_invalid_lower,
    invalid_paths[3], &result->invalid_ret[3],
    &result->invalid_sentinel_ok[3]);
  if (ret < 0)
    {
      return ret;
    }

  ret = bl_rtc_initialize_invalid_case(
    INT_MIN,
    (FAR struct rtc_lowerhalf_s *)&g_bl_rtc_initialize_invalid_lower,
    invalid_paths[4], &result->invalid_ret[4],
    &result->invalid_sentinel_ok[4]);
  if (ret < 0)
    {
      return ret;
    }

  ret = bl_rtc_initialize_invalid_case(
    INT_MAX,
    (FAR struct rtc_lowerhalf_s *)&g_bl_rtc_initialize_invalid_lower,
    invalid_paths[5], &result->invalid_ret[5],
    &result->invalid_sentinel_ok[5]);
  if (ret < 0)
    {
      return ret;
    }

  result->invalid_destroy_calls =
    g_bl_rtc_initialize_invalid_lower.snapshot.destroy_calls;

  bl_rtc_initialize_reset_lower(&g_bl_rtc_initialize_owner_lower);
  bl_rtc_initialize_reset_lower(&g_bl_rtc_initialize_challenger_lower);
  result->register_ret = rtc_initialize(
    97, (FAR struct rtc_lowerhalf_s *)&g_bl_rtc_initialize_owner_lower);
  if (result->register_ret == OK)
    {
      errno = 0;
      fd1 = open("/dev/rtc97", O_RDWR);
      result->open1_ret = fd1;
      result->open1_errno = errno;

      errno = 0;
      fd2 = open("/dev/rtc97", O_RDWR);
      result->open2_ret = fd2;
      result->open2_errno = errno;

      if (fd1 >= 0)
        {
          memset(&time, 0, sizeof(time));
          errno = 0;
          result->read_ret = ioctl(fd1, RTC_RD_TIME,
                                   (unsigned long)(uintptr_t)&time);
          result->read_errno = errno;
        }

      heap = kmm_mallinfo();
      result->heap_before_used = heap.uordblks;
      result->heap_before_allocs = heap.aordblks;
      result->duplicate_all_eexist = true;
      for (i = 0; i < 64; i++)
        {
          ret = rtc_initialize(
            97,
            (FAR struct rtc_lowerhalf_s *)
              &g_bl_rtc_initialize_challenger_lower);
          if (i == 0)
            {
              result->duplicate_ret = ret;
            }

          if (ret != -EEXIST)
            {
              result->duplicate_all_eexist = false;
            }
        }

      heap = kmm_mallinfo();
      result->heap_after_used = heap.uordblks;
      result->heap_after_allocs = heap.aordblks;
      result->challenger_destroy_calls =
        g_bl_rtc_initialize_challenger_lower.snapshot.destroy_calls;

      if (fd1 >= 0)
        {
          errno = 0;
          result->owner_after_conflict_ret = ioctl(
            fd1, RTC_RD_TIME, (unsigned long)(uintptr_t)&time);
          result->owner_after_conflict_errno = errno;
        }

      errno = 0;
      result->unlink_ret = unlink("/dev/rtc97");
      result->unlink_errno = errno;

      errno = 0;
      result->open_after_unlink_ret = open("/dev/rtc97", O_RDWR);
      result->open_after_unlink_errno = errno;
      if (result->open_after_unlink_ret >= 0)
        {
          ret = close(result->open_after_unlink_ret);
          if (ret < 0)
            {
              cleanup_ret = -errno;
            }
        }

      if (fd2 >= 0)
        {
          errno = 0;
          result->old_fd_ret = ioctl(
            fd2, RTC_RD_TIME, (unsigned long)(uintptr_t)&time);
          result->old_fd_errno = errno;
        }

      if (fd1 >= 0)
        {
          errno = 0;
          result->close1_ret = close(fd1);
          result->close1_errno = errno;
          fd1 = -1;
        }

      result->destroy_after_close1 =
        g_bl_rtc_initialize_owner_lower.snapshot.destroy_calls;

      if (fd2 >= 0)
        {
          errno = 0;
          result->close2_ret = close(fd2);
          result->close2_errno = errno;
          fd2 = -1;
        }

      result->destroy_after_close2 =
        g_bl_rtc_initialize_owner_lower.snapshot.destroy_calls;
    }

  if (fd1 >= 0)
    {
      ret = close(fd1);
      if (ret < 0 && cleanup_ret == OK)
        {
          cleanup_ret = -errno;
        }
    }

  if (fd2 >= 0)
    {
      ret = close(fd2);
      if (ret < 0 && cleanup_ret == OK)
        {
          cleanup_ret = -errno;
        }
    }

  if (result->register_ret == OK && result->unlink_ret != OK)
    {
      ret = unregister_driver("/dev/rtc97");
      if (ret < 0 && cleanup_ret == OK)
        {
          cleanup_ret = ret;
        }
    }

  if (cleanup_ret < 0)
    {
      return cleanup_ret;
    }

  bl_rtc_initialize_reset_lower(&g_bl_rtc_initialize_boundary_lower);
  result->boundary_register_ret = rtc_initialize(
    999, (FAR struct rtc_lowerhalf_s *)&g_bl_rtc_initialize_boundary_lower);
  if (result->boundary_register_ret == OK)
    {
      errno = 0;
      result->boundary_unlink_ret = unlink("/dev/rtc999");
      result->boundary_unlink_errno = errno;
      result->boundary_destroy_calls =
        g_bl_rtc_initialize_boundary_lower.snapshot.destroy_calls;
    }

  if (result->boundary_register_ret == OK &&
      result->boundary_unlink_ret != OK)
    {
      ret = unregister_driver("/dev/rtc999");
      if (ret < 0)
        {
          return ret;
        }
    }

  heap = kmm_mallinfo();
  result->empty_heap_before_used = heap.uordblks;
  result->empty_heap_before_allocs = heap.aordblks;
  result->empty_register_ret = rtc_initialize(
    96, &g_bl_rtc_initialize_empty_lower);
  if (result->empty_register_ret == OK)
    {
      errno = 0;
      result->empty_open_ret = open("/dev/rtc96", O_RDWR);
      result->empty_open_errno = errno;
      if (result->empty_open_ret >= 0)
        {
          empty_fd = result->empty_open_ret;
          errno = 0;
          result->empty_ioctl_ret = ioctl(
            empty_fd, RTC_RD_TIME,
            (unsigned long)(uintptr_t)&time);
          result->empty_ioctl_errno = errno;

          errno = 0;
          result->empty_close_ret = close(empty_fd);
          result->empty_close_errno = errno;
          if (result->empty_close_ret == OK)
            {
              empty_fd = -1;
            }
        }

      errno = 0;
      result->empty_unlink_ret = unlink("/dev/rtc96");
      result->empty_unlink_errno = errno;

      heap = kmm_mallinfo();
      result->empty_heap_after_used = heap.uordblks;
      result->empty_heap_after_allocs = heap.aordblks;
    }

  if (empty_fd >= 0)
    {
      ret = close(empty_fd);
      if (ret < 0)
        {
          cleanup_ret = -errno;
        }
    }

  if (result->empty_register_ret == OK && result->empty_unlink_ret != OK)
    {
      ret = unregister_driver("/dev/rtc96");
      if (ret < 0 && cleanup_ret == OK)
        {
          cleanup_ret = ret;
        }
    }

  return cleanup_ret;
}

int bl_rtc_ioctl_test_lower_register(void)
{
  int init_ret;
  int ret;

  bl_rtc_ioctl_test_lower_reset();
  ret = rtc_initialize(99,
    (FAR struct rtc_lowerhalf_s *)&g_bl_rtc_ioctl_test_lower);
  if (ret < 0)
    {
      return ret;
    }

  ret = rtc_initialize(98, &g_bl_rtc_ioctl_test_missing_lower);
  if (ret < 0)
    {
      init_ret = ret;
      ret = unlink(BL_RTC_IOCTL_TEST_DEVPATH);
      if (ret < 0)
        {
          return -errno;
        }

      return init_ret;
    }

  return ret;
}
