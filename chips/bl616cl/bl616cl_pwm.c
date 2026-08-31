/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_pwm.c
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
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/timers/pwm.h>

/* bflb_core.h includes newlib's sys/errno.h.  Preserve the NuttX ABI value
 * before including LHAL headers.
 */

enum
{
  BL616CL_PWM_NUTTX_ETIMEDOUT = ETIMEDOUT,
};

#include "bflb_clock.h"
#include "bflb_gpio.h"
#include "bflb_name.h"
#include "bflb_peri.h"
#include "bflb_pwm_v2.h"
#include "hardware/pwm_v2_reg.h"
#include "bl616cl_pwm.h"
#ifdef CONFIG_BL616CL_PWM_TEST
#include "bl616cl_pwm_test.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_PWM_CHANNEL     PWM_CH3
#define BL616CL_PWM_PIN         22
#define BL616CL_PWM_MIN_DIVIDER 1
#define BL616CL_PWM_MAX_DIVIDER UINT16_MAX
#define BL616CL_PWM_MIN_PERIOD  2
#define BL616CL_PWM_MAX_PERIOD  UINT16_MAX

#define BL616CL_PWM_GPIO_OUTPUT \
  (GPIO_FUNC_GPIO | GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_1)
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
  uint64_t error_num;
  uint32_t error_den;
};

enum bl616cl_pwm_operation_e
{
  BL616CL_PWM_OPERATION_NONE = 0,
  BL616CL_PWM_OPERATION_INIT,
  BL616CL_PWM_OPERATION_START,
  BL616CL_PWM_OPERATION_STOP,
  BL616CL_PWM_OPERATION_DEINIT,
};

struct bl616cl_pwm_lowerhalf_s
{
  FAR const struct pwm_ops_s *ops;
  FAR struct bflb_device_s *dev;
  FAR struct bflb_device_s *gpio;
  uint32_t source_frequency;
  uint32_t actual_frequency;
  uint16_t divider;
  uint16_t period;
  uint16_t threshold_low;
  uint16_t threshold_high;
  uint8_t channel;
  uint8_t pin;
  uint8_t cpol;
  uint8_t dcpol;
  bool initialized;
  bool pin_acquired;
  bool clock_enabled;
  bool channel_enabled;
  bool started;
#ifdef CONFIG_BL616CL_PWM_TEST
  enum bl616cl_pwm_test_fault_e fault;
  uint32_t setup_calls;
  uint32_t start_calls;
  uint32_t stop_calls;
  uint32_t shutdown_calls;
  uint32_t error_count;
  int last_error;
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bl616cl_pwm_setup(FAR struct pwm_lowerhalf_s *lower);
static int bl616cl_pwm_shutdown(FAR struct pwm_lowerhalf_s *lower);
static int bl616cl_pwm_start(FAR struct pwm_lowerhalf_s *lower,
                             FAR const struct pwm_info_s *info);
static int bl616cl_pwm_stop(FAR struct pwm_lowerhalf_s *lower);
static int bl616cl_pwm_ioctl(FAR struct pwm_lowerhalf_s *lower, int cmd,
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
  .channel = BL616CL_PWM_CHANNEL,
  .pin = BL616CL_PWM_PIN,
  .cpol = PWM_CPOL_LOW,
  .dcpol = PWM_DCPOL_LOW,
};

/* clang-format on */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bl616cl_pwm_record_error(
  FAR struct bl616cl_pwm_lowerhalf_s *priv, int error)
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

static bool bl616cl_pwm_error_less(uint64_t left_num, uint32_t left_den,
                                   uint64_t right_num, uint32_t right_den)
{
  uint64_t left_quotient = left_num / left_den;
  uint64_t right_quotient = right_num / right_den;
  uint64_t left_remainder;
  uint64_t right_remainder;

  if (left_quotient != right_quotient)
    {
      return left_quotient < right_quotient;
    }

  left_remainder = left_num % left_den;
  right_remainder = right_num % right_den;

  /* Both remainders and denominators are below 2^32, so this comparison
   * remains bounded in 64 bits.
   */

  return left_remainder * right_den < right_remainder * left_den;
}

static void bl616cl_pwm_consider_solution(
  uint32_t source, uint32_t frequency, uint32_t divider, uint32_t period,
  FAR struct bl616cl_pwm_solution_s *best, FAR bool *found)
{
  uint64_t denominator;
  uint64_t requested;
  uint64_t error;
  uint32_t actual;

  if (period < BL616CL_PWM_MIN_PERIOD ||
      period > BL616CL_PWM_MAX_PERIOD)
    {
      return;
    }

  denominator = (uint64_t)divider * period;
  requested = (uint64_t)frequency * denominator;
  error = requested > source ? requested - source : source - requested;
  actual = (uint32_t)((uint64_t)source / denominator);

  if (!*found ||
      bl616cl_pwm_error_less(error, (uint32_t)denominator,
                             best->error_num, best->error_den) ||
      (error == best->error_num && denominator == best->error_den &&
       divider < best->divider))
    {
      best->divider = divider;
      best->period = period;
      best->actual = actual;
      best->error_num = error;
      best->error_den = (uint32_t)denominator;
      *found = true;
    }
}

static int bl616cl_pwm_solve(uint32_t source, uint32_t frequency,
                             FAR struct bl616cl_pwm_solution_s *solution)
{
  uint64_t scaled;
  uint32_t divider;
  uint32_t period;
  bool found = false;

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

  for (divider = BL616CL_PWM_MIN_DIVIDER;
       divider <= BL616CL_PWM_MAX_DIVIDER; divider++)
    {
      scaled = (uint64_t)frequency * divider;
      period = (uint32_t)(((uint64_t)source + scaled / 2) / scaled);

      if (period > 0)
        {
          bl616cl_pwm_consider_solution(source, frequency, divider,
                                        period - 1, solution, &found);
        }

      bl616cl_pwm_consider_solution(source, frequency, divider, period,
                                    solution, &found);
      if (period < UINT32_MAX)
        {
          bl616cl_pwm_consider_solution(source, frequency, divider,
                                        period + 1, solution, &found);
        }

      if (period < BL616CL_PWM_MIN_PERIOD)
        {
          break;
        }
    }

  return found ? OK : -ERANGE;
}

static bool bl616cl_pwm_stopped(
  FAR struct bl616cl_pwm_lowerhalf_s *priv,
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

static void bl616cl_pwm_drive_stopped_pin(
  FAR struct bl616cl_pwm_lowerhalf_s *priv)
{
  if (priv->gpio == NULL)
    {
      return;
    }

  bflb_gpio_init(priv->gpio, priv->pin, BL616CL_PWM_GPIO_OUTPUT);
  if (priv->dcpol == PWM_DCPOL_HIGH)
    {
      bflb_gpio_set(priv->gpio, priv->pin);
    }
  else
    {
      bflb_gpio_reset(priv->gpio, priv->pin);
    }

  priv->pin_acquired = false;
}

static int bl616cl_pwm_enable_clock(
  FAR struct bl616cl_pwm_lowerhalf_s *priv)
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

static int bl616cl_pwm_acquire(FAR struct bl616cl_pwm_lowerhalf_s *priv)
{
  int ret;

  ret = bl616cl_pwm_enable_clock(priv);
  if (ret < 0)
    {
      return ret;
    }

  bflb_gpio_init(priv->gpio, priv->pin, BL616CL_PWM_GPIO_ALT);
  priv->pin_acquired = true;
  return OK;
}

static void bl616cl_pwm_release(FAR struct bl616cl_pwm_lowerhalf_s *priv)
{
  bl616cl_pwm_drive_stopped_pin(priv);
  if (priv->clock_enabled)
    {
      (void)bflb_peripheral_clock_control(BFLB_PERIPHERAL_PWM0, false);
      priv->clock_enabled = false;
    }

  priv->channel_enabled = false;
  priv->started = false;
}

static int bl616cl_pwm_force_stop(FAR struct bl616cl_pwm_lowerhalf_s *priv,
                                  enum bl616cl_pwm_operation_e operation)
{
  bool deinit_stopped;
  bool stop_stopped;

  if (priv->dev != NULL && priv->clock_enabled)
    {
      bflb_pwm_v2_channel_positive_stop(priv->dev, priv->channel);
      priv->channel_enabled = false;
      bflb_pwm_v2_stop(priv->dev);
      stop_stopped = bl616cl_pwm_stopped(priv, operation);
      bflb_pwm_v2_deinit(priv->dev);
      deinit_stopped = bl616cl_pwm_stopped(
        priv, BL616CL_PWM_OPERATION_DEINIT);
      bl616cl_pwm_release(priv);
      if (!stop_stopped || !deinit_stopped)
        {
          bl616cl_pwm_record_error(priv, -BL616CL_PWM_NUTTX_ETIMEDOUT);
          pwmerr("ERROR: PWM stop timed out: stop=%u deinit=%u\n",
                 stop_stopped, deinit_stopped);
          return -BL616CL_PWM_NUTTX_ETIMEDOUT;
        }
    }
  else
    {
      bl616cl_pwm_release(priv);
    }

  bl616cl_pwm_record_error(priv, OK);
  return OK;
}

static int bl616cl_pwm_validate(FAR const struct pwm_info_s *info)
{
  if (info == NULL || info->frequency == 0 ||
      (info->cpol != PWM_CPOL_LOW && info->cpol != PWM_CPOL_HIGH) ||
      (info->dcpol != PWM_DCPOL_LOW && info->dcpol != PWM_DCPOL_HIGH))
    {
      return -EINVAL;
    }

  return OK;
}

static int bl616cl_pwm_readback(
  FAR struct bl616cl_pwm_lowerhalf_s *priv,
  FAR const struct pwm_info_s *info,
  FAR const struct bl616cl_pwm_solution_s *solution)
{
  uint64_t denominator;
  uint32_t config0;
  uint32_t config1;
  uint32_t period_reg;
  uint32_t threshold;
  uint16_t divider;
  uint16_t period;
  bool polarity_high;
  bool stop_active;

  config0 = getreg32(priv->dev->reg_base + PWM_MC0_CONFIG0_OFFSET);
  config1 = getreg32(priv->dev->reg_base + PWM_MC0_CONFIG1_OFFSET);
  period_reg = getreg32(priv->dev->reg_base + PWM_MC0_PERIOD_OFFSET);
  threshold = getreg32(priv->dev->reg_base + PWM_MC0_CH3_THRE_OFFSET);
  divider = (config0 & PWM_CLK_DIV_MASK) >> PWM_CLK_DIV_SHIFT;
  period = (period_reg & PWM_PERIOD_MASK) >> PWM_PERIOD_SHIFT;
  polarity_high = (config1 & PWM_CH3_PPL) != 0;
  stop_active = (config1 & PWM_CH3_PSI) != 0;

  if (divider != solution->divider || period != solution->period ||
      ((config0 & PWM_REG_CLK_SEL_MASK) >> PWM_REG_CLK_SEL_SHIFT) != 1 ||
      (config0 & PWM_CENTER_ALIGNED_EN) != 0 ||
      (threshold & PWM_CH3_THREL_MASK) != priv->threshold_low ||
      ((threshold & PWM_CH3_THREH_MASK) >> PWM_CH3_THREH_SHIFT) !=
        priv->threshold_high ||
      polarity_high != (info->cpol == PWM_CPOL_HIGH) ||
      stop_active != ((info->cpol == PWM_CPOL_HIGH &&
                       info->dcpol == PWM_DCPOL_HIGH) ||
                      (info->cpol == PWM_CPOL_LOW &&
                       info->dcpol == PWM_DCPOL_LOW)) ||
      (config1 & PWM_CH3_PEN) == 0)
    {
      return -EIO;
    }

  denominator = (uint64_t)divider * period;
  priv->divider = divider;
  priv->period = period;
  priv->actual_frequency = priv->source_frequency / denominator;
  return priv->actual_frequency == solution->actual ? OK : -EIO;
}

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

static void bl616cl_pwm_channel_configure(
  FAR struct bl616cl_pwm_lowerhalf_s *priv,
  FAR const struct pwm_info_s *info, uint16_t period)
{
  struct bflb_pwm_v2_channel_config_s channel;
  bool stop_active;

  stop_active = (info->cpol == PWM_CPOL_HIGH &&
                 info->dcpol == PWM_DCPOL_HIGH) ||
                (info->cpol == PWM_CPOL_LOW &&
                 info->dcpol == PWM_DCPOL_LOW);

  memset(&channel, 0, sizeof(channel));
  channel.positive_polarity = info->cpol == PWM_CPOL_HIGH ?
                                PWM_POLARITY_ACTIVE_HIGH :
                                PWM_POLARITY_ACTIVE_LOW;
  channel.negative_polarity = PWM_POLARITY_ACTIVE_LOW;
  channel.positive_stop_state = stop_active ? PWM_STATE_ACTIVE :
                                              PWM_STATE_INACTIVE;
  channel.negative_stop_state = PWM_STATE_INACTIVE;
  channel.positive_brake_state = PWM_STATE_INACTIVE;
  channel.negative_brake_state = PWM_STATE_INACTIVE;
  channel.dead_time = 0;

  priv->threshold_low = 0;
  priv->threshold_high = bl616cl_pwm_threshold(info->duty, period);
  bflb_pwm_v2_channel_init(priv->dev, priv->channel, &channel);
  bflb_pwm_v2_channel_set_threshold(priv->dev, priv->channel,
                                    priv->threshold_low,
                                    priv->threshold_high);
  priv->cpol = info->cpol;
  priv->dcpol = info->dcpol;
}

static int bl616cl_pwm_setup(FAR struct pwm_lowerhalf_s *lower)
{
  FAR struct bl616cl_pwm_lowerhalf_s *priv =
    (FAR struct bl616cl_pwm_lowerhalf_s *)lower;
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

static int bl616cl_pwm_shutdown(FAR struct pwm_lowerhalf_s *lower)
{
  FAR struct bl616cl_pwm_lowerhalf_s *priv =
    (FAR struct bl616cl_pwm_lowerhalf_s *)lower;
  int ret;

#ifdef CONFIG_BL616CL_PWM_TEST
  priv->shutdown_calls++;
#endif
  ret = bl616cl_pwm_force_stop(priv, BL616CL_PWM_OPERATION_STOP);
  priv->initialized = false;
  return ret;
}

static int bl616cl_pwm_start(FAR struct pwm_lowerhalf_s *lower,
                             FAR const struct pwm_info_s *info)
{
  FAR struct bl616cl_pwm_lowerhalf_s *priv =
    (FAR struct bl616cl_pwm_lowerhalf_s *)lower;
  struct bflb_pwm_v2_config_s config;
  struct bl616cl_pwm_solution_s solution;
  bool same_divider;
  int ret;

#ifdef CONFIG_BL616CL_PWM_TEST
  priv->start_calls++;
#endif
  ret = bl616cl_pwm_validate(info);
  if (ret < 0)
    {
      bl616cl_pwm_record_error(priv, ret);
      return ret;
    }

  priv->source_frequency =
    bflb_clk_get_system_clock(BFLB_SYSTEM_PBCLK);
  ret = bl616cl_pwm_solve(priv->source_frequency, info->frequency,
                          &solution);
  if (ret < 0)
    {
      bl616cl_pwm_record_error(priv, ret);
      return ret;
    }

  ret = bl616cl_pwm_acquire(priv);
  if (ret < 0)
    {
      bl616cl_pwm_record_error(priv, ret);
      return ret;
    }

  same_divider = priv->started && priv->divider == solution.divider;
  if (same_divider)
    {
      (void)bflb_pwm_v2_feature_control(priv->dev,
                                        PWM_CMD_UPDATE_DISABLE, 0);
      bflb_pwm_v2_set_period(priv->dev, solution.period);
      bl616cl_pwm_channel_configure(priv, info, solution.period);
      (void)bflb_pwm_v2_feature_control(priv->dev,
                                        PWM_CMD_UPDATE_GENERATE, 0);
      (void)bflb_pwm_v2_feature_control(priv->dev,
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

          ret = bl616cl_pwm_acquire(priv);
          if (ret < 0)
            {
              bl616cl_pwm_record_error(priv, ret);
              return ret;
            }
        }

      config.counter_mode = PWM_COUNTER_MODE_UP;
      config.clk_source = BFLB_SYSTEM_PBCLK;
      config.clk_div = solution.divider;
      config.period = solution.period;
      bflb_pwm_v2_init(priv->dev, &config);
      if (!bl616cl_pwm_stopped(priv,
                               BL616CL_PWM_OPERATION_INIT))
        {
          (void)bl616cl_pwm_force_stop(
            priv, BL616CL_PWM_OPERATION_NONE);
          bl616cl_pwm_record_error(priv, -BL616CL_PWM_NUTTX_ETIMEDOUT);
          pwmerr("ERROR: PWM initialization timed out\n");
          return -BL616CL_PWM_NUTTX_ETIMEDOUT;
        }

      bl616cl_pwm_channel_configure(priv, info, solution.period);
      bflb_pwm_v2_channel_positive_start(priv->dev, priv->channel);
      priv->channel_enabled = true;
      bflb_pwm_v2_start(priv->dev);
      if (bl616cl_pwm_stopped(priv,
                              BL616CL_PWM_OPERATION_START))
        {
          (void)bl616cl_pwm_force_stop(
            priv, BL616CL_PWM_OPERATION_NONE);
          bl616cl_pwm_record_error(priv, -BL616CL_PWM_NUTTX_ETIMEDOUT);
          pwmerr("ERROR: PWM start timed out\n");
          return -BL616CL_PWM_NUTTX_ETIMEDOUT;
        }
    }

  ret = bl616cl_pwm_readback(priv, info, &solution);
  if (ret < 0)
    {
      (void)bl616cl_pwm_force_stop(priv, BL616CL_PWM_OPERATION_NONE);
      bl616cl_pwm_record_error(priv, ret);
      pwmerr("ERROR: PWM configuration readback failed\n");
      return ret;
    }

  priv->started = true;
  priv->channel_enabled = true;
  bl616cl_pwm_record_error(priv, OK);
  return OK;
}

static int bl616cl_pwm_stop(FAR struct pwm_lowerhalf_s *lower)
{
  FAR struct bl616cl_pwm_lowerhalf_s *priv =
    (FAR struct bl616cl_pwm_lowerhalf_s *)lower;

#ifdef CONFIG_BL616CL_PWM_TEST
  priv->stop_calls++;
#endif
  return bl616cl_pwm_force_stop(priv,
                                BL616CL_PWM_OPERATION_STOP);
}

static int bl616cl_pwm_ioctl(FAR struct pwm_lowerhalf_s *lower, int cmd,
                             unsigned long arg)
{
  UNUSED(lower);
  UNUSED(cmd);
  UNUSED(arg);
  return -ENOTTY;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct pwm_lowerhalf_s *bl616cl_pwm_initialize(uint8_t channel,
                                                   uint8_t pin)
{
  if (channel != BL616CL_PWM_CHANNEL || pin != BL616CL_PWM_PIN)
    {
      return NULL;
    }

  g_bl616cl_pwm.dev = bflb_device_get_by_name(BFLB_NAME_PWM_V2_0);
  g_bl616cl_pwm.gpio = bflb_device_get_by_name(BFLB_NAME_GPIO);
  if (g_bl616cl_pwm.dev == NULL || g_bl616cl_pwm.gpio == NULL)
    {
      return NULL;
    }

  return (FAR struct pwm_lowerhalf_s *)&g_bl616cl_pwm;
}

#ifdef CONFIG_BL616CL_PWM_TEST
void bl616cl_pwm_test_reset(void)
{
  g_bl616cl_pwm.fault = BL616CL_PWM_TEST_FAULT_NONE;
  g_bl616cl_pwm.setup_calls = 0;
  g_bl616cl_pwm.start_calls = 0;
  g_bl616cl_pwm.stop_calls = 0;
  g_bl616cl_pwm.shutdown_calls = 0;
  g_bl616cl_pwm.error_count = 0;
  g_bl616cl_pwm.last_error = OK;
}

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

int bl616cl_pwm_test_get_diag(FAR struct bl616cl_pwm_test_diag_s *diag)
{
  uint32_t config1;
  bool polarity_high;
  bool stop_active;

  if (diag == NULL)
    {
      return -EINVAL;
    }

  polarity_high = g_bl616cl_pwm.cpol == PWM_CPOL_HIGH;
  stop_active = (g_bl616cl_pwm.cpol == PWM_CPOL_HIGH &&
                 g_bl616cl_pwm.dcpol == PWM_DCPOL_HIGH) ||
                (g_bl616cl_pwm.cpol == PWM_CPOL_LOW &&
                 g_bl616cl_pwm.dcpol == PWM_DCPOL_LOW);
  if (g_bl616cl_pwm.dev != NULL && g_bl616cl_pwm.clock_enabled)
    {
      config1 = getreg32(g_bl616cl_pwm.dev->reg_base +
                         PWM_MC0_CONFIG1_OFFSET);
      polarity_high = (config1 & PWM_CH3_PPL) != 0;
      stop_active = (config1 & PWM_CH3_PSI) != 0;
    }

  diag->setup_calls = g_bl616cl_pwm.setup_calls;
  diag->start_calls = g_bl616cl_pwm.start_calls;
  diag->stop_calls = g_bl616cl_pwm.stop_calls;
  diag->shutdown_calls = g_bl616cl_pwm.shutdown_calls;
  diag->source_frequency = g_bl616cl_pwm.source_frequency;
  diag->actual_frequency = g_bl616cl_pwm.actual_frequency;
  diag->error_count = g_bl616cl_pwm.error_count;
  diag->last_error = g_bl616cl_pwm.last_error;
  diag->divider = g_bl616cl_pwm.divider;
  diag->period = g_bl616cl_pwm.period;
  diag->threshold_low = g_bl616cl_pwm.threshold_low;
  diag->threshold_high = g_bl616cl_pwm.threshold_high;
  diag->cpol = g_bl616cl_pwm.cpol;
  diag->dcpol = g_bl616cl_pwm.dcpol;
  diag->polarity_active_high = polarity_high;
  diag->stop_active = stop_active;
  diag->channel_enabled = g_bl616cl_pwm.channel_enabled;
  diag->pin_acquired = g_bl616cl_pwm.pin_acquired;
  diag->clock_enabled = g_bl616cl_pwm.clock_enabled;
  diag->started = g_bl616cl_pwm.started;
  return OK;
}
#endif
