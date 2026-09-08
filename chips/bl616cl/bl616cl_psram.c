/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_psram.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <arch/barriers.h>

#include "bl616cl_sdk.h"
#include "bflb_efuse.h"
#include "bflb_gpio.h"
#include "bl616cl_aon.h"
#include "bl616cl_glb.h"
#include "bl616cl_tzc_sec.h"
#include "../../drivers/soc/bl616cl/std/include/bl616cl_psram.h"
#include "bl616cl_psram_internal.h"

static size_t g_psram_size;

static int bl616cl_psram_error(const char *message, int error)
{
  while (*message != '\0')
    {
      riscv_lowputc(*message++);
    }

  return error;
}

static void bl616cl_psram_release(void)
{
  /* The SDK register operations do not release their request on timeout. */

  modifyreg32(PSRAM_CTRL_BASE + PSRAM_CONFIGURE_OFFSET,
              PSRAM_REG_CONFIG_REQ_MSK, 0);
}

static int bl616cl_psram_read_id(uint16_t *id)
{
  unsigned int attempt;

  for (attempt = 0; attempt < 100; attempt++)
    {
      if (PSram_Ctrl_Winbond_Read_Reg(PSRAM0_ID, PSRAM_WINBOND_REG_ID0,
                                    id) != SUCCESS)
        {
          bl616cl_psram_release();
          return -ETIMEDOUT;
        }

      if (*id == PSRAM_ID_WINBOND_4MB || *id == PSRAM_ID_WINBOND_8MB ||
          *id == PSRAM_ID_WINBOND_16MB || *id == PSRAM_ID_WINBOND_32MB)
        {
          return 0;
        }

      up_udelay(1);
    }

  return -ENODEV;
}

static int bl616cl_psram_configure(PSRAM_Ctrl_Cfg_Type *ctrl,
                                  PSRAM_Winbond_Cfg_Type *config,
                                  size_t *size)
{
  uint16_t id;
  uint16_t verify_id;
  int ret;

  ctrl->size = PSRAM_SIZE_4MB;
  config->driveStrength = PSRAM_WINBOND_DRIVE_STRENGTH_35_OHMS_FOR_4M;
  PSram_Ctrl_Init(PSRAM0_ID, ctrl);
  if (PSram_Ctrl_Winbond_Write_Reg(PSRAM0_ID, PSRAM_WINBOND_REG_CR0,
                                 config) != SUCCESS)
    {
      bl616cl_psram_release();
      return -ETIMEDOUT;
    }

  ret = bl616cl_psram_read_id(&id);
  if (ret < 0)
    {
      return ret;
    }

  switch (id)
    {
      case PSRAM_ID_WINBOND_4MB:
        *size = 4u * 1024u * 1024u;
        break;
      case PSRAM_ID_WINBOND_8MB:
        *size = 8u * 1024u * 1024u;
        ctrl->size = PSRAM_SIZE_8MB;
        config->driveStrength =
          PSRAM_WINBOND_DRIVE_STRENGTH_25_OHMS_FOR_8M;
        break;
      case PSRAM_ID_WINBOND_16MB:
        *size = 16u * 1024u * 1024u;
        ctrl->size = PSRAM_SIZE_16MB;
        config->driveStrength =
          PSRAM_WINBOND_DRIVE_STRENGTH_25_OHMS_FOR_16M;
        break;
      default:
        return -EFBIG;
    }

  PSram_Ctrl_Init(PSRAM0_ID, ctrl);
  if (PSram_Ctrl_Winbond_Write_Reg(PSRAM0_ID, PSRAM_WINBOND_REG_CR0,
                                 config) != SUCCESS)
    {
      bl616cl_psram_release();
      return -ETIMEDOUT;
    }

  ret = bl616cl_psram_read_id(&verify_id);
  return ret < 0 ? ret : (verify_id == id ? 0 : -EIO);
}

static bool bl616cl_psram_calibration_pattern(void)
{
  volatile uint32_t *memory = (uint32_t *)BL616CL_PSRAM_NOCACHE_BASE;
  const size_t words = (4u * 1024u * 1024u) / sizeof(uint32_t);
  uint32_t pattern;
  size_t word;
  unsigned int pass;

  /* All supported devices have at least 4 MiB. This destructive calibration
   * runs before heap registration, through the uncached alias so neither
   * writes nor verification can be satisfied by CPU cache lines.
   */

  for (pass = 0; pass < 2; pass++)
    {
      for (word = 0; word < words; word++)
        {
          pattern = (uint32_t)word ^ 0x55aa33ccu;
          memory[word] = pass == 0 ? pattern : ~pattern;
        }

      UP_DSB();
      for (word = 0; word < words; word++)
        {
          pattern = (uint32_t)word ^ 0x55aa33ccu;
          if (memory[word] != (pass == 0 ? pattern : ~pattern))
            {
              UP_DSB();
              return false;
            }
        }

      UP_DSB();
    }

  return true;
}

static int bl616cl_psram_calibrate(PSRAM_Ctrl_Cfg_Type *ctrl,
                                  PSRAM_Winbond_Cfg_Type *config)
{
  unsigned int tap;
  unsigned int length = 0;
  unsigned int best_length = 0;
  unsigned int best_end = 0;
  unsigned int middle;
  size_t size;
  int ret;

  for (tap = 0; tap < 16; tap++)
    {
      ctrl->dqs_delay = (uint16_t)(0xffffu << (15 - tap));
      ret = bl616cl_psram_configure(ctrl, config, &size);
      if (ret == 0 && bl616cl_psram_calibration_pattern())
        {
          length++;
          if (length > best_length)
            {
              best_length = length;
              best_end = tap;
            }
        }
      else
        {
          length = 0;
        }
    }

  if (best_length < 3)
    {
      return -EIO;
    }

  middle = best_end - (best_length / 2);
  ctrl->dqs_delay = (uint16_t)(0xffffu << (15 - middle));
  return 0;
}

int bl616cl_psram_initialize(void)
{
  bflb_efuse_device_info_type device_info = {0};
  bflb_ef_ctrl_com_trim_t trim = {0};
  struct bflb_device_s *gpio;
  PSRAM_Ctrl_Cfg_Type ctrl =
  {
    .vendor = PSRAM_CTRL_VENDOR_WINBOND,
    .ioMode = PSRAM_CTRL_X8_MODE,
    .size = PSRAM_SIZE_4MB,
  };
  PSRAM_Winbond_Cfg_Type config =
  {
    .rst = DISABLE,
    .clockType = PSRAM_CLOCK_DIFF,
    .inputPowerDownMode = DISABLE,
    .hybridSleepMode = DISABLE,
    .linear_dis = ENABLE,
    .PASR = PSRAM_PARTIAL_REFRESH_FULL,
    .disDeepPowerDownMode = ENABLE,
    .fixedLatency = DISABLE,
    .burstLen = PSRAM_WINBOND_BURST_LENGTH_64_BYTES,
    .burstType = PSRAM_WRAPPED_BURST,
    .latency = PSRAM_WINBOND_6_CLOCKS_LATENCY,
    .driveStrength = PSRAM_WINBOND_DRIVE_STRENGTH_35_OHMS_FOR_4M,
  };
  unsigned int left;
  unsigned int right;
  unsigned int middle;
  unsigned int pin;
  size_t size;
  int ret;

  g_psram_size = 0;
  bflb_efuse_get_device_info(&device_info);
  if (device_info.psram_info == 0)
    {
      return bl616cl_psram_error("PSRAM: chip has no PSRAM\r\n", -ENODEV);
    }

  bflb_ef_ctrl_read_common_trim(NULL, "psram_trim", &trim, 1);
  left = (trim.value >> 4) & 15;
  right = trim.value & 15;
  if (trim.en &&
      (trim.parity != bflb_ef_ctrl_get_trim_parity(trim.value, trim.len) ||
       right < left))
    {
      return bl616cl_psram_error("PSRAM: invalid factory trim\r\n", -EINVAL);
    }

  middle = (left + right) / 2;
  ctrl.dqs_delay = (uint16_t)(0xffffu << (15 - middle));
  gpio = bflb_device_get_by_name("gpio");
  if (gpio == NULL)
    {
      return bl616cl_psram_error("PSRAM: GPIO unavailable\r\n", -ENODEV);
    }

  if (AON_LDO18_IO_Switch_Psram(1) != SUCCESS ||
      GLB_Set_PSRAMB_CLK_Sel(ENABLE, GLB_PSRAMB_EMI_WIFIPLL_320M, 0) !=
      SUCCESS)
    {
      return bl616cl_psram_error("PSRAM: power/clock failure\r\n", -EIO);
    }

  for (pin = 46; pin < 58; pin++)
    {
      bflb_gpio_init(gpio, pin,
                     GPIO_INPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_0);
    }

  Tzc_Sec_PSRAMB_Access_Release();

  /* Reapply the configuration even if boot2 enabled the GPIOs.
   * GPIO state alone does not establish the device identity or capacity.
   * No PSRAM allocation exists at this point in the startup sequence.
   */

  if (!trim.en)
    {
      ret = bl616cl_psram_calibrate(&ctrl, &config);
      if (ret < 0)
        {
          return bl616cl_psram_error("PSRAM: DQS calibration failed\r\n",
                                    ret);
        }
    }

  ret = bl616cl_psram_configure(&ctrl, &config, &size);
  if (ret < 0)
    {
      return bl616cl_psram_error(ret == -EFBIG ?
                                "PSRAM: exceeds 16 MiB PMP window\r\n" :
                                "PSRAM: final configuration failed\r\n",
                                ret);
    }

  if (!trim.en && !bl616cl_psram_calibration_pattern())
    {
      return bl616cl_psram_error("PSRAM: DQS midpoint unstable\r\n", -EIO);
    }

  g_psram_size = size;
  return 0;
}

size_t bl616cl_psram_size_get(void)
{
  return g_psram_size;
}
