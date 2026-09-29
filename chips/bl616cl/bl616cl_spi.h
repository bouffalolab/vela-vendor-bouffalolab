/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_spi.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SPI_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SPI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nuttx/spi/spi.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bl616cl_spi_board_ops_s
{
  bool (*select)(void *arg, uint32_t devid, bool selected);
};

enum bl616cl_spi_test_feature_e
{
  BL616CL_SPI_TEST_FREQUENCY = 0,
  BL616CL_SPI_TEST_MODE,
  BL616CL_SPI_TEST_BITS,
  BL616CL_SPI_TEST_BITORDER,
};

#ifdef CONFIG_BL616CL_SPI_TEST
struct bl616cl_spi_test_ops_s
{
  bool (*select)(void *arg, uint32_t devid, bool selected);
  int (*feature)(void *arg, enum bl616cl_spi_test_feature_e feature,
                 uint32_t value);
  int (*exchange)(void *arg, const void *txbuffer, void *rxbuffer,
                  size_t nbytes);
  void (*recover)(void *arg);
};

struct bl616cl_spi_test_diag_s
{
  int last_error;
  uint32_t error_count;
  uint32_t actual_frequency;
  uint8_t mode;
  uint8_t nbits;
  bool lsbfirst;
};
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

struct spi_dev_s *bl616cl_spibus_initialize(
  int port, const struct bl616cl_spi_board_ops_s *board_ops,
  void *board_arg);
int bl616cl_spibus_uninitialize(struct spi_dev_s *dev);
int bl616cl_spi_configure_pins(int port, uint8_t miso_pin,
                               uint8_t mosi_pin);

#ifdef CONFIG_BL616CL_SPI_TEST
int bl616cl_spi_test_install(int port,
                             const struct bl616cl_spi_test_ops_s *ops,
                             void *arg);
int bl616cl_spi_test_initialize(int port);
int bl616cl_spi_test_addref(int port);
struct spi_dev_s *bl616cl_spi_test_device(int port);
int bl616cl_spi_test_get_diag(int port,
                              struct bl616cl_spi_test_diag_s *diag);
#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SPI_H */
