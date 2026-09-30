/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_rtc.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
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
#define BL616CL_RTC_CLOCK_DENOMINATOR ((uint64_t)BL616CL_RTC_DIG32K_DIV)
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
  const struct rtc_ops_s *ops;
  spinlock_t lock;
  time_t epoch_base;
  uint64_t counter_base;
  uint32_t nanosecond_base;
  bool initialized;
  bool time_set;

#ifdef CONFIG_BL616CL_RTC_ALARM
  rtc_alarm_callback_t alarm_callback;
  void *alarm_arg;
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

static int bl616cl_rtc_rdtime(struct rtc_lowerhalf_s *lower,
                              struct rtc_time *rtctime);
static int bl616cl_rtc_settime(struct rtc_lowerhalf_s *lower,
                               const struct rtc_time *rtctime);
static bool bl616cl_rtc_havesettime(struct rtc_lowerhalf_s *lower);

#ifdef CONFIG_BL616CL_RTC_ALARM
static int bl616cl_rtc_setalarm(struct rtc_lowerhalf_s *lower,
                                const struct lower_setalarm_s
                                  *alarminfo);
static int bl616cl_rtc_setrelative(
  struct rtc_lowerhalf_s *lower,
  const struct lower_setrelative_s *alarminfo);
static int bl616cl_rtc_cancelalarm(struct rtc_lowerhalf_s *lower,
                                   int alarmid);
static int bl616cl_rtc_rdalarm(struct rtc_lowerhalf_s *lower,
                               struct lower_rdalarm_s *alarminfo);
#endif

#ifndef CONFIG_DISABLE_PSEUDOFS_OPERATIONS
static int bl616cl_rtc_destroy(struct rtc_lowerhalf_s *lower);
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

/****************************************************************************
 * Name: bl616cl_rtc_ticks_to_seconds
 *
 * Description:
 *   Convert RTC counter ticks to whole seconds and nanoseconds using the RTC
 *   clock numerator and denominator.
 *
 * Input Parameters:
 *   ticks - RTC counter ticks.
 *   nanoseconds - Location to return the sub-second part in nanoseconds.
 *
 * Returned Value:
 *   The number of whole seconds.
 *
 ****************************************************************************/

static uint64_t bl616cl_rtc_ticks_to_seconds(uint64_t ticks,
                                             uint32_t *nanoseconds)
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
/****************************************************************************
 * Name: bl616cl_rtc_duration_to_ticks
 *
 * Description:
 *   Convert a duration to RTC counter ticks, rounding any fraction up.
 *   Available only with alarm support.
 *
 * Input Parameters:
 *   seconds - Whole seconds of the duration.
 *   nanoseconds - Sub-second part in nanoseconds.
 *   ticks - Location to return the number of ticks.
 *
 * Returned Value:
 *   Zero (OK) on success; -ERANGE if the duration does not fit in the 48-bit
 *   RTC counter.
 *
 ****************************************************************************/

static int bl616cl_rtc_duration_to_ticks(uint64_t seconds,
                                         uint32_t nanoseconds,
                                         uint64_t *ticks)
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

/****************************************************************************
 * Name: bl616cl_rtc_time_to_epoch
 *
 * Description:
 *   Validate a broken-down time and convert it to seconds since the Epoch. A
 *   date that does not exist (for example February 30) or is before 1970 is
 *   rejected. The nanosecond part comes from tm_nsec when
 *   CONFIG_ARCH_HAVE_RTC_SUBSECONDS is set and is zero otherwise.
 *
 * Input Parameters:
 *   rtctime - Broken-down time to convert.
 *   epoch - Location to return the seconds since the Epoch.
 *   nanosecond - Location to return the nanoseconds.
 *
 * Returned Value:
 *   Zero (OK) on success; -EINVAL if an argument is NULL or the time is not
 *   valid.
 *
 ****************************************************************************/

static int bl616cl_rtc_time_to_epoch(const struct rtc_time *rtctime,
                                     time_t *epoch,
                                     uint32_t *nanosecond)
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

/****************************************************************************
 * Name: bl616cl_rtc_snapshot_locked
 *
 * Description:
 *   Read the RTC counter and compute the current time as the base time plus
 *   the ticks elapsed since the base counter. The caller must hold the RTC
 *   lock.
 *
 * Input Parameters:
 *   priv - RTC state.
 *   epoch - Location to return the seconds since the Epoch.
 *   nanosecond - Location to return the nanoseconds.
 *   counter - Location to return the RTC counter value that was read.
 *
 * Returned Value:
 *   Zero (OK) on success; -ERANGE if the time exceeds the range of time_t.
 *
 ****************************************************************************/

static int bl616cl_rtc_snapshot_locked(
  struct bl616cl_rtc_lowerhalf_s *priv,
  time_t *epoch, uint32_t *nanosecond, uint64_t *counter)
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

/****************************************************************************
 * Name: bl616cl_rtc_snapshot
 *
 * Description:
 *   Take the RTC lock and get a time snapshot with
 *   bl616cl_rtc_snapshot_locked().
 *
 * Input Parameters:
 *   priv - RTC state.
 *   epoch - Location to return the seconds since the Epoch.
 *   nanosecond - Location to return the nanoseconds.
 *   counter - Location to return the RTC counter value that was read.
 *
 * Returned Value:
 *   The result of bl616cl_rtc_snapshot_locked().
 *
 ****************************************************************************/

static int bl616cl_rtc_snapshot(
  struct bl616cl_rtc_lowerhalf_s *priv,
  time_t *epoch, uint32_t *nanosecond, uint64_t *counter)
{
  irqstate_t flags;
  int ret;

  flags = spin_lock_irqsave(&priv->lock);
  ret = bl616cl_rtc_snapshot_locked(priv, epoch, nanosecond, counter);
  spin_unlock_irqrestore(&priv->lock, flags);
  return ret;
}

#ifdef CONFIG_BL616CL_RTC_ALARM
/****************************************************************************
 * Name: bl616cl_rtc_clear_alarm_hardware
 *
 * Description:
 *   Clear the RTC alarm interrupt in hardware. Available only with alarm
 *   support.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_rtc_clear_alarm_hardware(void)
{
  bl616cl_rtc_hw_clear_alarm();
}

/****************************************************************************
 * Name: bl616cl_rtc_program_counter
 *
 * Description:
 *   Program the hardware alarm comparator for a target counter value. The
 *   target is moved to the current counter plus BL616CL_RTC_PROGRAM_GUARD if
 *   it is expired, too close or beyond the requested delta. After the write
 *   the remaining time is checked and the programming is retried up to 3
 *   times with a growing window; the last target is written even if it still
 *   cannot be verified.
 *
 * Input Parameters:
 *   target_counter - Target RTC counter value.
 *   requested_delta - Ticks from now to the target, used to check the target.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: bl616cl_rtc_alarm_delta
 *
 * Description:
 *   Compute the number of RTC ticks between the current time and a target
 *   time.
 *
 * Input Parameters:
 *   current_epoch - Current time in seconds since the Epoch.
 *   current_nanosecond - Nanoseconds of the current time.
 *   target_epoch - Target time in seconds since the Epoch.
 *   target_nanosecond - Nanoseconds of the target time.
 *   delta_ticks - Location to return the number of ticks.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -ETIME  - The target is not after the current time, or is less than
 *               BL616CL_RTC_MIN_ALARM_TICKS ticks away.
 *     -ERANGE - The interval does not fit in the RTC counter.
 *
 ****************************************************************************/

static int bl616cl_rtc_alarm_delta(time_t current_epoch,
                                   uint32_t current_nanosecond,
                                   time_t target_epoch,
                                   uint32_t target_nanosecond,
                                   uint64_t *delta_ticks)
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

/****************************************************************************
 * Name: bl616cl_rtc_program_alarm_locked
 *
 * Description:
 *   Store the alarm state and program the hardware alarm for a target time.
 *   The caller must hold the RTC lock.
 *
 * Input Parameters:
 *   priv - RTC state.
 *   target_epoch - Alarm time in seconds since the Epoch.
 *   target_nanosecond - Nanoseconds of the alarm time.
 *   callback - Function called when the alarm expires.
 *   arg - Argument passed to the callback.
 *   alarm_time - Broken-down alarm time saved for rdalarm.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -ENODEV - The alarm interrupt is not ready.
 *     -ETIME  - The alarm time is not far enough in the future.
 *     -ERANGE - The time or interval is out of range.
 *
 ****************************************************************************/

static int bl616cl_rtc_program_alarm_locked(
  struct bl616cl_rtc_lowerhalf_s *priv, time_t target_epoch,
  uint32_t target_nanosecond, rtc_alarm_callback_t callback, void *arg,
  const struct rtc_time *alarm_time)
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

/****************************************************************************
 * Name: bl616cl_rtc_interrupt
 *
 * Description:
 *   RTC alarm interrupt handler (HBN_OUT0). Ignore the interrupt if no alarm
 *   is pending; otherwise clear it. If the alarm was set while waiting for
 *   CLOCK_REALTIME to be synchronized and the system time has not reached the
 *   alarm time, re-arm the alarm. Else clear the alarm state and call the
 *   callback outside the lock.
 *
 * Input Parameters:
 *   irq - IRQ number (unused).
 *   context - Interrupt register state save area (unused).
 *   arg - The bl616cl_rtc_lowerhalf_s instance.
 *
 * Returned Value:
 *   Always OK.
 *
 ****************************************************************************/

static int bl616cl_rtc_interrupt(int irq, void *context, void *arg)
{
  struct bl616cl_rtc_lowerhalf_s *priv = arg;
  rtc_alarm_callback_t callback = NULL;
  void *callback_arg = NULL;
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

/****************************************************************************
 * Name: bl616cl_rtc_rdtime
 *
 * Description:
 *   Implement the rtc_ops_s rdtime method. Return the current time as a
 *   broken-down time, including nanoseconds when
 *   CONFIG_ARCH_HAVE_RTC_SUBSECONDS is set.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the RTC lower
 *           half.
 *   rtctime - Location to return the current time.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -EINVAL - An argument is NULL.
 *     -ERANGE - The time is out of range.
 *
 ****************************************************************************/

static int bl616cl_rtc_rdtime(struct rtc_lowerhalf_s *lower,
                              struct rtc_time *rtctime)
{
  struct bl616cl_rtc_lowerhalf_s *priv =
    (struct bl616cl_rtc_lowerhalf_s *)lower;
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

/****************************************************************************
 * Name: bl616cl_rtc_settime
 *
 * Description:
 *   Implement the rtc_ops_s settime method. Set a new base time from the
 *   current counter value. An active alarm is re-programmed for the new time;
 *   if the alarm time has already passed, the callback is deferred until
 *   CLOCK_REALTIME has been synchronized.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the RTC lower
 *           half.
 *   rtctime - The new time to set.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -EINVAL - The lower half or time is NULL or the time is not valid.
 *     -ERANGE - The active alarm interval is out of range.
 *
 ****************************************************************************/

static int bl616cl_rtc_settime(struct rtc_lowerhalf_s *lower,
                               const struct rtc_time *rtctime)
{
  struct bl616cl_rtc_lowerhalf_s *priv =
    (struct bl616cl_rtc_lowerhalf_s *)lower;
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
/****************************************************************************
 * Name: bl616cl_rtc_destroy
 *
 * Description:
 *   Implement the rtc_ops_s destroy method. Mark the RTC uninitialized, clear
 *   the alarm state and, with alarm support, disable and detach the RTC alarm
 *   interrupt.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the RTC lower
 *           half.
 *
 * Returned Value:
 *   Zero (OK) on success; -EINVAL if lower is NULL.
 *
 ****************************************************************************/

static int bl616cl_rtc_destroy(struct rtc_lowerhalf_s *lower)
{
  struct bl616cl_rtc_lowerhalf_s *priv =
    (struct bl616cl_rtc_lowerhalf_s *)lower;
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
  up_disable_irq(BL616CL_IRQ_NUM_HBN_OUT0);
  irq_detach(BL616CL_IRQ_NUM_HBN_OUT0);
#endif
  return OK;
}
#endif

/****************************************************************************
 * Name: bl616cl_rtc_havesettime
 *
 * Description:
 *   Implement the rtc_ops_s havesettime method.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the RTC lower
 *           half.
 *
 * Returned Value:
 *   true if the time has been set since initialization; false otherwise.
 *
 ****************************************************************************/

static bool bl616cl_rtc_havesettime(struct rtc_lowerhalf_s *lower)
{
  struct bl616cl_rtc_lowerhalf_s *priv =
    (struct bl616cl_rtc_lowerhalf_s *)lower;
  irqstate_t flags;
  bool time_set;

  flags = spin_lock_irqsave(&priv->lock);
  time_set = priv->time_set;
  spin_unlock_irqrestore(&priv->lock, flags);
  return time_set;
}

#ifdef CONFIG_BL616CL_RTC_ALARM
/****************************************************************************
 * Name: bl616cl_rtc_setalarm
 *
 * Description:
 *   Implement the rtc_ops_s setalarm method. Only alarm 0 is supported. The
 *   alarm time is converted to seconds since the Epoch and programmed.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the RTC lower
 *           half.
 *   alarminfo - Alarm ID, callback, callback argument and alarm time.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -EINVAL - An argument is NULL, the alarm ID is not 0, the callback is
 *               NULL or the time is not valid.
 *     Other errors are returned from bl616cl_rtc_program_alarm_locked().
 *
 ****************************************************************************/

static int bl616cl_rtc_setalarm(struct rtc_lowerhalf_s *lower,
                                const struct lower_setalarm_s *alarminfo)
{
  struct bl616cl_rtc_lowerhalf_s *priv =
    (struct bl616cl_rtc_lowerhalf_s *)lower;
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

/****************************************************************************
 * Name: bl616cl_rtc_setrelative
 *
 * Description:
 *   Implement the rtc_ops_s setrelative method. Only alarm 0 is supported.
 *   The alarm is set to the current time plus the relative time.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the RTC lower
 *           half.
 *   alarminfo - Alarm ID, callback, callback argument and relative time in
 *               seconds.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -EINVAL - An argument is NULL, the alarm ID is not 0, the callback is
 *               NULL or the relative time is not positive.
 *     -ERANGE - The target time is out of range.
 *     Other errors are returned from bl616cl_rtc_program_alarm_locked().
 *
 ****************************************************************************/

static int bl616cl_rtc_setrelative(
  struct rtc_lowerhalf_s *lower,
  const struct lower_setrelative_s *alarminfo)
{
  struct bl616cl_rtc_lowerhalf_s *priv =
    (struct bl616cl_rtc_lowerhalf_s *)lower;
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

/****************************************************************************
 * Name: bl616cl_rtc_cancelalarm
 *
 * Description:
 *   Implement the rtc_ops_s cancelalarm method. Only alarm 0 is supported.
 *   Clear the alarm state and the alarm hardware.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the RTC lower
 *           half.
 *   alarmid - Alarm ID; must be 0.
 *
 * Returned Value:
 *   Zero (OK) on success; -EINVAL if lower is NULL or the alarm ID is not 0.
 *
 ****************************************************************************/

static int bl616cl_rtc_cancelalarm(struct rtc_lowerhalf_s *lower,
                                   int alarmid)
{
  struct bl616cl_rtc_lowerhalf_s *priv =
    (struct bl616cl_rtc_lowerhalf_s *)lower;
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

/****************************************************************************
 * Name: bl616cl_rtc_rdalarm
 *
 * Description:
 *   Implement the rtc_ops_s rdalarm method. Only alarm 0 is supported. Return
 *   the saved alarm time.
 *
 * Input Parameters:
 *   lower - A pointer to the publicly visible representation of the RTC lower
 *           half.
 *   alarminfo - Alarm ID and location to return the alarm time.
 *
 * Returned Value:
 *   Zero (OK) on success; -EINVAL if an argument is NULL or the alarm ID is
 *   not 0.
 *
 ****************************************************************************/

static int bl616cl_rtc_rdalarm(struct rtc_lowerhalf_s *lower,
                               struct lower_rdalarm_s *alarminfo)
{
  struct bl616cl_rtc_lowerhalf_s *priv =
    (struct bl616cl_rtc_lowerhalf_s *)lower;
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

/****************************************************************************
 * Name: bl616cl_rtc_counter
 *
 * Description:
 *   Read the 48-bit RTC hardware counter.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The RTC counter value.
 *
 ****************************************************************************/

uint64_t bl616cl_rtc_counter(void)
{
  uint32_t high;
  uint32_t low;

  bl616cl_rtc_hw_counter(&low, &high);
  return (((uint64_t)high << 32) | low) & BL616CL_RTC_COUNTER_MASK;
}

/****************************************************************************
 * Name: up_rtc_initialize
 *
 * Description:
 *   Implement the NuttX up_rtc_initialize() interface. Initialize the RTC
 *   hardware, set the base time to CONFIG_START_YEAR, CONFIG_START_MONTH and
 *   CONFIG_START_DAY, attach and enable the HBN_OUT0 alarm interrupt when
 *   alarm support is enabled, and register the lower half with
 *   up_rtc_set_lowerhalf().
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -EINVAL - The configured start date is not valid.
 *     Other errors are returned from irq_attach().
 *
 ****************************************************************************/

int up_rtc_initialize(void)
{
  struct bl616cl_rtc_lowerhalf_s *priv = &g_bl616cl_rtc;
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
  ret = irq_attach(BL616CL_IRQ_NUM_HBN_OUT0, bl616cl_rtc_interrupt, priv);
  if (ret < 0)
    {
      rtcerr("ERROR: Failed to attach HBN_OUT0 RTC IRQ: %d\n", ret);
      return ret;
    }

  flags = spin_lock_irqsave(&priv->lock);
  priv->alarm_irq_ready = true;
  spin_unlock_irqrestore(&priv->lock, flags);
  up_enable_irq(BL616CL_IRQ_NUM_HBN_OUT0);
#endif

  up_rtc_set_lowerhalf((struct rtc_lowerhalf_s *)priv, false);

  flags = spin_lock_irqsave(&priv->lock);
  priv->initialized = true;
  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_rtc_register
 *
 * Description:
 *   Register the RTC as an RTC character device with rtc_initialize().
 *
 * Input Parameters:
 *   minor - Device minor number of the RTC device.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -ENODEV - up_rtc_initialize() has not completed.
 *     Other errors are returned from rtc_initialize().
 *
 ****************************************************************************/

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
                        (struct rtc_lowerhalf_s *)&g_bl616cl_rtc);
}

/****************************************************************************
 * Name: bl616cl_rtc_clock_numerator
 *
 * Description:
 *   Get the numerator of the RTC clock frequency ratio; the RTC counter runs
 *   at numerator / denominator Hz.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The numerator of the RTC clock frequency.
 *
 ****************************************************************************/

uint32_t bl616cl_rtc_clock_numerator(void)
{
  return BL616CL_RTC_CLOCK_NUMERATOR;
}

/****************************************************************************
 * Name: bl616cl_rtc_clock_denominator
 *
 * Description:
 *   Get the denominator of the RTC clock frequency ratio; the RTC counter
 *   runs at numerator / denominator Hz.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The denominator of the RTC clock frequency.
 *
 ****************************************************************************/

uint32_t bl616cl_rtc_clock_denominator(void)
{
  return BL616CL_RTC_CLOCK_DENOMINATOR;
}
