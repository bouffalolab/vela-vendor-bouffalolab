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
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

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
