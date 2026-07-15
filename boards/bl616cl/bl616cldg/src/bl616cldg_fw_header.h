/****************************************************************************
 * apps/vendor/bouffalolab/boards/bl616cl/bl616cldg/src/bl616cldg_fw_header.h
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

#ifndef __VENDOR_BOUFFALOLAB_BOARDS_BL616CL_BL616CLDG_SRC_BL616CLDG_FW_HEADER_H
#define __VENDOR_BOUFFALOLAB_BOARDS_BL616CL_BL616CLDG_SRC_BL616CLDG_FW_HEADER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct __attribute__((packed, aligned(4))) bl616cldg_spi_flash_cfg_s
{
  uint8_t io_mode;
  uint8_t cread_support;
  uint8_t clk_delay;
  uint8_t clk_invert;
  uint8_t reset_en_cmd;
  uint8_t reset_cmd;
  uint8_t reset_cread_cmd;
  uint8_t reset_cread_cmd_size;
  uint8_t jedec_id_cmd;
  uint8_t jedec_id_cmd_dmy_clk;
  uint8_t enter_32bits_addr_cmd;
  uint8_t exit_32bits_addr_cmd;
  uint8_t sector_size;
  uint8_t mid;
  uint16_t page_size;
  uint8_t chip_erase_cmd;
  uint8_t sector_erase_cmd;
  uint8_t blk32_erase_cmd;
  uint8_t blk64_erase_cmd;
  uint8_t write_enable_cmd;
  uint8_t page_program_cmd;
  uint8_t qpage_program_cmd;
  uint8_t qpp_addr_mode;
  uint8_t fast_read_cmd;
  uint8_t fr_dmy_clk;
  uint8_t qpi_fast_read_cmd;
  uint8_t qpi_fr_dmy_clk;
  uint8_t fast_read_do_cmd;
  uint8_t fr_do_dmy_clk;
  uint8_t fast_read_dio_cmd;
  uint8_t fr_dio_dmy_clk;
  uint8_t fast_read_qo_cmd;
  uint8_t fr_qo_dmy_clk;
  uint8_t fast_read_qio_cmd;
  uint8_t fr_qio_dmy_clk;
  uint8_t qpi_fast_read_qio_cmd;
  uint8_t qpi_fr_qio_dmy_clk;
  uint8_t qpi_page_program_cmd;
  uint8_t write_vreg_enable_cmd;
  uint8_t wr_enable_index;
  uint8_t qe_index;
  uint8_t busy_index;
  uint8_t wr_enable_bit;
  uint8_t qe_bit;
  uint8_t busy_bit;
  uint8_t wr_enable_write_reg_len;
  uint8_t wr_enable_read_reg_len;
  uint8_t qe_write_reg_len;
  uint8_t qe_read_reg_len;
  uint8_t release_power_down;
  uint8_t busy_read_reg_len;
  uint8_t read_reg_cmd[4];
  uint8_t write_reg_cmd[4];
  uint8_t enter_qpi;
  uint8_t exit_qpi;
  uint8_t cread_mode;
  uint8_t cr_exit;
  uint8_t burst_wrap_cmd;
  uint8_t burst_wrap_cmd_dmy_clk;
  uint8_t burst_wrap_data_mode;
  uint8_t burst_wrap_data;
  uint8_t de_burst_wrap_cmd;
  uint8_t de_burst_wrap_cmd_dmy_clk;
  uint8_t de_burst_wrap_data_mode;
  uint8_t de_burst_wrap_data;
  uint16_t time_esector;
  uint16_t time_e32k;
  uint16_t time_e64k;
  uint16_t time_page_pgm;
  uint16_t time_ce;
  uint8_t pd_delay;
  uint8_t qe_data;
};

struct __attribute__((packed, aligned(4))) bl616cldg_boot_flash_cfg_s
{
  uint32_t magiccode;
  struct bl616cldg_spi_flash_cfg_s cfg;
  uint32_t crc32;
};

struct __attribute__((packed, aligned(4))) bl616cldg_sys_clk_cfg_s
{
  uint8_t xtal_type;
  uint8_t mcu_clk;
  uint8_t mcu_clk_div;
  uint8_t mcu_bclk_div;
  uint8_t mcu_pbclk_div;
  uint8_t emi_clk;
  uint8_t emi_clk_div;
  uint8_t flash_clk_type;
  uint8_t flash_clk_div;
  uint8_t wifipll_pu;
  uint8_t aupll_pu;
  uint8_t reserved;
};

struct __attribute__((packed, aligned(4))) bl616cldg_boot_clk_cfg_s
{
  uint32_t magiccode;
  struct bl616cldg_sys_clk_cfg_s cfg;
  uint32_t crc32;
};

struct __attribute__((packed, aligned(4))) bl616cldg_boot_basic_cfg_s
{
  uint32_t sign_type : 2;
  uint32_t encrypt_type : 2;
  uint32_t key_sel : 2;
  uint32_t xts_mode : 1;
  uint32_t aes_region_lock : 1;
  uint32_t no_segment : 1;
  uint32_t reserved_0 : 1;
  uint32_t reserved_1 : 1;
  uint32_t cpu_master_id : 4;
  uint32_t notload_in_bootrom : 1;
  uint32_t crc_ignore : 1;
  uint32_t hash_ignore : 1;
  uint32_t power_on_mm : 1;
  uint32_t em_sel : 3;
  uint32_t cmds_en : 1;
  uint32_t cmds_wrap_mode : 2;
  uint32_t cmds_wrap_len : 4;
  uint32_t icache_invalid : 1;
  uint32_t dcache_invalid : 1;
  uint32_t reserved_3 : 1;
  uint32_t group_image_offset;
  uint32_t aes_region_len;
  uint32_t img_len_cnt;
  uint32_t hash[8];
};

struct __attribute__((packed, aligned(4))) bl616cldg_boot_cpu_cfg_s
{
  uint8_t config_enable;
  uint8_t halt_cpu;
  uint8_t cache_enable : 1;
  uint8_t cache_wa : 1;
  uint8_t cache_wb : 1;
  uint8_t cache_wt : 1;
  uint8_t cache_way_dis : 4;
  uint8_t reserved;
  uint32_t image_address_offset;
  uint32_t reserved_1;
  uint32_t msp_val;
};

struct __attribute__((packed, aligned(4))) bootheader_s
{
  uint32_t magiccode;
  uint32_t revision;
  struct bl616cldg_boot_flash_cfg_s flash_cfg;
  struct bl616cldg_boot_clk_cfg_s clk_cfg;
  struct bl616cldg_boot_basic_cfg_s basic_cfg;
  struct bl616cldg_boot_cpu_cfg_s cpu_cfg;
  uint32_t boot2_pt_table_0_reserved;
  uint32_t boot2_pt_table_1_reserved;
  uint32_t flash_cfg_table_addr;
  uint32_t flash_cfg_table_len;
  uint32_t reserved_0[6];
  uint32_t reserved_1[6];
  uint32_t reserved;
  uint32_t crc32;
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

extern const struct bootheader_s g_bl616cldg_fw_header;

#endif /* __VENDOR_BOUFFALOLAB_BOARDS_BL616CL_BL616CLDG_SRC_BL616CLDG_FW_HEADER_H */
