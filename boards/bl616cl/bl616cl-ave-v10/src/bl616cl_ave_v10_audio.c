/****************************************************************************
 * vendor/bouffalolab/boards/bl616cl/bl616cl-ave-v10/src/bl616cl_ave_v10_audio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <nuttx/audio/audio.h>
#include <nuttx/audio/es8388.h>
#include <nuttx/i2c/i2c_master.h>

#include "bl616cl_i2c.h"
#include "bl616cl_i2s.h"
#include "bl616cl_ave_v10.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct bl616cl_i2s_pins_s g_i2s_pins =
{
  .mclk = BL616CL_AVE_V10_I2S_MCLK_PIN,
  .bclk = BL616CL_AVE_V10_I2S_BCLK_PIN,
  .fs = BL616CL_AVE_V10_I2S_FS_PIN,
  .di = BL616CL_AVE_V10_I2S_DI_PIN,
  .dout = BL616CL_AVE_V10_I2S_DO_PIN,
};

static const struct es8388_lower_s g_es8388_lower =
{
  .frequency = BL616CL_AVE_V10_ES8388_I2C_FREQ,
  .address = BL616CL_AVE_V10_ES8388_ADDR,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_ave_v10_audio_initialize
 *
 * Description:
 *   Register the ES8388 codec on I2C0 and I2S0 as /dev/audio/pcm0
 *   (playback) and /dev/audio/pcm_in0 (recording). The two are separate
 *   driver instances of the same codec; each configures the whole chip, so
 *   use one at a time. I2C0 is shared with /dev/i2c0 (the bus is reference
 *   counted). I2S0 starts MCLK before the codec is reset.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero (OK) on success; -ENODEV if the I2C bus or I2S cannot be
 *   initialized, -ENOMEM if a codec instance cannot be allocated, or the
 *   negated errno value from audio_register(). The codec is not probed, so
 *   a codec that does not answer on I2C is not reported here. If only the
 *   recording device fails, /dev/audio/pcm0 stays registered.
 *
 ****************************************************************************/

int bl616cl_ave_v10_audio_initialize(void)
{
  struct audio_lowerhalf_s *codec;
  struct i2c_master_s *i2c;
  struct i2s_dev_s *i2s;
  int ret;

  i2c = bl616cl_i2cbus_initialize(0, BL616CL_AVE_V10_I2C0_SCL_PIN,
                                  BL616CL_AVE_V10_I2C0_SDA_PIN);
  if (i2c == NULL)
    {
      return -ENODEV;
    }

  i2s = bl616cl_i2sbus_initialize(&g_i2s_pins);
  if (i2s == NULL)
    {
      bl616cl_i2cbus_uninitialize(i2c);
      return -ENODEV;
    }

  /* The ES8388 driver has no uninitialize, so a codec instance and its I2C
   * reference cannot be released once created. Playback is registered
   * before the recording instance is created, so that /dev/audio/pcm0
   * works even if recording fails.
   */

  codec = es8388_initialize(i2c, i2s, &g_es8388_lower);
  if (codec == NULL)
    {
      bl616cl_i2cbus_uninitialize(i2c);
      return -ENOMEM;
    }

  ret = audio_register("pcm0", codec);
  if (ret < 0)
    {
      return ret;
    }

  codec = es8388_initialize(i2c, i2s, &g_es8388_lower);
  if (codec == NULL)
    {
      return -ENOMEM;
    }

  return audio_register("pcm_in0", codec);
}
