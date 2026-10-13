/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_pwm.c
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
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/timers/pwm.h>

#include "bl616cl_lhal.h"
#include "bflb_clock.h"
#include "bflb_gpio.h"
#include "bflb_name.h"
#include "bflb_peri.h"
#include "bflb_pwm_v2.h"
#include "hardware/pwm_v2_reg.h"
#include "bl616cl_pwm.h"
#ifdef CONFIG_BL616CL_PWM_TEST
#include <arch/chip/bl616cl_pwm.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_PWM_NCHANNELS   4
#define BL616CL_PWM_NPINS       37 /* GPIO0..GPIO36 */
#define BL616CL_PWM_MAX_DIVIDER UINT16_MAX
#define BL616CL_PWM_MIN_PERIOD  2
#define BL616CL_PWM_MAX_PERIOD  UINT16_MAX

/* PWM_IO_SEL_DIFF_END maps GPIO n to channel (n % 8) / 2, with the positive
 * output on an even n % 8 and the negative output on the odd one. The reset
 * layout puts only positive outputs on n % 4.
 */

#define BL616CL_PWM_POS_SLOT(ch) ((ch) * 2)
#define BL616CL_PWM_NEG_SLOT(ch) ((ch) * 2 + 1)

/* Per-channel bits in PWM_MC0_CONFIG1 and the threshold registers */

#define BL616CL_PWM_PEN(ch)  (PWM_CH0_PEN << ((ch) * 4))
#define BL616CL_PWM_PSI(ch)  (PWM_CH0_PSI << ((ch) * 4))
#define BL616CL_PWM_NEN(ch)  (PWM_CH0_NEN << ((ch) * 4))
#define BL616CL_PWM_NSI(ch)  (PWM_CH0_NSI << ((ch) * 4))
#define BL616CL_PWM_PPL(ch)  (PWM_CH0_PPL << ((ch) * 2))
#define BL616CL_PWM_NPL(ch)  (PWM_CH0_NPL << ((ch) * 2))
#define BL616CL_PWM_THRE(ch) (PWM_MC0_CH0_THRE_OFFSET + (ch) * 4)

/* Stopped pins are plain GPIO outputs. The input buffer stays enabled, as for
 * the GPIO driver's output pins, so the pin level can be read back.
 */

#define BL616CL_PWM_GPIO_OUTPUT                                    \
  (GPIO_FUNC_GPIO | GPIO_INPUT | GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | \
   GPIO_DRV_1)
#define BL616CL_PWM_GPIO_ALT \
  (GPIO_FUNC_PWM0 | GPIO_ALTERNATE | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_1)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl616cl_pwm_solution_s
{
  uint16_t divider;
  uint16_t period;
  uint32_t actual;
};

enum bl616cl_pwm_operation_e
{
  BL616CL_PWM_OPERATION_NONE = 0,
  BL616CL_PWM_OPERATION_INIT,
  BL616CL_PWM_OPERATION_START,
  BL616CL_PWM_OPERATION_STOP,
  BL616CL_PWM_OPERATION_DEINIT,
};

/* Requested output of one channel. cpol and dcpol describe the positive
 * output; the negative output is always its complement.
 */

struct bl616cl_pwm_setting_s
{
  ub16_t duty;
  uint8_t cpol;
  uint8_t dcpol;
  bool active;
};

struct bl616cl_pwm_chan_s
{
  struct bl616cl_pwm_setting_s set;
  uint16_t threshold_low;
  uint16_t threshold_high;
  int8_t pos_pin;
  int8_t neg_pin;
  bool configured;
  bool enabled;
  bool routed;   /* Pins carry the PWM output, or have never been driven */
};

struct bl616cl_pwm_lowerhalf_s
{
  const struct pwm_ops_s *ops;
  struct bflb_device_s *dev;
  struct bflb_device_s *gpio;
  uint32_t source_frequency;
  uint32_t actual_frequency;
  uint32_t solved_source;
  uint32_t solved_frequency;
  struct bl616cl_pwm_solution_s solved;
  struct bl616cl_pwm_chan_s chan[BL616CL_PWM_NCHANNELS];
  uint16_t divider;
  uint16_t period;
  bool initialized;
  bool pin_acquired;
  bool clock_enabled;
  bool started;
#ifdef CONFIG_BL616CL_PWM_TEST
  enum bl616cl_pwm_test_fault_e fault;
  uint32_t setup_calls;
  uint32_t start_calls;
  uint32_t stop_calls;
  uint32_t shutdown_calls;
  uint32_t solve_calls;
  uint32_t error_count;
  int last_error;
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bl616cl_pwm_setup(struct pwm_lowerhalf_s *lower);
static int bl616cl_pwm_shutdown(struct pwm_lowerhalf_s *lower);
static int bl616cl_pwm_start(struct pwm_lowerhalf_s *lower,
                             const struct pwm_info_s *info);
static int bl616cl_pwm_stop(struct pwm_lowerhalf_s *lower);
static int bl616cl_pwm_ioctl(struct pwm_lowerhalf_s *lower, int cmd,
                             unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* clang-format off */

static const struct pwm_ops_s g_bl616cl_pwm_ops =
{
  .setup = bl616cl_pwm_setup,
  .shutdown = bl616cl_pwm_shutdown,
  .start = bl616cl_pwm_start,
  .stop = bl616cl_pwm_stop,
  .ioctl = bl616cl_pwm_ioctl,
};

static struct bl616cl_pwm_lowerhalf_s g_bl616cl_pwm =
{
  .ops = &g_bl616cl_pwm_ops,
};

/* clang-format on */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_pwm_record_error
 *
 * Description:
 *   Remember the last result and count failures for the test diagnostics.
 *   Does nothing when CONFIG_BL616CL_PWM_TEST is disabled.
 *
 * Input Parameters:
 *   priv - PWM lower-half state
 *   error - Result to record (OK or a negated errno value)
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_pwm_record_error(
  struct bl616cl_pwm_lowerhalf_s *priv, int error)
{
#ifdef CONFIG_BL616CL_PWM_TEST
  priv->last_error = error;
  if (error < 0)
    {
      priv->error_count++;
    }
#else
  UNUSED(priv);
  UNUSED(error);
#endif
}

/****************************************************************************
 * Name: bl616cl_pwm_solve
 *
 * Description:
 *   Pick the smallest divider that keeps the period within 16 bits, so the
 *   period and the duty resolution are as large as possible, and round the
 *   period to the requested frequency.
 *
 * Input Parameters:
 *   source    - Source clock frequency in Hz
 *   frequency - Requested PWM frequency in Hz
 *   solution  - Location that receives the divider, period and actual
 *               frequency
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. Errors: -ERANGE
 *   if source is 0, solution is NULL, or the frequency is too high or too
 *   low for the 16-bit divider and period; -EINVAL if frequency is 0.
 *
 ****************************************************************************/

static int bl616cl_pwm_solve(uint32_t source, uint32_t frequency,
                             struct bl616cl_pwm_solution_s *solution)
{
  uint64_t max_count;
  uint64_t scaled;
  uint32_t divider;
  uint32_t period;

  if (solution == NULL || source == 0)
    {
      return -ERANGE;
    }

  if (frequency == 0)
    {
      return -EINVAL;
    }

  if (frequency > source / BL616CL_PWM_MIN_PERIOD)
    {
      return -ERANGE;
    }

  /* With the smallest divider the period stays at or below the maximum
   * after rounding, and above half of it once the divider exceeds 1.
   */

  max_count = (uint64_t)frequency * BL616CL_PWM_MAX_PERIOD;
  divider = (uint32_t)((source + max_count - 1) / max_count);
  if (divider > BL616CL_PWM_MAX_DIVIDER)
    {
      return -ERANGE;
    }

  scaled = (uint64_t)frequency * divider;
  period = (uint32_t)((source + scaled / 2) / scaled);

  solution->divider = divider;
  solution->period = period;
  solution->actual = source / (divider * period);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_pwm_stopped
 *
 * Description:
 *   Check whether the PWM counter has stopped by reading the STOP status bit.
 *   With the test fault injection, a matching fault forces the result.
 *
 * Input Parameters:
 *   priv - PWM lower-half state
 *   operation - Operation being checked, used for test fault injection
 *
 * Returned Value:
 *   true if the counter is stopped; false otherwise.
 *
 ****************************************************************************/

static bool bl616cl_pwm_stopped(
  struct bl616cl_pwm_lowerhalf_s *priv,
  enum bl616cl_pwm_operation_e operation)
{
#ifdef CONFIG_BL616CL_PWM_TEST
  if (operation != BL616CL_PWM_OPERATION_NONE &&
      (int)priv->fault == (int)operation)
    {
      return operation == BL616CL_PWM_OPERATION_START;
    }
#else
  UNUSED(operation);
#endif

  return (getreg32(priv->dev->reg_base + PWM_MC0_CONFIG0_OFFSET) &
          PWM_STS_STOP) != 0;
}

/****************************************************************************
 * Name: bl616cl_pwm_stop_active
 *
 * Description:
 *   Convert cpol/dcpol to the positive output stop state. The hardware stop
 *   state is relative to the output polarity, so the output idles at the
 *   dcpol level when it is "active" for a matching cpol.
 *
 * Input Parameters:
 *   set - Channel setting
 *
 * Returned Value:
 *   true for PWM_STATE_ACTIVE; false for PWM_STATE_INACTIVE.
 *
 ****************************************************************************/

static bool bl616cl_pwm_stop_active(const struct bl616cl_pwm_setting_s *set)
{
  return (set->cpol == PWM_CPOL_HIGH) == (set->dcpol == PWM_DCPOL_HIGH);
}

/****************************************************************************
 * Name: bl616cl_pwm_latch_pin
 *
 * Description:
 *   Write the GPIO output latch of a pin. The latch drives the pin only
 *   while the pin is a plain GPIO output; bflb_gpio_init() keeps it.
 *
 * Input Parameters:
 *   priv - PWM lower-half state
 *   pin  - GPIO pin number, negative when the output is not used
 *   high - Level to latch
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_pwm_latch_pin(struct bl616cl_pwm_lowerhalf_s *priv,
                                  int pin, bool high)
{
  if (pin < 0)
    {
      return;
    }

  if (high)
    {
      bflb_gpio_set(priv->gpio, pin);
    }
  else
    {
      bflb_gpio_reset(priv->gpio, pin);
    }
}

/****************************************************************************
 * Name: bl616cl_pwm_latch_idle
 *
 * Description:
 *   Latch the idle levels of a channel setting on its pins: the positive
 *   output at the dcpol level and the negative output at its complement.
 *   The output that becomes inactive is written first, so GPIO pins never
 *   pass through a state where both outputs are active.
 *
 * Input Parameters:
 *   priv - PWM lower-half state
 *   chan - Channel whose pins are latched
 *   set  - Setting that gives the idle levels
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_pwm_latch_idle(struct bl616cl_pwm_lowerhalf_s *priv,
                                   const struct bl616cl_pwm_chan_s *chan,
                                   const struct bl616cl_pwm_setting_s *set)
{
  bool high = set->dcpol == PWM_DCPOL_HIGH;
  bool neg_first = bl616cl_pwm_stop_active(set);

  if (neg_first)
    {
      bl616cl_pwm_latch_pin(priv, chan->neg_pin, !high);
    }

  bl616cl_pwm_latch_pin(priv, chan->pos_pin, high);
  if (!neg_first)
    {
      bl616cl_pwm_latch_pin(priv, chan->neg_pin, !high);
    }
}

/****************************************************************************
 * Name: bl616cl_pwm_drive_stopped_pins
 *
 * Description:
 *   Reconfigure the output pins of every routed channel as plain GPIO
 *   outputs at their idle level: the positive output at the dcpol level and
 *   the negative output at its complement. Pins that are already idle are
 *   left alone. Marks the pins as released.
 *
 * Input Parameters:
 *   priv - PWM lower-half state
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_pwm_drive_stopped_pins(
  struct bl616cl_pwm_lowerhalf_s *priv)
{
  struct bl616cl_pwm_chan_s *chan;
  int ch;

  if (priv->gpio == NULL)
    {
      return;
    }

  for (ch = 0; ch < BL616CL_PWM_NCHANNELS; ch++)
    {
      chan = &priv->chan[ch];
      if (!chan->configured || !chan->routed)
        {
          continue;
        }

      /* Latch first: the idle levels equal the stop levels the disabled
       * outputs hold, so the pins do not move when they become GPIO.
       */

      bl616cl_pwm_latch_idle(priv, chan, &chan->set);
      if (chan->pos_pin >= 0)
        {
          bflb_gpio_init(priv->gpio, chan->pos_pin, BL616CL_PWM_GPIO_OUTPUT);
        }

      if (chan->neg_pin >= 0)
        {
          bflb_gpio_init(priv->gpio, chan->neg_pin, BL616CL_PWM_GPIO_OUTPUT);
        }

      chan->routed = false;
    }

  priv->pin_acquired = false;
}

/****************************************************************************
 * Name: bl616cl_pwm_enable_clock
 *
 * Description:
 *   Enable the PWM0 peripheral clock if it is not already enabled.
 *
 * Input Parameters:
 *   priv - PWM lower-half state
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_pwm_enable_clock(
  struct bl616cl_pwm_lowerhalf_s *priv)
{
  int ret;

  if (!priv->clock_enabled)
    {
      ret = bflb_peripheral_clock_control(BFLB_PERIPHERAL_PWM0, true);
      if (ret < 0)
        {
          return ret;
        }

      priv->clock_enabled = true;
    }

  return OK;
}

/****************************************************************************
 * Name: bl616cl_pwm_acquire
 *
 * Description:
 *   Enable the PWM clock and select the per-channel pin layout. The pins are
 *   routed to the PWM function later, by bl616cl_pwm_configure().
 *
 * Input Parameters:
 *   priv - PWM lower-half state
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_pwm_acquire(struct bl616cl_pwm_lowerhalf_s *priv)
{
  int ret;

  ret = bl616cl_pwm_enable_clock(priv);
  if (ret < 0)
    {
      return ret;
    }

  ret = bflb_pwm_v2_feature_control(priv->dev, PWM_CMD_IO_SEL,
                                    PWM_IO_SEL_DIFF_END);
  if (ret < 0)
    {
      return ret;
    }

  priv->pin_acquired = true;
  return OK;
}

/****************************************************************************
 * Name: bl616cl_pwm_release
 *
 * Description:
 *   Return the output pins to their idle levels, gate the PWM clock and
 *   clear the channel, active and started flags.
 *
 * Input Parameters:
 *   priv - PWM lower-half state
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_pwm_release(struct bl616cl_pwm_lowerhalf_s *priv)
{
  int ch;

  bl616cl_pwm_drive_stopped_pins(priv);
  if (priv->clock_enabled)
    {
      bflb_peripheral_clock_control(BFLB_PERIPHERAL_PWM0, false);
      priv->clock_enabled = false;
    }

  for (ch = 0; ch < BL616CL_PWM_NCHANNELS; ch++)
    {
      priv->chan[ch].enabled = false;
      priv->chan[ch].set.active = false;
    }

  priv->started = false;
}

/****************************************************************************
 * Name: bl616cl_pwm_force_stop
 *
 * Description:
 *   Stop all channel outputs and the counter, hand the pins over to GPIO at
 *   their idle levels, de-initialize the PWM block and gate the clock. The
 *   stop state is checked after each step.
 *
 * Input Parameters:
 *   priv - PWM lower-half state
 *   operation - Operation being performed, used for test fault injection
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -ETIMEDOUT if the
 *   counter did not report stopped.
 *
 ****************************************************************************/

static int bl616cl_pwm_force_stop(struct bl616cl_pwm_lowerhalf_s *priv,
                                  enum bl616cl_pwm_operation_e operation)
{
  uintptr_t config1;
  uint32_t enable = 0;
  bool deinit_stopped;
  bool stop_stopped;
  int ch;

  if (priv->dev != NULL && priv->clock_enabled)
    {
      /* Disable every output in one write, so all of them move to their stop
       * levels together.
       */

      for (ch = 0; ch < BL616CL_PWM_NCHANNELS; ch++)
        {
          enable |= BL616CL_PWM_PEN(ch) | BL616CL_PWM_NEN(ch);
          priv->chan[ch].enabled = false;
        }

      config1 = priv->dev->reg_base + PWM_MC0_CONFIG1_OFFSET;
      putreg32(getreg32(config1) & ~enable, config1);
      bflb_pwm_v2_stop(priv->dev);
      stop_stopped = bl616cl_pwm_stopped(priv, operation);

      /* The outputs now hold the idle levels. Hand the pins to GPIO before
       * the deinit below resets the output polarity and stop states.
       */

      bl616cl_pwm_drive_stopped_pins(priv);
      bflb_pwm_v2_deinit(priv->dev);
      deinit_stopped = bl616cl_pwm_stopped(
        priv, BL616CL_PWM_OPERATION_DEINIT);
      bl616cl_pwm_release(priv);
      if (!stop_stopped || !deinit_stopped)
        {
          bl616cl_pwm_record_error(priv, -ETIMEDOUT);
          pwmerr("ERROR: PWM stop timed out: stop=%u deinit=%u\n",
                 stop_stopped, deinit_stopped);
          return -ETIMEDOUT;
        }
    }
  else
    {
      bl616cl_pwm_release(priv);
    }

  bl616cl_pwm_record_error(priv, OK);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_pwm_validate_polarity
 *
 * Description:
 *   Check that cpol and dcpol are LOW or HIGH.
 *
 * Input Parameters:
 *   cpol  - Requested channel polarity
 *   dcpol - Requested disabled channel polarity
 *
 * Returned Value:
 *   Zero (OK) if both are valid; -EINVAL otherwise.
 *
 ****************************************************************************/

static int bl616cl_pwm_validate_polarity(uint8_t cpol, uint8_t dcpol)
{
  if ((cpol != PWM_CPOL_LOW && cpol != PWM_CPOL_HIGH) ||
      (dcpol != PWM_DCPOL_LOW && dcpol != PWM_DCPOL_HIGH))
    {
      return -EINVAL;
    }

  return OK;
}

/****************************************************************************
 * Name: bl616cl_pwm_request
 *
 * Description:
 *   Build the requested setting of every channel. Channels that the request
 *   does not mention keep their current setting, so a running channel keeps
 *   running. With CONFIG_PWM_MULTICHAN, channel numbers 1 to 4 select CH0 to
 *   CH3, 0 skips an entry and -1 ends the list; otherwise the request
 *   applies to the only configured channel.
 *
 * Input Parameters:
 *   priv - PWM lower-half state
 *   info - Requested PWM characteristics
 *   next - Location that receives the setting of every channel
 *
 * Returned Value:
 *   Zero (OK) on success; -EINVAL for a NULL request, zero frequency, an
 *   invalid cpol/dcpol, an unknown, unconfigured or repeated channel, or a
 *   request that selects no channel.
 *
 ****************************************************************************/

static int bl616cl_pwm_request(struct bl616cl_pwm_lowerhalf_s *priv,
                               const struct pwm_info_s *info,
                               struct bl616cl_pwm_setting_s *next)
{
  int count = 0;
  int ret;
  int ch;
#ifdef CONFIG_PWM_MULTICHAN
  bool requested[BL616CL_PWM_NCHANNELS];
  const struct pwm_chan_s *req;
  int i;
#endif

  if (info == NULL || info->frequency == 0)
    {
      return -EINVAL;
    }

  for (ch = 0; ch < BL616CL_PWM_NCHANNELS; ch++)
    {
      next[ch] = priv->chan[ch].set;
    }

#ifdef CONFIG_PWM_MULTICHAN
  memset(requested, 0, sizeof(requested));
  for (i = 0; i < CONFIG_PWM_NCHANNELS; i++)
    {
      req = &info->channels[i];
      if (req->channel == -1)
        {
          break;
        }

      if (req->channel == 0)
        {
          continue;
        }

      ch = req->channel - 1;
      if (ch < 0 || ch >= BL616CL_PWM_NCHANNELS ||
          !priv->chan[ch].configured || requested[ch])
        {
          return -EINVAL;
        }

      ret = bl616cl_pwm_validate_polarity(req->cpol, req->dcpol);
      if (ret < 0)
        {
          return ret;
        }

      next[ch].duty = req->duty;
      next[ch].cpol = req->cpol;
      next[ch].dcpol = req->dcpol;
      next[ch].active = true;
      requested[ch] = true;
      count++;
    }
#else
  ret = bl616cl_pwm_validate_polarity(info->cpol, info->dcpol);
  if (ret < 0)
    {
      return ret;
    }

  for (ch = 0; ch < BL616CL_PWM_NCHANNELS; ch++)
    {
      if (priv->chan[ch].configured)
        {
          next[ch].duty = info->duty;
          next[ch].cpol = info->cpol;
          next[ch].dcpol = info->dcpol;
          next[ch].active = true;
          count++;
        }
    }
#endif

  return count > 0 ? OK : -EINVAL;
}

/****************************************************************************
 * Name: bl616cl_pwm_threshold
 *
 * Description:
 *   Convert a 16-bit duty value to a compare threshold for the given period.
 *   Zero maps to 0, full scale maps to period - 1, others are rounded and
 *   clamped below the period.
 *
 * Input Parameters:
 *   duty   - Duty cycle, 0 to 65535
 *   period - Counter period
 *
 * Returned Value:
 *   The threshold value.
 *
 ****************************************************************************/

static uint16_t bl616cl_pwm_threshold(uint16_t duty, uint16_t period)
{
  uint32_t threshold;

  if (duty == 0)
    {
      return 0;
    }

  if (duty == UINT16_MAX)
    {
      return period - 1;
    }

  threshold = ((uint32_t)duty * period + 0x8000) >> 16;
  return threshold < period ? threshold : period - 1;
}

/****************************************************************************
 * Name: bl616cl_pwm_configure
 *
 * Description:
 *   Program every channel that is about to run: polarity, stop and brake
 *   states from cpol/dcpol and the threshold from the duty. Then route the
 *   pins of the channels that are not running yet to the PWM function and
 *   enable their outputs. The negative output uses the positive polarity and
 *   the opposite stop state, so it is the complement of the positive output
 *   while running and when stopped.
 *
 * Input Parameters:
 *   priv   - PWM lower-half state
 *   next   - Requested setting of every channel
 *   period - Counter period used to scale the duty
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_pwm_configure(struct bl616cl_pwm_lowerhalf_s *priv,
                                  const struct bl616cl_pwm_setting_s *next,
                                  uint16_t period)
{
  struct bflb_pwm_v2_channel_config_s config;
  struct bl616cl_pwm_chan_s *chan;
  uintptr_t config1;
  uint32_t enable = 0;
  bool stop_active;
  int ch;

  for (ch = 0; ch < BL616CL_PWM_NCHANNELS; ch++)
    {
      chan = &priv->chan[ch];
      if (!next[ch].active)
        {
          continue;
        }

      stop_active = bl616cl_pwm_stop_active(&next[ch]);

      memset(&config, 0, sizeof(config));
      config.positive_polarity = next[ch].cpol == PWM_CPOL_HIGH ?
                                   PWM_POLARITY_ACTIVE_HIGH :
                                   PWM_POLARITY_ACTIVE_LOW;
      config.negative_polarity = config.positive_polarity;
      config.positive_stop_state = stop_active ? PWM_STATE_ACTIVE :
                                                 PWM_STATE_INACTIVE;
      config.negative_stop_state = stop_active ? PWM_STATE_INACTIVE :
                                                 PWM_STATE_ACTIVE;
      config.positive_brake_state = PWM_STATE_INACTIVE;
      config.negative_brake_state = PWM_STATE_INACTIVE;
      config.dead_time = 0;

      chan->threshold_low = 0;
      chan->threshold_high = bl616cl_pwm_threshold(next[ch].duty, period);
      bflb_pwm_v2_channel_init(priv->dev, ch, &config);
      bflb_pwm_v2_channel_set_threshold(priv->dev, ch, chan->threshold_low,
                                        chan->threshold_high);
      if (chan->enabled)
        {
          continue;
        }

      /* Move the GPIO pins to the idle levels of the new setting. These are
       * the stop levels the disabled outputs now hold, so routing the pins
       * does not move them.
       */

      bl616cl_pwm_latch_idle(priv, chan, &next[ch]);
      if (chan->pos_pin >= 0)
        {
          bflb_gpio_init(priv->gpio, chan->pos_pin, BL616CL_PWM_GPIO_ALT);
          enable |= BL616CL_PWM_PEN(ch);
        }

      if (chan->neg_pin >= 0)
        {
          bflb_gpio_init(priv->gpio, chan->neg_pin, BL616CL_PWM_GPIO_ALT);
          enable |= BL616CL_PWM_NEN(ch);
        }

      chan->routed = true;
      chan->enabled = true;
    }

  /* Enable all new outputs in one write, so the positive and negative
   * outputs never pass through a state where both are active.
   */

  if (enable != 0)
    {
      config1 = priv->dev->reg_base + PWM_MC0_CONFIG1_OFFSET;
      putreg32(getreg32(config1) | enable, config1);
    }
}

/****************************************************************************
 * Name: bl616cl_pwm_readback
 *
 * Description:
 *   Read back the PWM registers and check that they match the computed
 *   solution and the requested setting of every channel. Updates the cached
 *   divider, period and actual frequency.
 *
 * Input Parameters:
 *   priv     - PWM lower-half state
 *   next     - Requested setting of every channel
 *   solution - Divider, period and actual frequency that were programmed
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -EIO if a
 *   register value or the resulting frequency differs from the request.
 *
 ****************************************************************************/

static int bl616cl_pwm_readback(
  struct bl616cl_pwm_lowerhalf_s *priv,
  const struct bl616cl_pwm_setting_s *next,
  const struct bl616cl_pwm_solution_s *solution)
{
  const struct bl616cl_pwm_chan_s *chan;
  uint64_t denominator;
  uint32_t config0;
  uint32_t config1;
  uint32_t period_reg;
  uint32_t threshold;
  uint16_t divider;
  uint16_t period;
  bool stop_active;
  bool high;
  int ch;

  config0 = getreg32(priv->dev->reg_base + PWM_MC0_CONFIG0_OFFSET);
  config1 = getreg32(priv->dev->reg_base + PWM_MC0_CONFIG1_OFFSET);
  period_reg = getreg32(priv->dev->reg_base + PWM_MC0_PERIOD_OFFSET);
  divider = (config0 & PWM_CLK_DIV_MASK) >> PWM_CLK_DIV_SHIFT;
  period = (period_reg & PWM_PERIOD_MASK) >> PWM_PERIOD_SHIFT;

  if (divider != solution->divider || period != solution->period ||
      ((config0 & PWM_REG_CLK_SEL_MASK) >> PWM_REG_CLK_SEL_SHIFT) != 1 ||
      (config0 & PWM_CENTER_ALIGNED_EN) != 0)
    {
      return -EIO;
    }

  for (ch = 0; ch < BL616CL_PWM_NCHANNELS; ch++)
    {
      chan = &priv->chan[ch];
      if (((config1 & BL616CL_PWM_PEN(ch)) != 0) !=
            (next[ch].active && chan->pos_pin >= 0) ||
          ((config1 & BL616CL_PWM_NEN(ch)) != 0) !=
            (next[ch].active && chan->neg_pin >= 0))
        {
          return -EIO;
        }

      if (!next[ch].active)
        {
          continue;
        }

      threshold = getreg32(priv->dev->reg_base + BL616CL_PWM_THRE(ch));
      stop_active = bl616cl_pwm_stop_active(&next[ch]);
      high = next[ch].cpol == PWM_CPOL_HIGH;
      if ((threshold & PWM_CH0_THREL_MASK) != chan->threshold_low ||
          ((threshold & PWM_CH0_THREH_MASK) >> PWM_CH0_THREH_SHIFT) !=
            chan->threshold_high ||
          ((config1 & BL616CL_PWM_PPL(ch)) != 0) != high ||
          ((config1 & BL616CL_PWM_NPL(ch)) != 0) != high ||
          ((config1 & BL616CL_PWM_PSI(ch)) != 0) != stop_active ||
          ((config1 & BL616CL_PWM_NSI(ch)) != 0) == stop_active)
        {
          return -EIO;
        }
    }

  denominator = (uint64_t)divider * period;
  priv->divider = divider;
  priv->period = period;
  priv->actual_frequency = priv->source_frequency / denominator;
  return priv->actual_frequency == solution->actual ? OK : -EIO;
}

/****************************************************************************
 * Name: bl616cl_pwm_setup
 *
 * Description:
 *   Implement the pwm_ops_s setup method. Enable the clock, put the block
 *   in a known stopped state and drive the configured pins to their idle
 *   levels. Does nothing if already set up.
 *
 * Input Parameters:
 *   lower - Lower-half PWM device structure
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int bl616cl_pwm_setup(struct pwm_lowerhalf_s *lower)
{
  struct bl616cl_pwm_lowerhalf_s *priv =
    (struct bl616cl_pwm_lowerhalf_s *)lower;
  int ret;

#ifdef CONFIG_BL616CL_PWM_TEST
  priv->setup_calls++;
#endif
  if (priv->initialized)
    {
      return OK;
    }

  ret = bl616cl_pwm_enable_clock(priv);
  if (ret < 0)
    {
      bl616cl_pwm_record_error(priv, ret);
      return ret;
    }

  ret = bl616cl_pwm_force_stop(priv, BL616CL_PWM_OPERATION_NONE);
  if (ret < 0)
    {
      return ret;
    }

  priv->initialized = true;
  bl616cl_pwm_record_error(priv, OK);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_pwm_shutdown
 *
 * Description:
 *   Implement the pwm_ops_s shutdown method. Force all outputs stopped and
 *   release the pins and clock.
 *
 * Input Parameters:
 *   lower - Lower-half PWM device structure
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -ETIMEDOUT if the
 *   counter did not stop.
 *
 ****************************************************************************/

static int bl616cl_pwm_shutdown(struct pwm_lowerhalf_s *lower)
{
  struct bl616cl_pwm_lowerhalf_s *priv =
    (struct bl616cl_pwm_lowerhalf_s *)lower;
  int ret;

#ifdef CONFIG_BL616CL_PWM_TEST
  priv->shutdown_calls++;
#endif
  ret = bl616cl_pwm_force_stop(priv, BL616CL_PWM_OPERATION_STOP);
  priv->initialized = false;
  return ret;
}

/****************************************************************************
 * Name: bl616cl_pwm_start
 *
 * Description:
 *   Implement the pwm_ops_s start method. Compute the divider and period
 *   (reused from the previous call for the same frequency), then either
 *   update the running period and channels in place (same divider) or
 *   re-initialize and start the block with every active channel, including
 *   running channels the request does not mention. The registers are read
 *   back to verify the result.
 *
 * Input Parameters:
 *   lower - Lower-half PWM device structure
 *   info  - Requested PWM characteristics
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. Errors: -EINVAL
 *   for invalid parameters or channels; -ERANGE if the frequency cannot be
 *   generated; -ETIMEDOUT if init or start did not take effect; -EIO on
 *   readback mismatch.
 *
 ****************************************************************************/

static int bl616cl_pwm_start(struct pwm_lowerhalf_s *lower,
                             const struct pwm_info_s *info)
{
  struct bl616cl_pwm_lowerhalf_s *priv =
    (struct bl616cl_pwm_lowerhalf_s *)lower;
  struct bl616cl_pwm_setting_s next[BL616CL_PWM_NCHANNELS];
  struct bflb_pwm_v2_config_s config;
  struct bl616cl_pwm_solution_s solution;
  int ret;
  int ch;

#ifdef CONFIG_BL616CL_PWM_TEST
  priv->start_calls++;
#endif
  ret = bl616cl_pwm_request(priv, info, next);
  if (ret < 0)
    {
      bl616cl_pwm_record_error(priv, ret);
      return ret;
    }

  priv->source_frequency =
    bflb_clk_get_system_clock(BFLB_SYSTEM_PBCLK);

  /* Reuse the last result when only the duty or polarity changes, so such
   * updates skip the 64-bit divisions.
   */

  if (priv->solved_frequency == info->frequency &&
      priv->solved_source == priv->source_frequency)
    {
      solution = priv->solved;
    }
  else
    {
#ifdef CONFIG_BL616CL_PWM_TEST
      priv->solve_calls++;
#endif
      ret = bl616cl_pwm_solve(priv->source_frequency, info->frequency,
                              &solution);
      if (ret < 0)
        {
          bl616cl_pwm_record_error(priv, ret);
          return ret;
        }

      priv->solved = solution;
      priv->solved_source = priv->source_frequency;
      priv->solved_frequency = info->frequency;
    }

  if (priv->started && priv->divider == solution.divider)
    {
      ret = bl616cl_pwm_acquire(priv);
      if (ret < 0)
        {
          bl616cl_pwm_record_error(priv, ret);
          return ret;
        }

      bflb_pwm_v2_feature_control(priv->dev,
                                  PWM_CMD_UPDATE_DISABLE, 0);
      bflb_pwm_v2_set_period(priv->dev, solution.period);
      bl616cl_pwm_configure(priv, next, solution.period);
      bflb_pwm_v2_feature_control(priv->dev,
                                  PWM_CMD_UPDATE_GENERATE, 0);
      bflb_pwm_v2_feature_control(priv->dev,
                                  PWM_CMD_UPDATE_ENABLE, 0);
    }
  else
    {
      if (priv->started)
        {
          ret = bl616cl_pwm_force_stop(
            priv, BL616CL_PWM_OPERATION_STOP);
          if (ret < 0)
            {
              return ret;
            }
        }

      ret = bl616cl_pwm_acquire(priv);
      if (ret < 0)
        {
          bl616cl_pwm_record_error(priv, ret);
          return ret;
        }

      config.counter_mode = PWM_COUNTER_MODE_UP;
      config.clk_source = BFLB_SYSTEM_PBCLK;
      config.clk_div = solution.divider;
      config.period = solution.period;
      bflb_pwm_v2_init(priv->dev, &config);
      if (!bl616cl_pwm_stopped(priv,
                               BL616CL_PWM_OPERATION_INIT))
        {
          bl616cl_pwm_force_stop(
            priv, BL616CL_PWM_OPERATION_NONE);
          bl616cl_pwm_record_error(priv, -ETIMEDOUT);
          pwmerr("ERROR: PWM initialization timed out\n");
          return -ETIMEDOUT;
        }

      bl616cl_pwm_configure(priv, next, solution.period);
      bflb_pwm_v2_start(priv->dev);
      if (bl616cl_pwm_stopped(priv,
                              BL616CL_PWM_OPERATION_START))
        {
          bl616cl_pwm_force_stop(
            priv, BL616CL_PWM_OPERATION_NONE);
          bl616cl_pwm_record_error(priv, -ETIMEDOUT);
          pwmerr("ERROR: PWM start timed out\n");
          return -ETIMEDOUT;
        }
    }

  ret = bl616cl_pwm_readback(priv, next, &solution);
  if (ret < 0)
    {
      bl616cl_pwm_force_stop(priv, BL616CL_PWM_OPERATION_NONE);
      bl616cl_pwm_record_error(priv, ret);
      pwmerr("ERROR: PWM configuration readback failed\n");
      return ret;
    }

  for (ch = 0; ch < BL616CL_PWM_NCHANNELS; ch++)
    {
      priv->chan[ch].set = next[ch];
    }

  priv->started = true;
  bl616cl_pwm_record_error(priv, OK);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_pwm_stop
 *
 * Description:
 *   Implement the pwm_ops_s stop method. Force all outputs stopped and
 *   release the pins and clock.
 *
 * Input Parameters:
 *   lower - Lower-half PWM device structure
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -ETIMEDOUT if the
 *   counter did not stop.
 *
 ****************************************************************************/

static int bl616cl_pwm_stop(struct pwm_lowerhalf_s *lower)
{
  struct bl616cl_pwm_lowerhalf_s *priv =
    (struct bl616cl_pwm_lowerhalf_s *)lower;

#ifdef CONFIG_BL616CL_PWM_TEST
  priv->stop_calls++;
#endif
  return bl616cl_pwm_force_stop(priv,
                                BL616CL_PWM_OPERATION_STOP);
}

/****************************************************************************
 * Name: bl616cl_pwm_ioctl
 *
 * Description:
 *   Implement the pwm_ops_s ioctl method. No device-specific commands are
 *   supported.
 *
 * Input Parameters:
 *   lower - Lower-half PWM device structure
 *   cmd   - The ioctl command
 *   arg   - The ioctl argument
 *
 * Returned Value:
 *   -ENOTTY always.
 *
 ****************************************************************************/

static int bl616cl_pwm_ioctl(struct pwm_lowerhalf_s *lower, int cmd,
                             unsigned long arg)
{
  UNUSED(lower);
  UNUSED(cmd);
  UNUSED(arg);
  return -ENOTTY;
}

/****************************************************************************
 * Name: bl616cl_pwm_pin_valid
 *
 * Description:
 *   Check that a board output pin is unused (negative) or a GPIO that
 *   carries the given channel output slot.
 *
 * Input Parameters:
 *   pin  - GPIO pin number, negative when the output is not used
 *   slot - Required pin % 8
 *
 * Returned Value:
 *   true if the pin is usable for the slot; false otherwise.
 *
 ****************************************************************************/

static bool bl616cl_pwm_pin_valid(int pin, int slot)
{
  return pin < 0 || (pin < BL616CL_PWM_NPINS && pin % 8 == slot);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_pwm_initialize
 *
 * Description:
 *   Get the PWM0 lower half for the given channel outputs and look up the
 *   PWM and GPIO devices. Each channel may use a positive output, a
 *   negative output or both; the negative output is the complement of the
 *   positive one. Without CONFIG_PWM_MULTICHAN exactly one channel must be
 *   given.
 *
 * Input Parameters:
 *   outputs  - Channel outputs, one entry per channel
 *   noutputs - Number of entries in outputs
 *
 * Returned Value:
 *   A pointer to the PWM lower half on success; NULL if an entry is invalid
 *   (unknown or repeated channel, no pin, or a pin that does not carry that
 *   channel output), the number of entries is not supported, or a device
 *   lookup fails.
 *
 ****************************************************************************/

struct pwm_lowerhalf_s *
bl616cl_pwm_initialize(const struct bl616cl_pwm_output_s *outputs,
                       unsigned int noutputs)
{
  struct bl616cl_pwm_lowerhalf_s *priv = &g_bl616cl_pwm;
  const struct bl616cl_pwm_output_s *output;
  struct bl616cl_pwm_chan_s *chan;
  unsigned int i;

#ifdef CONFIG_PWM_MULTICHAN
  if (outputs == NULL || noutputs == 0 ||
      noutputs > BL616CL_PWM_NCHANNELS)
#else
  if (outputs == NULL || noutputs != 1)
#endif
    {
      return NULL;
    }

  memset(priv->chan, 0, sizeof(priv->chan));
  for (i = 0; i < noutputs; i++)
    {
      output = &outputs[i];
      if (output->channel >= BL616CL_PWM_NCHANNELS ||
          priv->chan[output->channel].configured ||
          (output->pos_pin < 0 && output->neg_pin < 0) ||
          !bl616cl_pwm_pin_valid(output->pos_pin,
                                 BL616CL_PWM_POS_SLOT(output->channel)) ||
          !bl616cl_pwm_pin_valid(output->neg_pin,
                                 BL616CL_PWM_NEG_SLOT(output->channel)))
        {
          memset(priv->chan, 0, sizeof(priv->chan));
          return NULL;
        }

      chan = &priv->chan[output->channel];
      chan->pos_pin = output->pos_pin < 0 ? -1 : output->pos_pin;
      chan->neg_pin = output->neg_pin < 0 ? -1 : output->neg_pin;
      chan->set.cpol = PWM_CPOL_LOW;
      chan->set.dcpol = PWM_DCPOL_LOW;
      chan->configured = true;
      chan->routed = true;
    }

  priv->dev = bflb_device_get_by_name(BFLB_NAME_PWM_V2_0);
  priv->gpio = bflb_device_get_by_name(BFLB_NAME_GPIO);
  if (priv->dev == NULL || priv->gpio == NULL)
    {
      return NULL;
    }

  return (struct pwm_lowerhalf_s *)priv;
}

#ifdef CONFIG_BL616CL_PWM_TEST
/****************************************************************************
 * Name: bl616cl_pwm_test_reset
 *
 * Description:
 *   Clear the test fault, call counters, last error and the cached
 *   divider/period solution.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void bl616cl_pwm_test_reset(void)
{
  g_bl616cl_pwm.fault = BL616CL_PWM_TEST_FAULT_NONE;
  g_bl616cl_pwm.setup_calls = 0;
  g_bl616cl_pwm.start_calls = 0;
  g_bl616cl_pwm.stop_calls = 0;
  g_bl616cl_pwm.shutdown_calls = 0;
  g_bl616cl_pwm.solve_calls = 0;
  g_bl616cl_pwm.solved_frequency = 0;
  g_bl616cl_pwm.error_count = 0;
  g_bl616cl_pwm.last_error = OK;
}

/****************************************************************************
 * Name: bl616cl_pwm_test_set_fault
 *
 * Description:
 *   Select the fault to inject into later operations.
 *
 * Input Parameters:
 *   fault - Fault to inject, BL616CL_PWM_TEST_FAULT_NONE to disable
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -EINVAL if the
 *   fault value is out of range.
 *
 ****************************************************************************/

int bl616cl_pwm_test_set_fault(enum bl616cl_pwm_test_fault_e fault)
{
  if (fault < BL616CL_PWM_TEST_FAULT_NONE ||
      fault > BL616CL_PWM_TEST_FAULT_DEINIT_TIMEOUT)
    {
      return -EINVAL;
    }

  g_bl616cl_pwm.fault = fault;
  return OK;
}

/****************************************************************************
 * Name: bl616cl_pwm_test_get_diag
 *
 * Description:
 *   Copy the call counters, last error, cached settings and the per-channel
 *   state to a diagnostics structure. Output enables, polarity and stop
 *   states are read from the registers while the clock is enabled; the pin
 *   levels are read from the GPIO input register, which follows the pin
 *   only while it is a plain GPIO output (after STOP).
 *
 * Input Parameters:
 *   diag - Location that receives the diagnostics
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -EINVAL if diag
 *   is NULL.
 *
 ****************************************************************************/

int bl616cl_pwm_test_get_diag(struct bl616cl_pwm_test_diag_s *diag)
{
  const struct bl616cl_pwm_chan_s *chan;
  struct bl616cl_pwm_test_channel_s *out;
  uint32_t config1 = 0;
  bool registers;
  int ch;

  if (diag == NULL)
    {
      return -EINVAL;
    }

  memset(diag, 0, sizeof(*diag));
  registers = g_bl616cl_pwm.dev != NULL && g_bl616cl_pwm.clock_enabled;
  if (registers)
    {
      config1 = getreg32(g_bl616cl_pwm.dev->reg_base +
                         PWM_MC0_CONFIG1_OFFSET);
    }

  diag->setup_calls = g_bl616cl_pwm.setup_calls;
  diag->start_calls = g_bl616cl_pwm.start_calls;
  diag->stop_calls = g_bl616cl_pwm.stop_calls;
  diag->shutdown_calls = g_bl616cl_pwm.shutdown_calls;
  diag->solve_calls = g_bl616cl_pwm.solve_calls;
  diag->source_frequency = g_bl616cl_pwm.source_frequency;
  diag->actual_frequency = g_bl616cl_pwm.actual_frequency;
  diag->error_count = g_bl616cl_pwm.error_count;
  diag->last_error = g_bl616cl_pwm.last_error;
  diag->divider = g_bl616cl_pwm.divider;
  diag->period = g_bl616cl_pwm.period;
  diag->pin_acquired = g_bl616cl_pwm.pin_acquired;
  diag->clock_enabled = g_bl616cl_pwm.clock_enabled;
  diag->started = g_bl616cl_pwm.started;

  for (ch = 0; ch < BL616CL_PWM_NCHANNELS; ch++)
    {
      chan = &g_bl616cl_pwm.chan[ch];
      out = &diag->channels[ch];
      out->pos_pin = chan->pos_pin;
      out->neg_pin = chan->neg_pin;
      out->threshold_low = chan->threshold_low;
      out->threshold_high = chan->threshold_high;
      out->cpol = chan->set.cpol;
      out->dcpol = chan->set.dcpol;
      out->configured = chan->configured;
      out->active = chan->set.active;
      out->enabled = chan->enabled;
      if (registers)
        {
          out->pos_output = (config1 & BL616CL_PWM_PEN(ch)) != 0;
          out->neg_output = (config1 & BL616CL_PWM_NEN(ch)) != 0;
          out->pos_polarity_high = (config1 & BL616CL_PWM_PPL(ch)) != 0;
          out->neg_polarity_high = (config1 & BL616CL_PWM_NPL(ch)) != 0;
          out->pos_stop_active = (config1 & BL616CL_PWM_PSI(ch)) != 0;
          out->neg_stop_active = (config1 & BL616CL_PWM_NSI(ch)) != 0;
        }

      if (g_bl616cl_pwm.gpio != NULL && chan->pos_pin >= 0)
        {
          out->pos_level = bflb_gpio_read(g_bl616cl_pwm.gpio,
                                          chan->pos_pin);
        }

      if (g_bl616cl_pwm.gpio != NULL && chan->neg_pin >= 0)
        {
          out->neg_level = bflb_gpio_read(g_bl616cl_pwm.gpio,
                                          chan->neg_pin);
        }
    }

  return OK;
}
#endif
