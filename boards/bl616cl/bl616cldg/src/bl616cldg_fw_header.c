/****************************************************************************
 * vendor/bouffalolab/boards/bl616cl/bl616cldg/src/bl616cldg_fw_header.c
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

#include <assert.h>

#include "bl616cldg_fw_header.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CLDG_FW_HEADER_MAGIC       0x504e4642
#define BL616CLDG_FLASH_CFG_MAGIC       0x47464346
#define BL616CLDG_CLOCK_CFG_MAGIC       0x47464350
#define BL616CLDG_FW_HEADER_PLACEHOLDER 0xdeadbeef

/****************************************************************************
 * Public Data
 ****************************************************************************/

static_assert(sizeof(struct bootheader_s) == 256,
              "BL616CL firmware header must be 256 bytes");

const struct bootheader_s g_bl616cldg_fw_header
  __attribute__((section(".fw_header"))) =
{
  .magiccode = BL616CLDG_FW_HEADER_MAGIC,
  .revision = 0x00000001,
  .flash_cfg =
  {
    .magiccode = BL616CLDG_FLASH_CFG_MAGIC,
    .cfg =
    {
      .io_mode = 0x11,
      .cread_support = 0x00,
      .clk_delay = 0x01,
      .clk_invert = 0x01,
      .reset_en_cmd = 0x66,
      .reset_cmd = 0x99,
      .reset_cread_cmd = 0xff,
      .reset_cread_cmd_size = 0x03,
      .jedec_id_cmd = 0x9f,
      .jedec_id_cmd_dmy_clk = 0x00,
      .enter_32bits_addr_cmd = 0xb7,
      .exit_32bits_addr_cmd = 0xe9,
      .sector_size = 0x04,
      .mid = 0x00,
      .page_size = 0x100,
      .chip_erase_cmd = 0xc7,
      .sector_erase_cmd = 0x20,
      .blk32_erase_cmd = 0x52,
      .blk64_erase_cmd = 0xd8,
      .write_enable_cmd = 0x06,
      .page_program_cmd = 0x02,
      .qpage_program_cmd = 0x32,
      .qpp_addr_mode = 0x00,
      .fast_read_cmd = 0x0b,
      .fr_dmy_clk = 0x01,
      .qpi_fast_read_cmd = 0x0b,
      .qpi_fr_dmy_clk = 0x01,
      .fast_read_do_cmd = 0x3b,
      .fr_do_dmy_clk = 0x01,
      .fast_read_dio_cmd = 0xbb,
      .fr_dio_dmy_clk = 0x00,
      .fast_read_qo_cmd = 0x6b,
      .fr_qo_dmy_clk = 0x01,
      .fast_read_qio_cmd = 0xeb,
      .fr_qio_dmy_clk = 0x02,
      .qpi_fast_read_qio_cmd = 0xeb,
      .qpi_fr_qio_dmy_clk = 0x02,
      .qpi_page_program_cmd = 0x02,
      .write_vreg_enable_cmd = 0x50,
      .wr_enable_index = 0x00,
      .qe_index = 0x01,
      .busy_index = 0x00,
      .wr_enable_bit = 0x01,
      .qe_bit = 0x01,
      .busy_bit = 0x00,
      .wr_enable_write_reg_len = 0x02,
      .wr_enable_read_reg_len = 0x01,
      .qe_write_reg_len = 0x02,
      .qe_read_reg_len = 0x01,
      .release_power_down = 0xab,
      .busy_read_reg_len = 0x01,
      .read_reg_cmd =
      {
        0x05, 0x35, 0x00, 0x00
      },
      .write_reg_cmd =
      {
        0x01, 0x01, 0x00, 0x00
      },
      .enter_qpi = 0x38,
      .exit_qpi = 0xff,
      .cread_mode = 0x20,
      .cr_exit = 0xf0,
      .burst_wrap_cmd = 0x77,
      .burst_wrap_cmd_dmy_clk = 0x03,
      .burst_wrap_data_mode = 0x02,
      .burst_wrap_data = 0x40,
      .de_burst_wrap_cmd = 0x77,
      .de_burst_wrap_cmd_dmy_clk = 0x03,
      .de_burst_wrap_data_mode = 0x02,
      .de_burst_wrap_data = 0xf0,
      .time_esector = 300,
      .time_e32k = 1200,
      .time_e64k = 1200,
      .time_page_pgm = 50,
      .time_ce = 30000,
      .pd_delay = 20,
      .qe_data = 0,
    },
    .crc32 = BL616CLDG_FW_HEADER_PLACEHOLDER,
  },
  .clk_cfg =
  {
    .magiccode = BL616CLDG_CLOCK_CFG_MAGIC,
    .cfg =
    {
      .xtal_type = 0x07,
      .mcu_clk = 0x05,
      .mcu_clk_div = 0x00,
      .mcu_bclk_div = 0x00,
      .mcu_pbclk_div = 0x01,
      .emi_clk = 0x02,
      .emi_clk_div = 0x01,
      .flash_clk_type = 0x01,
      .flash_clk_div = 0x00,
      .wifipll_pu = 0x01,
      .aupll_pu = 0x00,
    },
    .crc32 = BL616CLDG_FW_HEADER_PLACEHOLDER,
  },
  .basic_cfg =
  {
    .sign_type = 0,
    .encrypt_type = 0,
    .key_sel = 0,
    .xts_mode = 0,
    .aes_region_lock = 0,
    .no_segment = 1,
    .reserved_0 = 0,
    .reserved_1 = 0,
    .cpu_master_id = 0,
    .notload_in_bootrom = 0,
    .crc_ignore = 1,
    .hash_ignore = 1,
    .power_on_mm = 1,
    .em_sel = 1,
    .cmds_en = 1,
    .cmds_wrap_mode = 1,
    .cmds_wrap_len = 9,
    .icache_invalid = 1,
    .dcache_invalid = 1,
    .reserved_3 = 0,
    .group_image_offset = 0x00001000,
    .aes_region_len = 0x00000000,
    .img_len_cnt = 0x00010000,
    .hash =
    {
      BL616CLDG_FW_HEADER_PLACEHOLDER
    },
  },
  .cpu_cfg =
  {
    .config_enable = 1,
    .halt_cpu = 0,
    .cache_enable = 0,
    .cache_wa = 0,
    .cache_wb = 0,
    .cache_wt = 0,
    .cache_way_dis = 0,
    .reserved = 0,
    .image_address_offset = 0x00000000,
    .reserved_1 = 0x80000000,
    .msp_val = 0x00000000,
  },
  .boot2_pt_table_0_reserved = 0x00000000,
  .boot2_pt_table_1_reserved = 0x00000000,
  .flash_cfg_table_addr = 0x00000000,
  .flash_cfg_table_len = 0x00000000,
  .crc32 = BL616CLDG_FW_HEADER_PLACEHOLDER,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/
