/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_flash.c
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

#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>

#include "bl616cl_sdk.h"
#include "bl616cl_glb.h"
#include "bflb_flash.h"
#include "bflb_xip_sflash.h"
#include "hardware/sf_ctrl_reg.h"

#include "bl616cl_flash.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Delay windows of SDK board_set_flash_hs() for the 80 MHz MUXPLL clock */

#define FLASH_HS_1P5T_LEFT   120
#define FLASH_HS_1P5T_RIGHT  155
#define FLASH_HS_1T_LEFT     50
#define FLASH_HS_1T_RIGHT    70
#define FLASH_HS_1P5T_BEST   125
#define FLASH_HS_MAX_ADDED   6

#define FLASH_SF_CTRL_0      (BFLB_SF_CTRL_BASE + SF_CTRL_0_OFFSET)
#define FLASH_SF_CTRL_1      (BFLB_SF_CTRL_BASE + SF_CTRL_1_OFFSET)

#define FLASH_BOOTHEADER_LEN 256

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct flash_clock_para_s
{
  uint8_t flash_clock;
  uint8_t flash_clock_div;
  uint8_t delay_value;
  uint32_t sdmin;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* SDK flash_clock_para_list: WIFIPLL sdmin steps that scan the flash sample
 * point.  It is read with XIP disabled, so it must stay in RAM.
 */

static const struct flash_clock_para_s g_flash_clock_para[]
ATTR_TCM_CONST_SECTION =
{
  { GLB_SFLASH_CLK_WIFIPLL_120M, 3, 180, 5825422 },
  { GLB_SFLASH_CLK_WIFIPLL_120M, 3, 175, 5991862 },
  { GLB_SFLASH_CLK_WIFIPLL_120M, 3, 170, 6168094 },
  { GLB_SFLASH_CLK_WIFIPLL_120M, 3, 165, 6355006 },
  { GLB_SFLASH_CLK_WIFIPLL_120M, 3, 160, 6553600 },
  { GLB_SFLASH_CLK_WIFIPLL_120M, 3, 155, 6765006 },
  { GLB_SFLASH_CLK_WIFIPLL_120M, 3, 150, 6990506 },
  { GLB_SFLASH_CLK_MUXPLL_80M,   1, 145, 5423668 },
  { GLB_SFLASH_CLK_MUXPLL_80M,   1, 140, 5617371 },
  { GLB_SFLASH_CLK_MUXPLL_80M,   1, 135, 5825422 },
  { GLB_SFLASH_CLK_MUXPLL_80M,   1, 130, 6049476 },
  { GLB_SFLASH_CLK_MUXPLL_80M,   1, 125, 6291456 },
  { GLB_SFLASH_CLK_MUXPLL_80M,   1, 120, 6553600 },
  { GLB_SFLASH_CLK_MUXPLL_80M,   1, 115, 6838539 },
  { GLB_SFLASH_CLK_WIFIPLL_96M,  1, 110, 5957818 },
  { GLB_SFLASH_CLK_WIFIPLL_96M,  1, 105, 6241523 },
  { GLB_SFLASH_CLK_WIFIPLL_96M,  1, 100, 6553600 },
  { GLB_SFLASH_CLK_WIFIPLL_96M,  1, 95,  6898526 },
  { GLB_SFLASH_CLK_WIFIPLL_120M, 1, 90,  5825422 },
  { GLB_SFLASH_CLK_WIFIPLL_120M, 1, 85,  6168094 },
  { GLB_SFLASH_CLK_WIFIPLL_120M, 1, 80,  6553600 },
  { GLB_SFLASH_CLK_WIFIPLL_120M, 1, 75,  6990506 },
  { GLB_SFLASH_CLK_MUXPLL_80M,   0, 70,  5617371 },
  { GLB_SFLASH_CLK_MUXPLL_80M,   0, 65,  6049476 },
  { GLB_SFLASH_CLK_MUXPLL_80M,   0, 60,  6553600 },
  { GLB_SFLASH_CLK_WIFIPLL_96M,  0, 55,  5957818 },
  { GLB_SFLASH_CLK_WIFIPLL_96M,  0, 50,  6553600 },
  { GLB_SFLASH_CLK_WIFIPLL_120M, 0, 45,  5825422 },
  { GLB_SFLASH_CLK_WIFIPLL_120M, 0, 40,  6553600 },
};

/* WIFIPLL VCO speed thresholds of SDK clk_pll_set() */

static const uint32_t g_flash_sdmin_range[] ATTR_TCM_CONST_SECTION =
{
  0x6e0000, 0x640000, 0x5a0000, 0x500000, 0x460000, 0x3c0000
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: flash_reset_io_cs_delay
 *
 * Description:
 *   Reset the IO delay and CS/clock delay of flash pads 1 to 3 to zero.
 *   Placed in TCM because it runs with XIP disabled.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void ATTR_TCM_SECTION flash_reset_io_cs_delay(void)
{
  uint8_t pad;

  for (pad = SF_CTRL_PAD1; pad <= SF_CTRL_PAD3; pad++)
    {
      bflb_sf_ctrl_set_io_delay(pad, 0, 0, 0);
      bflb_sf_ctrl_set_cs_clk_delay(pad, 0, 0);
    }
}

/****************************************************************************
 * Name: flash_pll_set
 *
 * Description:
 *   Program the WIFIPLL SDM input, switch the MCU system clock to RC32M while
 *   the PLL is restarted, select the VCO speed from g_flash_sdmin_range,
 *   cycle the PLL reset with the required settling delays, then switch the
 *   MCU system clock back to the 240 MHz WIFIPLL output. Placed in TCM
 *   because it runs with XIP disabled.
 *
 * Input Parameters:
 *   sdmin - New value of the WIFIPLL SDM input field
 *
 * Returned Value:
 *   Zero if the PLL locked; -1 if it did not lock.
 *
 ****************************************************************************/

static int ATTR_TCM_SECTION flash_pll_set(uint32_t sdmin)
{
  uint32_t regval;
  int vco_speed = 1;
  int ret = -1;
  int i;

  regval = getreg32(GLB_BASE + GLB_WIFIPLL_SDMIN_OFFSET);
  regval = (regval & GLB_WIFIPLL_SDM_IN_UMSK) | sdmin;
  putreg32(regval, GLB_BASE + GLB_WIFIPLL_SDMIN_OFFSET);

  GLB_Set_MCU_System_CLK(GLB_MCU_SYS_CLK_RC32M);
  GLB_Set_MCU_System_CLK_Div(0, 0);

  for (i = 0; i < nitems(g_flash_sdmin_range); i++)
    {
      if (sdmin > g_flash_sdmin_range[i])
        {
          vco_speed = nitems(g_flash_sdmin_range) - i + 1;
          break;
        }
    }

  regval = getreg32(GLB_BASE + GLB_WIFIPLL_ANA_CTRL_OFFSET);
  regval = BL_SET_REG_BITS_VAL(regval, GLB_WIFIPLL_VCO_SPEED, vco_speed);
  regval = BL_SET_REG_BITS_VAL(regval, GLB_WIFIPLL_DTC_R_SEL, 3);
  regval = BL_SET_REG_BITS_VAL(regval, GLB_PU_WIFIPLL, 1);
  regval = BL_SET_REG_BITS_VAL(regval, GLB_WIFIPLL_RSTB, 1);
  putreg32(regval, GLB_BASE + GLB_WIFIPLL_ANA_CTRL_OFFSET);
  arch_delay_us(6);

  regval = getreg32(GLB_BASE + GLB_WIFIPLL_ANA_CTRL_OFFSET);
  regval = BL_SET_REG_BITS_VAL(regval, GLB_WIFIPLL_RSTB, 0);
  putreg32(regval, GLB_BASE + GLB_WIFIPLL_ANA_CTRL_OFFSET);
  arch_delay_us(2);

  regval = getreg32(GLB_BASE + GLB_WIFIPLL_ANA_CTRL_OFFSET);
  regval = BL_SET_REG_BITS_VAL(regval, GLB_WIFIPLL_RSTB, 1);
  putreg32(regval, GLB_BASE + GLB_WIFIPLL_ANA_CTRL_OFFSET);
  arch_delay_us(270);

  regval = getreg32(GLB_BASE + GLB_WIFIPLL_TEST_AND_READBACK_OFFSET);
  if (BL_GET_REG_BITS_VAL(regval, GLB_WIFIPLL_LO_LOCK) == 1)
    {
      ret = 0;
    }

  GLB_Set_MCU_System_CLK(GLB_MCU_SYS_CLK_TOP_WIFIPLL_240M);
  GLB_Set_MCU_System_CLK_Div(0, 3);
  return ret;
}

/****************************************************************************
 * Name: flash_check_bootheader
 *
 * Description:
 *   Read the flash boot header and verify its CRC32, to test whether flash
 *   reads work at the current clock and delay setting. Placed in TCM because
 *   it runs with XIP disabled.
 *
 * Input Parameters:
 *   cfg - Flash configuration used for the read
 *
 * Returned Value:
 *   Zero if the boot header CRC matches; -1 if the read fails or the CRC
 *   differs.
 *
 ****************************************************************************/

static int ATTR_TCM_SECTION flash_check_bootheader(spi_flash_cfg_type *cfg)
{
  uint8_t data[FLASH_BOOTHEADER_LEN];
  uint32_t crc;

  if (bflb_sflash_read(cfg, cfg->io_mode & 0xf, 0, 0, data,
                       sizeof(data)) != 0)
    {
      return -1;
    }

  crc = bflb_soft_crc32(data, sizeof(data) - 4);
  return arch_memcmp(&crc, data + sizeof(data) - 4, 4) == 0 ? 0 : -1;
}

/****************************************************************************
 * Name: flash_get_delay
 *
 * Description:
 *   Scan the sample point by stepping the flash clock and return the delay
 *   value of the first failing step after a passing one (0 if none). The scan
 *   stops early if the PLL fails to lock, and the original PLL setting is
 *   always restored. Placed in TCM because it runs with XIP disabled.
 *
 * Input Parameters:
 *   cfg - Flash configuration used for the boot header reads
 *
 * Returned Value:
 *   The delay value of the first failing step after a passing one, or 0 if
 *   there is none.
 *
 ****************************************************************************/

static uint8_t ATTR_TCM_SECTION flash_get_delay(spi_flash_cfg_type *cfg)
{
  uint8_t state = 0;
  uint8_t delay = 0;
  uint32_t sdmin;
  int i;

  sdmin = getreg32(GLB_BASE + GLB_WIFIPLL_SDMIN_OFFSET) &
          GLB_WIFIPLL_SDM_IN_MSK;

  for (i = 0; i < nitems(g_flash_clock_para); i++)
    {
      GLB_Set_SF_CLK(1, g_flash_clock_para[i].flash_clock,
                     g_flash_clock_para[i].flash_clock_div);
      if (flash_pll_set(g_flash_clock_para[i].sdmin) != 0)
        {
          break;
        }

      if (flash_check_bootheader(cfg) != 0)
        {
          if (state == 1)
            {
              delay = g_flash_clock_para[i].delay_value;
              break;
            }
        }
      else
        {
          state = 1;
        }
    }

  flash_pll_set(sdmin);
  return delay;
}

/****************************************************************************
 * Name: flash_set_hs_cfg
 *
 * Description:
 *   Apply a high-speed flash controller setting: clock delay 1, clock
 *   inversion set, the given RX clock inversion, re-initialize the serial
 *   flash controller and select the 80 MHz MUXPLL flash clock. Placed in TCM
 *   because it runs with XIP disabled.
 *
 * Input Parameters:
 *   sf - Serial flash controller configuration to update and apply
 *   rx_clk_invert - RX clock inversion value
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void ATTR_TCM_SECTION flash_set_hs_cfg(struct sf_ctrl_cfg_type *sf,
                                              uint8_t rx_clk_invert)
{
  sf->clk_delay = 1;
  sf->clk_invert = 1;
  sf->rx_clk_invert = rx_clk_invert;
  bflb_sflash_init(sf, NULL);
  GLB_Set_SF_CLK(1, GLB_SFLASH_CLK_MUXPLL_80M, 0);
}

/****************************************************************************
 * Name: flash_set_hs
 *
 * Description:
 *   Port of SDK board_set_flash_hs(GLB_SFLASH_CLK_MUXPLL_80M). Runs with XIP
 *   disabled, so everything it reaches must be in RAM. Measure the
 *   sample-point delay and choose the 1T or 1.5T high-speed setting, adding
 *   pad delay when the delay falls between the two windows. If no delay
 *   setting fits, the boot flash clock and pad delays are restored.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void ATTR_TCM_SECTION flash_set_hs(void)
{
  struct sf_ctrl_cfg_type sf =
  {
    .owner         = SF_CTRL_OWNER_SAHB,
    .clk_invert    = 1,
    .rx_clk_invert = 1,
  };

  spi_flash_cfg_type *cfg;
  uint32_t cfg_len;
  uint32_t sf_ctrl_0;
  uint32_t sf_ctrl_1;
  uint32_t sf_clk_cfg;
  uint32_t offset = 0;
  uint8_t aes_enable = 0;
  uint8_t org_delay;
  uint8_t final_delay;
  uint8_t added = 0;
  uint8_t pad;
  bool delay_found = false;
  bool changed = false;

  sf_ctrl_0 = getreg32(FLASH_SF_CTRL_0);
  sf_ctrl_1 = getreg32(FLASH_SF_CTRL_1);
  sf_clk_cfg = getreg32(GLB_BASE + GLB_SF_CFG0_OFFSET);

  bflb_flash_get_cfg((uint8_t **)&cfg, &cfg_len);
  bflb_xip_sflash_opt_enter(&aes_enable);
  bflb_xip_sflash_state_save(cfg, &offset, 0, 0);

  bflb_sflash_init(&sf, NULL);
  bflb_sflash_reset_continue_read(cfg);
  flash_reset_io_cs_delay();

  org_delay = flash_get_delay(cfg);

  if (org_delay >= FLASH_HS_1P5T_LEFT && org_delay <= FLASH_HS_1P5T_RIGHT)
    {
      flash_set_hs_cfg(&sf, 1);
      changed = true;
    }
  else if (org_delay >= FLASH_HS_1T_LEFT && org_delay <= FLASH_HS_1T_RIGHT)
    {
      flash_set_hs_cfg(&sf, 0);
      changed = true;
    }
  else if (org_delay > FLASH_HS_1T_RIGHT && org_delay < FLASH_HS_1P5T_LEFT)
    {
      /* Between the 1T and 1.5T windows: add pad delay until 1.5T fits */

      added = 1;
      while (1)
        {
          if (delay_found)
            {
              flash_set_hs_cfg(&sf, 1);
              added--;
            }

          if (added > FLASH_HS_MAX_ADDED)
            {
              break;
            }

          for (pad = SF_CTRL_PAD1; pad <= SF_CTRL_PAD3; pad++)
            {
              bflb_sf_ctrl_set_io_delay(pad, 0, added <= 3 ? added : 3, 0);
              if (added > 3)
                {
                  bflb_sf_ctrl_set_cs_clk_delay(pad, 0, (added - 3) & 3);
                }
            }

          if (delay_found)
            {
              changed = true;
              break;
            }

          final_delay = flash_get_delay(cfg);
          if (final_delay >= FLASH_HS_1P5T_BEST ||
              (final_delay >= FLASH_HS_1P5T_BEST - 15 &&
               added == FLASH_HS_MAX_ADDED))
            {
              delay_found = true;
            }

          added++;
        }
    }

  if (!changed)
    {
      flash_reset_io_cs_delay();
      putreg32(sf_ctrl_0, FLASH_SF_CTRL_0);
      putreg32(sf_ctrl_1, FLASH_SF_CTRL_1);
      putreg32(sf_clk_cfg, GLB_BASE + GLB_SF_CFG0_OFFSET);
    }
  else
    {
      cfg->clk_delay = sf.clk_delay;
      cfg->clk_invert = sf.clk_invert | (sf.rx_clk_invert << 1);
    }

  bflb_sf_ctrl_set_owner(SF_CTRL_OWNER_SAHB);
  bflb_xip_sflash_state_restore(cfg, offset, 0, 0);
  bflb_xip_sflash_opt_exit(aes_enable);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_flash_early_init
 *
 * Description:
 *   Enable the second serial flash interface bank (BK2 enable and mode) in
 *   SF_CTRL_2 during early startup.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void bl616cl_flash_early_init(void)
{
  uint32_t regval;

  regval = getreg32(SF_CTRL_BASE + SF_CTRL_2_OFFSET);
  regval |= SF_CTRL_SF_IF_BK2_EN | SF_CTRL_SF_IF_BK2_MODE;
  putreg32(regval, SF_CTRL_BASE + SF_CTRL_2_OFFSET);
}

/****************************************************************************
 * Name: bl616cl_flash_initialize
 *
 * Description:
 *   Initialize the SDK LHAL flash state after RAM-safe sections are loaded
 *   and before the system clock is changed, then calibrate the flash sample
 *   delay and switch the flash clock to 80 MHz like SDK board_init(). Placed
 *   in TCM because it runs with XIP disabled.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Zero on success; a nonzero value returned by bflb_flash_init() on
 *   failure, in which case the clock is not changed.
 *
 ****************************************************************************/

int ATTR_TCM_SECTION bl616cl_flash_initialize(void)
{
  int ret;

  ret = bflb_flash_init();
  if (ret == 0)
    {
      flash_set_hs();
    }

  return ret;
}
