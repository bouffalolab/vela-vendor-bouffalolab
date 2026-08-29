/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_rtc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/timers/arch_rtc.h>
#include <nuttx/timers/rtc.h>

#include <arch/irq.h>

#include "bl616cl_rtc.h"
#include "bl616cl_rtc_hw.h"

#if defined(CONFIG_BL616CL_RTC_ALARM) && CONFIG_RTC_NALARMS != 1
#error "BL616CL RTC supports exactly one alarm"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_RTC_COUNTER_MASK    UINT64_C(0x0000ffffffffffff)
#define BL616CL_RTC_NSEC_PER_SEC    UINT64_C(1000000000)
#define BL616CL_RTC_MIN_ALARM_TICKS UINT64_C(2)
#define BL616CL_RTC_PROGRAM_GUARD   UINT64_C(32)

#ifdef CONFIG_BL616CL_RTC_CLOCK_DIG32K
#define BL616CL_RTC_CLOCK_NUMERATOR   UINT64_C(40000000)
#define BL616CL_RTC_CLOCK_DENOMINATOR UINT64_C(1221)
#else
#define BL616CL_RTC_CLOCK_NUMERATOR   UINT64_C(32768)
#define BL616CL_RTC_CLOCK_DENOMINATOR UINT64_C(1)
#endif

#ifdef CONFIG_SYSTEM_TIME64
#define BL616CL_RTC_TIME_MAX INT64_MAX
#else
#define BL616CL_RTC_TIME_MAX INT32_MAX
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl616cl_rtc_lowerhalf_s
{
  FAR const struct rtc_ops_s *ops;
  spinlock_t lock;
  time_t epoch_base;
  uint64_t counter_base;
  uint32_t nanosecond_base;
  bool initialized;
  bool time_set;

#ifdef CONFIG_BL616CL_RTC_ALARM
  rtc_alarm_callback_t alarm_callback;
  FAR void *alarm_arg;
  struct rtc_time alarm_time;
  time_t alarm_epoch;
  uint32_t alarm_nanosecond;
  bool alarm_active;
  bool alarm_irq_ready;
  bool alarm_wait_for_clock;
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bl616cl_rtc_rdtime(FAR struct rtc_lowerhalf_s *lower,
                              FAR struct rtc_time *rtctime);
static int bl616cl_rtc_settime(FAR struct rtc_lowerhalf_s *lower,
                               FAR const struct rtc_time *rtctime);
static bool bl616cl_rtc_havesettime(FAR struct rtc_lowerhalf_s *lower);

#ifdef CONFIG_BL616CL_RTC_ALARM
static int bl616cl_rtc_setalarm(FAR struct rtc_lowerhalf_s *lower,
                                FAR const struct lower_setalarm_s
                                  *alarminfo);
static int bl616cl_rtc_setrelative(
  FAR struct rtc_lowerhalf_s *lower,
  FAR const struct lower_setrelative_s *alarminfo);
static int bl616cl_rtc_cancelalarm(FAR struct rtc_lowerhalf_s *lower,
                                   int alarmid);
static int bl616cl_rtc_rdalarm(FAR struct rtc_lowerhalf_s *lower,
                               FAR struct lower_rdalarm_s *alarminfo);
#endif

#ifndef CONFIG_DISABLE_PSEUDOFS_OPERATIONS
static int bl616cl_rtc_destroy(FAR struct rtc_lowerhalf_s *lower);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct rtc_ops_s g_bl616cl_rtc_ops =
{
  .rdtime = bl616cl_rtc_rdtime,
  .settime = bl616cl_rtc_settime,
  .havesettime = bl616cl_rtc_havesettime,
#ifdef CONFIG_BL616CL_RTC_ALARM
  .setalarm = bl616cl_rtc_setalarm,
  .setrelative = bl616cl_rtc_setrelative,
  .cancelalarm = bl616cl_rtc_cancelalarm,
  .rdalarm = bl616cl_rtc_rdalarm,
#endif
#ifdef CONFIG_RTC_IOCTL
  .ioctl = NULL,
#endif
#ifndef CONFIG_DISABLE_PSEUDOFS_OPERATIONS
  .destroy = bl616cl_rtc_destroy,
#endif
};

static struct bl616cl_rtc_lowerhalf_s g_bl616cl_rtc =
{
  .ops = &g_bl616cl_rtc_ops,
  .lock = SP_UNLOCKED,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint64_t bl616cl_rtc_ticks_to_seconds(uint64_t ticks,
                                             FAR uint32_t *nanoseconds)
{
  uint64_t scaled;
  uint64_t remainder;

  scaled = ticks * BL616CL_RTC_CLOCK_DENOMINATOR;
  remainder = scaled % BL616CL_RTC_CLOCK_NUMERATOR;
  *nanoseconds = (uint32_t)((remainder * BL616CL_RTC_NSEC_PER_SEC) /
                            BL616CL_RTC_CLOCK_NUMERATOR);
  return scaled / BL616CL_RTC_CLOCK_NUMERATOR;
}

#ifdef CONFIG_BL616CL_RTC_ALARM
static int bl616cl_rtc_duration_to_ticks(uint64_t seconds,
                                         uint32_t nanoseconds,
                                         FAR uint64_t *ticks)
{
  uint64_t denominator;
  uint64_t fraction;
  uint64_t quotient;
  uint64_t remainder;
  uint64_t result;

  quotient = BL616CL_RTC_CLOCK_NUMERATOR /
             BL616CL_RTC_CLOCK_DENOMINATOR;
  remainder = BL616CL_RTC_CLOCK_NUMERATOR %
              BL616CL_RTC_CLOCK_DENOMINATOR;

  if (seconds > BL616CL_RTC_COUNTER_MASK / quotient)
    {
      return -ERANGE;
    }

  result = seconds * quotient;
  result += (seconds / BL616CL_RTC_CLOCK_DENOMINATOR) * remainder;

  denominator = BL616CL_RTC_CLOCK_DENOMINATOR *
                BL616CL_RTC_NSEC_PER_SEC;
  fraction = (seconds % BL616CL_RTC_CLOCK_DENOMINATOR) * remainder *
             BL616CL_RTC_NSEC_PER_SEC;
  fraction += (uint64_t)nanoseconds * BL616CL_RTC_CLOCK_NUMERATOR;
  result += (fraction + denominator - 1) / denominator;

  if (result > BL616CL_RTC_COUNTER_MASK)
    {
      return -ERANGE;
    }

  *ticks = result;
  return OK;
}
#endif

static int bl616cl_rtc_time_to_epoch(FAR const struct rtc_time *rtctime,
                                     FAR time_t *epoch,
                                     FAR uint32_t *nanosecond)
{
  struct tm converted;
  struct tm input;
  time_t value;

  if (rtctime == NULL || epoch == NULL || nanosecond == NULL ||
      rtctime->tm_sec < 0 || rtctime->tm_sec > 59 ||
      rtctime->tm_min < 0 || rtctime->tm_min > 59 ||
      rtctime->tm_hour < 0 || rtctime->tm_hour > 23 ||
      rtctime->tm_mday < 1 || rtctime->tm_mday > 31 ||
      rtctime->tm_mon < 0 || rtctime->tm_mon > 11 ||
      rtctime->tm_year < 70)
    {
      return -EINVAL;
    }

#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  if (rtctime->tm_nsec < 0 || rtctime->tm_nsec >= NSEC_PER_SEC)
    {
      return -EINVAL;
    }
#endif

  memcpy(&input, rtctime, sizeof(input));
  value = timegm(&input);
  if (value < 0 || gmtime_r(&value, &converted) == NULL ||
      converted.tm_sec != rtctime->tm_sec ||
      converted.tm_min != rtctime->tm_min ||
      converted.tm_hour != rtctime->tm_hour ||
      converted.tm_mday != rtctime->tm_mday ||
      converted.tm_mon != rtctime->tm_mon ||
      converted.tm_year != rtctime->tm_year)
    {
      return -EINVAL;
    }

  *epoch = value;
#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  *nanosecond = rtctime->tm_nsec;
#else
  *nanosecond = 0;
#endif
  return OK;
}

static int bl616cl_rtc_snapshot_locked(
  FAR struct bl616cl_rtc_lowerhalf_s *priv,
  FAR time_t *epoch, FAR uint32_t *nanosecond, FAR uint64_t *counter)
{
  uint64_t base_counter;
  uint64_t elapsed_ticks;
  uint64_t elapsed_seconds;
  uint64_t total_nanoseconds;
  time_t base_epoch;
  uint32_t base_nanoseconds;
  uint32_t elapsed_nanoseconds;

  base_epoch = priv->epoch_base;
  base_counter = priv->counter_base;
  base_nanoseconds = priv->nanosecond_base;
  *counter = bl616cl_rtc_counter();

  elapsed_ticks = (*counter - base_counter) & BL616CL_RTC_COUNTER_MASK;
  elapsed_seconds = bl616cl_rtc_ticks_to_seconds(elapsed_ticks,
                                                 &elapsed_nanoseconds);
  total_nanoseconds = (uint64_t)base_nanoseconds + elapsed_nanoseconds;
  elapsed_seconds += total_nanoseconds / BL616CL_RTC_NSEC_PER_SEC;
  if (elapsed_seconds > (uint64_t)BL616CL_RTC_TIME_MAX -
                          (uint64_t)base_epoch)
    {
      return -ERANGE;
    }

  *epoch = base_epoch + (time_t)elapsed_seconds;
  *nanosecond = total_nanoseconds % BL616CL_RTC_NSEC_PER_SEC;
  return OK;
}

static int bl616cl_rtc_snapshot(
  FAR struct bl616cl_rtc_lowerhalf_s *priv,
  FAR time_t *epoch, FAR uint32_t *nanosecond, FAR uint64_t *counter)
{
  irqstate_t flags;
  int ret;

  flags = spin_lock_irqsave(&priv->lock);
  ret = bl616cl_rtc_snapshot_locked(priv, epoch, nanosecond, counter);
  spin_unlock_irqrestore(&priv->lock, flags);
  return ret;
}

#ifdef CONFIG_BL616CL_RTC_ALARM
static void bl616cl_rtc_clear_alarm_hardware(void)
{
  bl616cl_rtc_hw_clear_alarm();
}

static void bl616cl_rtc_program_counter(uint64_t target_counter,
                                        uint64_t requested_delta)
{
  uint64_t start_counter;
  uint64_t current_counter;
  uint64_t remaining_ticks;
  uint64_t remaining_after;
  bool programmed = false;
  unsigned int attempt;

  /* Keep a guard window between the counter read and compare write.  This
   * also handles a target that expires while the SDK register calls run.
   */

  start_counter = bl616cl_rtc_counter();
  remaining_ticks = (target_counter - start_counter) &
                    BL616CL_RTC_COUNTER_MASK;
  if (remaining_ticks == 0 || remaining_ticks > requested_delta ||
      remaining_ticks < BL616CL_RTC_PROGRAM_GUARD)
    {
      target_counter = (start_counter + BL616CL_RTC_PROGRAM_GUARD) &
                       BL616CL_RTC_COUNTER_MASK;
      remaining_ticks = BL616CL_RTC_PROGRAM_GUARD;
    }

  for (attempt = 0; attempt < 3; attempt++)
    {
      bl616cl_rtc_clear_alarm_hardware();
      bl616cl_rtc_hw_set_alarm(target_counter);
      current_counter = bl616cl_rtc_counter();
      remaining_after = (target_counter - current_counter) &
                        BL616CL_RTC_COUNTER_MASK;
      if (remaining_after >= BL616CL_RTC_PROGRAM_GUARD &&
          remaining_after <= remaining_ticks)
        {
          programmed = true;
          break;
        }

      /* The original target was reached before compare became active.
       */

      start_counter = current_counter;
      remaining_ticks = BL616CL_RTC_PROGRAM_GUARD << (attempt + 1);
      target_counter = (start_counter + remaining_ticks) &
                       BL616CL_RTC_COUNTER_MASK;
    }

  if (!programmed)
    {
      /* The last retry uses a 256-tick window.
       */

      bl616cl_rtc_clear_alarm_hardware();
      bl616cl_rtc_hw_set_alarm(target_counter);
    }
}

static int bl616cl_rtc_alarm_delta(time_t current_epoch,
                                   uint32_t current_nanosecond,
                                   time_t target_epoch,
                                   uint32_t target_nanosecond,
                                   FAR uint64_t *delta_ticks)
{
  uint64_t seconds;
  uint32_t nanoseconds;
  int ret;

  if (target_epoch < current_epoch ||
      (target_epoch == current_epoch &&
       target_nanosecond <= current_nanosecond))
    {
      return -ETIME;
    }

  seconds = (uint64_t)target_epoch - (uint64_t)current_epoch;
  if (target_nanosecond < current_nanosecond)
    {
      seconds--;
      nanoseconds = BL616CL_RTC_NSEC_PER_SEC - current_nanosecond +
                    target_nanosecond;
    }
  else
    {
      nanoseconds = target_nanosecond - current_nanosecond;
    }

  ret = bl616cl_rtc_duration_to_ticks(seconds, nanoseconds, delta_ticks);
  if (ret < 0)
    {
      return ret;
    }

  return *delta_ticks < BL616CL_RTC_MIN_ALARM_TICKS ? -ETIME : OK;
}

static int bl616cl_rtc_program_alarm_locked(
  FAR struct bl616cl_rtc_lowerhalf_s *priv, time_t target_epoch,
  uint32_t target_nanosecond, rtc_alarm_callback_t callback, FAR void *arg,
  FAR const struct rtc_time *alarm_time)
{
  uint64_t delta_ticks;
  uint64_t current_counter;
  uint64_t target_counter;
  time_t current_epoch;
  uint32_t current_nanosecond;
  int ret;

  if (!priv->alarm_irq_ready)
    {
      return -ENODEV;
    }

  ret = bl616cl_rtc_snapshot_locked(priv, &current_epoch,
                                    &current_nanosecond, &current_counter);
  if (ret < 0)
    {
      return ret;
    }

  ret = bl616cl_rtc_alarm_delta(current_epoch, current_nanosecond,
                                target_epoch, target_nanosecond,
                                &delta_ticks);
  if (ret < 0)
    {
      return ret;
    }

  target_counter = (current_counter + delta_ticks) &
                   BL616CL_RTC_COUNTER_MASK;
  priv->alarm_callback = callback;
  priv->alarm_arg = arg;
  priv->alarm_epoch = target_epoch;
  priv->alarm_nanosecond = target_nanosecond;
  priv->alarm_time = *alarm_time;
  priv->alarm_active = true;
  priv->alarm_wait_for_clock = false;

  bl616cl_rtc_program_counter(target_counter, delta_ticks);

  return OK;
}

static int bl616cl_rtc_interrupt(int irq, FAR void *context, FAR void *arg)
{
  FAR struct bl616cl_rtc_lowerhalf_s *priv = arg;
  rtc_alarm_callback_t callback = NULL;
  FAR void *callback_arg = NULL;
  struct timespec system_time;
  irqstate_t flags;

  UNUSED(irq);
  UNUSED(context);

  flags = spin_lock_irqsave(&priv->lock);
  if (!bl616cl_rtc_hw_alarm_pending())
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return OK;
    }

  bl616cl_rtc_clear_alarm_hardware();

  if (priv->alarm_active)
    {
      if (priv->alarm_wait_for_clock &&
          nxclock_gettime(CLOCK_REALTIME, &system_time) == OK &&
          (system_time.tv_sec < priv->alarm_epoch ||
           (system_time.tv_sec == priv->alarm_epoch &&
            system_time.tv_nsec < priv->alarm_nanosecond)))
        {
          bl616cl_rtc_program_counter(
            (bl616cl_rtc_counter() + BL616CL_RTC_MIN_ALARM_TICKS) &
              BL616CL_RTC_COUNTER_MASK,
            BL616CL_RTC_MIN_ALARM_TICKS);
          spin_unlock_irqrestore(&priv->lock, flags);
          return OK;
        }

      callback = priv->alarm_callback;
      callback_arg = priv->alarm_arg;
      priv->alarm_callback = NULL;
      priv->alarm_arg = NULL;
      priv->alarm_active = false;
      priv->alarm_wait_for_clock = false;
    }

  spin_unlock_irqrestore(&priv->lock, flags);

  if (callback != NULL)
    {
      callback(callback_arg, 0);
    }

  return OK;
}
#endif

static int bl616cl_rtc_rdtime(FAR struct rtc_lowerhalf_s *lower,
                              FAR struct rtc_time *rtctime)
{
  FAR struct bl616cl_rtc_lowerhalf_s *priv =
    (FAR struct bl616cl_rtc_lowerhalf_s *)lower;
  struct tm converted;
  uint64_t counter;
  time_t epoch;
  uint32_t nanosecond;
  int ret;

  if (priv == NULL || rtctime == NULL)
    {
      return -EINVAL;
    }

  ret = bl616cl_rtc_snapshot(priv, &epoch, &nanosecond, &counter);
  if (ret < 0)
    {
      return ret;
    }

  if (gmtime_r(&epoch, &converted) == NULL)
    {
      return -ERANGE;
    }

  memcpy(rtctime, &converted, sizeof(converted));
#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
  rtctime->tm_nsec = nanosecond;
#endif
  return OK;
}

static int bl616cl_rtc_settime(FAR struct rtc_lowerhalf_s *lower,
                               FAR const struct rtc_time *rtctime)
{
  FAR struct bl616cl_rtc_lowerhalf_s *priv =
    (FAR struct bl616cl_rtc_lowerhalf_s *)lower;
  uint64_t counter;
  time_t epoch;
  uint32_t nanosecond;
  irqstate_t flags;
  int ret;

  if (priv == NULL)
    {
      return -EINVAL;
    }

  ret = bl616cl_rtc_time_to_epoch(rtctime, &epoch, &nanosecond);
  if (ret < 0)
    {
      return ret;
    }

  flags = spin_lock_irqsave(&priv->lock);
  counter = bl616cl_rtc_counter();

#ifdef CONFIG_BL616CL_RTC_ALARM
  if (priv->alarm_active)
    {
      uint64_t delta_ticks;

      ret = bl616cl_rtc_alarm_delta(epoch, nanosecond, priv->alarm_epoch,
                                    priv->alarm_nanosecond, &delta_ticks);
      if (ret < 0 && ret != -ETIME)
        {
          spin_unlock_irqrestore(&priv->lock, flags);
          return ret;
        }

      if (ret == OK)
        {
          bl616cl_rtc_program_counter(
            (counter + delta_ticks) & BL616CL_RTC_COUNTER_MASK,
            delta_ticks);
        }
      else
        {
          /* Defer the callback until the upper half has synchronized
           * CLOCK_REALTIME. The ISR rechecks the target before notifying.
           */

          priv->alarm_wait_for_clock = true;
          bl616cl_rtc_program_counter(
            (counter + BL616CL_RTC_MIN_ALARM_TICKS) &
              BL616CL_RTC_COUNTER_MASK,
            BL616CL_RTC_MIN_ALARM_TICKS);
        }
    }
#endif

  priv->epoch_base = epoch;
  priv->counter_base = counter;
  priv->nanosecond_base = nanosecond;
  priv->time_set = true;

  spin_unlock_irqrestore(&priv->lock, flags);

  return OK;
}

#ifndef CONFIG_DISABLE_PSEUDOFS_OPERATIONS
static int bl616cl_rtc_destroy(FAR struct rtc_lowerhalf_s *lower)
{
  FAR struct bl616cl_rtc_lowerhalf_s *priv =
    (FAR struct bl616cl_rtc_lowerhalf_s *)lower;
  irqstate_t flags;

  if (priv == NULL)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&priv->lock);
  priv->initialized = false;
#ifdef CONFIG_BL616CL_RTC_ALARM
  priv->alarm_callback = NULL;
  priv->alarm_arg = NULL;
  priv->alarm_active = false;
  priv->alarm_irq_ready = false;
  priv->alarm_wait_for_clock = false;
  bl616cl_rtc_clear_alarm_hardware();
#endif
  spin_unlock_irqrestore(&priv->lock, flags);

#ifdef CONFIG_BL616CL_RTC_ALARM
  up_disable_irq(BL616CL_IRQ_HBN_OUT0);
  irq_detach(BL616CL_IRQ_HBN_OUT0);
#endif
  return OK;
}
#endif

static bool bl616cl_rtc_havesettime(FAR struct rtc_lowerhalf_s *lower)
{
  FAR struct bl616cl_rtc_lowerhalf_s *priv =
    (FAR struct bl616cl_rtc_lowerhalf_s *)lower;
  irqstate_t flags;
  bool time_set;

  flags = spin_lock_irqsave(&priv->lock);
  time_set = priv->time_set;
  spin_unlock_irqrestore(&priv->lock, flags);
  return time_set;
}

#ifdef CONFIG_BL616CL_RTC_ALARM
static int bl616cl_rtc_setalarm(FAR struct rtc_lowerhalf_s *lower,
                                FAR const struct lower_setalarm_s *alarminfo)
{
  FAR struct bl616cl_rtc_lowerhalf_s *priv =
    (FAR struct bl616cl_rtc_lowerhalf_s *)lower;
  time_t epoch;
  uint32_t nanosecond;
  irqstate_t flags;
  int ret;

  if (priv == NULL || alarminfo == NULL || alarminfo->id != 0 ||
      alarminfo->cb == NULL)
    {
      return -EINVAL;
    }

  ret = bl616cl_rtc_time_to_epoch(&alarminfo->time, &epoch, &nanosecond);
  if (ret < 0)
    {
      return ret;
    }

  flags = spin_lock_irqsave(&priv->lock);
  ret = bl616cl_rtc_program_alarm_locked(priv, epoch, nanosecond,
                                         alarminfo->cb, alarminfo->priv,
                                         &alarminfo->time);
  spin_unlock_irqrestore(&priv->lock, flags);

  return ret;
}

static int bl616cl_rtc_setrelative(
  FAR struct rtc_lowerhalf_s *lower,
  FAR const struct lower_setrelative_s *alarminfo)
{
  FAR struct bl616cl_rtc_lowerhalf_s *priv =
    (FAR struct bl616cl_rtc_lowerhalf_s *)lower;
  struct rtc_time alarm_time;
  struct tm converted;
  uint64_t counter;
  time_t current_epoch;
  time_t target_epoch;
  uint32_t nanosecond;
  irqstate_t flags;
  int ret;

  if (priv == NULL || alarminfo == NULL || alarminfo->id != 0 ||
      alarminfo->cb == NULL || alarminfo->reltime <= 0)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&priv->lock);
  ret = bl616cl_rtc_snapshot_locked(priv, &current_epoch, &nanosecond,
                                    &counter);
  if (ret == OK &&
      (uint64_t)alarminfo->reltime >
        (uint64_t)BL616CL_RTC_TIME_MAX - (uint64_t)current_epoch)
    {
      ret = -ERANGE;
    }

  if (ret == OK)
    {
      target_epoch = current_epoch + alarminfo->reltime;
      if (gmtime_r(&target_epoch, &converted) == NULL)
        {
          ret = -ERANGE;
        }
    }

  if (ret == OK)
    {
      memset(&alarm_time, 0, sizeof(alarm_time));
      memcpy(&alarm_time, &converted, sizeof(converted));
#ifdef CONFIG_ARCH_HAVE_RTC_SUBSECONDS
      alarm_time.tm_nsec = nanosecond;
#endif
      ret = bl616cl_rtc_program_alarm_locked(priv, target_epoch, nanosecond,
                                             alarminfo->cb, alarminfo->priv,
                                             &alarm_time);
    }

  spin_unlock_irqrestore(&priv->lock, flags);

  return ret;
}

static int bl616cl_rtc_cancelalarm(FAR struct rtc_lowerhalf_s *lower,
                                   int alarmid)
{
  FAR struct bl616cl_rtc_lowerhalf_s *priv =
    (FAR struct bl616cl_rtc_lowerhalf_s *)lower;
  irqstate_t flags;

  if (priv == NULL || alarmid != 0)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&priv->lock);
  priv->alarm_callback = NULL;
  priv->alarm_arg = NULL;
  priv->alarm_active = false;
  priv->alarm_wait_for_clock = false;
  memset(&priv->alarm_time, 0, sizeof(priv->alarm_time));
  bl616cl_rtc_clear_alarm_hardware();
  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}

static int bl616cl_rtc_rdalarm(FAR struct rtc_lowerhalf_s *lower,
                               FAR struct lower_rdalarm_s *alarminfo)
{
  FAR struct bl616cl_rtc_lowerhalf_s *priv =
    (FAR struct bl616cl_rtc_lowerhalf_s *)lower;
  irqstate_t flags;

  if (priv == NULL || alarminfo == NULL || alarminfo->time == NULL ||
      alarminfo->id != 0)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&priv->lock);
  *alarminfo->time = priv->alarm_time;
  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

uint64_t bl616cl_rtc_counter(void)
{
  uint32_t high;
  uint32_t low;

  bl616cl_rtc_hw_counter(&low, &high);
  return (((uint64_t)high << 32) | low) & BL616CL_RTC_COUNTER_MASK;
}

int up_rtc_initialize(void)
{
  FAR struct bl616cl_rtc_lowerhalf_s *priv = &g_bl616cl_rtc;
  struct tm converted;
  struct tm start_time;
  time_t start_epoch;
  irqstate_t flags;
#ifdef CONFIG_BL616CL_RTC_ALARM
  int ret;
#endif

  bl616cl_rtc_hw_initialize();
#ifdef CONFIG_BL616CL_RTC_ALARM
  bl616cl_rtc_clear_alarm_hardware();
#endif

  memset(&start_time, 0, sizeof(start_time));
  start_time.tm_year = CONFIG_START_YEAR - TM_YEAR_BASE;
  start_time.tm_mon = CONFIG_START_MONTH - 1;
  start_time.tm_mday = CONFIG_START_DAY;

  start_epoch = timegm(&start_time);
  if (start_epoch < 0 || gmtime_r(&start_epoch, &converted) == NULL ||
      converted.tm_year != start_time.tm_year ||
      converted.tm_mon != start_time.tm_mon ||
      converted.tm_mday != start_time.tm_mday)
    {
      rtcerr("ERROR: Invalid RTC start date: %04d-%02d-%02d\n",
             CONFIG_START_YEAR, CONFIG_START_MONTH, CONFIG_START_DAY);
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&priv->lock);
  priv->initialized = false;
  priv->epoch_base = start_epoch;
  priv->counter_base = bl616cl_rtc_counter();
  priv->nanosecond_base = 0;
  priv->time_set = false;
#ifdef CONFIG_BL616CL_RTC_ALARM
  priv->alarm_callback = NULL;
  priv->alarm_arg = NULL;
  priv->alarm_active = false;
  priv->alarm_irq_ready = false;
  priv->alarm_wait_for_clock = false;
  memset(&priv->alarm_time, 0, sizeof(priv->alarm_time));
#endif
  spin_unlock_irqrestore(&priv->lock, flags);

#ifdef CONFIG_BL616CL_RTC_ALARM
  ret = irq_attach(BL616CL_IRQ_HBN_OUT0, bl616cl_rtc_interrupt, priv);
  if (ret < 0)
    {
      rtcerr("ERROR: Failed to attach HBN_OUT0 RTC IRQ: %d\n", ret);
      return ret;
    }

  flags = spin_lock_irqsave(&priv->lock);
  priv->alarm_irq_ready = true;
  spin_unlock_irqrestore(&priv->lock, flags);
  up_enable_irq(BL616CL_IRQ_HBN_OUT0);
#endif

  up_rtc_set_lowerhalf((FAR struct rtc_lowerhalf_s *)priv, false);

  flags = spin_lock_irqsave(&priv->lock);
  priv->initialized = true;
  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}

int bl616cl_rtc_register(int minor)
{
  irqstate_t flags;
  bool initialized;

  flags = spin_lock_irqsave(&g_bl616cl_rtc.lock);
  initialized = g_bl616cl_rtc.initialized;
  spin_unlock_irqrestore(&g_bl616cl_rtc.lock, flags);
  if (!initialized)
    {
      return -ENODEV;
    }

  return rtc_initialize(minor,
                        (FAR struct rtc_lowerhalf_s *)&g_bl616cl_rtc);
}

uint32_t bl616cl_rtc_clock_numerator(void)
{
  return BL616CL_RTC_CLOCK_NUMERATOR;
}

uint32_t bl616cl_rtc_clock_denominator(void)
{
  return BL616CL_RTC_CLOCK_DENOMINATOR;
}
