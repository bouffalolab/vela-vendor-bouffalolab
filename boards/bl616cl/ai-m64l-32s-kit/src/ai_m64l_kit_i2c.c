/****************************************************************************
 * apps/vendor/bouffalolab/boards/bl616cl/ai-m64l-32s-kit/src/ai_m64l_kit_i2c.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <nuttx/i2c/i2c_master.h>

#include "bl616cl_i2c.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AI_M64L_KIT_I2C_PIN_RESERVED(pin) \
  (((pin) >= 6 && (pin) <= 11) || (pin) == 21 || \
   ((pin) >= 32 && (pin) <= 36))

#ifdef CONFIG_AI_M64L_KIT_I2C0
#  ifdef CONFIG_AI_M64L_KIT_PWM
#    if CONFIG_AI_M64L_KIT_I2C0_SCL_PIN == 22 || \
        CONFIG_AI_M64L_KIT_I2C0_SDA_PIN == 22
#      error "I2C0 pin conflicts with PWM0 channel 3 on GPIO22"
#    endif
#  endif
#  if (CONFIG_AI_M64L_KIT_I2C0_SCL_PIN & 1) != 0
#    error "I2C0 SCL pin must be even"
#  endif
#  if (CONFIG_AI_M64L_KIT_I2C0_SDA_PIN & 1) == 0
#    error "I2C0 SDA pin must be odd"
#  endif
#  if AI_M64L_KIT_I2C_PIN_RESERVED(CONFIG_AI_M64L_KIT_I2C0_SCL_PIN) || \
      AI_M64L_KIT_I2C_PIN_RESERVED(CONFIG_AI_M64L_KIT_I2C0_SDA_PIN)
#    error "I2C0 pin conflicts with a reserved board resource"
#  endif
#endif

#ifdef CONFIG_AI_M64L_KIT_I2C1
#  ifdef CONFIG_AI_M64L_KIT_PWM
#    if CONFIG_AI_M64L_KIT_I2C1_SCL_PIN == 22 || \
        CONFIG_AI_M64L_KIT_I2C1_SDA_PIN == 22
#      error "I2C1 pin conflicts with PWM0 channel 3 on GPIO22"
#    endif
#  endif
#  if (CONFIG_AI_M64L_KIT_I2C1_SCL_PIN & 1) != 0
#    error "I2C1 SCL pin must be even"
#  endif
#  if (CONFIG_AI_M64L_KIT_I2C1_SDA_PIN & 1) == 0
#    error "I2C1 SDA pin must be odd"
#  endif
#  if AI_M64L_KIT_I2C_PIN_RESERVED(CONFIG_AI_M64L_KIT_I2C1_SCL_PIN) || \
      AI_M64L_KIT_I2C_PIN_RESERVED(CONFIG_AI_M64L_KIT_I2C1_SDA_PIN)
#    error "I2C1 pin conflicts with a reserved board resource"
#  endif
#endif

#if defined(CONFIG_AI_M64L_KIT_I2C0) && defined(CONFIG_AI_M64L_KIT_I2C1)
#  if CONFIG_AI_M64L_KIT_I2C0_SCL_PIN == \
      CONFIG_AI_M64L_KIT_I2C1_SCL_PIN || \
      CONFIG_AI_M64L_KIT_I2C0_SCL_PIN == \
      CONFIG_AI_M64L_KIT_I2C1_SDA_PIN || \
      CONFIG_AI_M64L_KIT_I2C0_SDA_PIN == \
      CONFIG_AI_M64L_KIT_I2C1_SCL_PIN || \
      CONFIG_AI_M64L_KIT_I2C0_SDA_PIN == \
      CONFIG_AI_M64L_KIT_I2C1_SDA_PIN
#    error "I2C0 and I2C1 pins must not overlap"
#  endif
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int ai_m64l_kit_i2c_register(int port, uint8_t scl_pin,
                                    uint8_t sda_pin)
{
  struct i2c_master_s *i2c;
  int ret;

  i2c = bl616cl_i2cbus_initialize(port, scl_pin, sda_pin);
  if (i2c == NULL)
    {
      return -ENODEV;
    }

  ret = i2c_register(i2c, port);
  if (ret < 0)
    {
      (void)bl616cl_i2cbus_uninitialize(i2c);
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ai_m64l_kit_i2c_initialize(void)
{
  int ret;

#ifdef CONFIG_AI_M64L_KIT_I2C0
  ret = ai_m64l_kit_i2c_register(0,
                                 CONFIG_AI_M64L_KIT_I2C0_SCL_PIN,
                                 CONFIG_AI_M64L_KIT_I2C0_SDA_PIN);
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_AI_M64L_KIT_I2C1
  ret = ai_m64l_kit_i2c_register(1,
                                 CONFIG_AI_M64L_KIT_I2C1_SCL_PIN,
                                 CONFIG_AI_M64L_KIT_I2C1_SDA_PIN);
  if (ret < 0)
    {
      return ret;
    }
#endif

  return OK;
}
