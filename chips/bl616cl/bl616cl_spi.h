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

/****************************************************************************
 * Name: bl616cl_spibus_initialize
 *
 * Description:
 *   Initialize an SPI port as a master and return its device instance. The
 *   first call enables the clocks and sets 400 kHz, mode 0, 8-bit, MSB
 *   first; later calls with the same board ops and argument only take a
 *   reference.
 *
 * Input Parameters:
 *   port - SPI port number (0 or 1).
 *   board_ops - Board callbacks, including chip select.
 *   board_arg - Argument passed to the board callbacks.
 *
 * Returned Value:
 *   A pointer to the SPI device instance, or NULL if the port is invalid,
 *   board_ops or its select callback is NULL, the arguments differ from the
 *   first initialization, the reference count is exhausted, or the device or
 *   clock setup fails.
 *
 ****************************************************************************/

struct spi_dev_s *bl616cl_spibus_initialize(
  int port, const struct bl616cl_spi_board_ops_s *board_ops,
  void *board_arg);

/****************************************************************************
 * Name: bl616cl_spibus_uninitialize
 *
 * Description:
 *   Release a reference to an SPI port. On the last reference, deselect any
 *   selected device, deinitialize the controller, disable the clocks and
 *   clear the board and test hooks.
 *
 * Input Parameters:
 *   dev - Pointer to the SPI device instance.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -EINVAL if dev
 *   is not a known device or has no reference.
 *
 ****************************************************************************/

int bl616cl_spibus_uninitialize(struct spi_dev_s *dev);

/****************************************************************************
 * Name: bl616cl_spi_configure_pins
 *
 * Description:
 *   Select which pins carry MISO and MOSI by programming the GLB SPI signal
 *   swap for the port. Each pin must be a valid SPI data pin (pin number
 *   modulo 4 is 2 or 3), and MISO and MOSI must not share the same function.
 *
 * Input Parameters:
 *   port - SPI port number (0 or 1).
 *   miso_pin - GPIO number for MISO (0-36).
 *   mosi_pin - GPIO number for MOSI (0-36).
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -EINVAL for an
 *   invalid port or pin combination; -EIO if the GLB configuration fails.
 *
 ****************************************************************************/

int bl616cl_spi_configure_pins(int port, uint8_t miso_pin,
                               uint8_t mosi_pin);

#ifdef CONFIG_BL616CL_SPI_TEST

/****************************************************************************
 * Name: bl616cl_spi_test_install
 *
 * Description:
 *   Test helper: install or remove (ops is NULL) the test hooks that replace
 *   the hardware transport of an initialized port.
 *
 * Input Parameters:
 *   port - SPI port number (0 or 1).
 *   ops - Test hooks; all callbacks must be set, or NULL to remove.
 *   arg - Argument passed to the test hooks.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -EINVAL for an
 *   invalid port or incomplete ops; -ENODEV if the port is not initialized.
 *
 ****************************************************************************/

int bl616cl_spi_test_install(int port,
                             const struct bl616cl_spi_test_ops_s *ops,
                             void *arg);

/****************************************************************************
 * Name: bl616cl_spi_test_initialize
 *
 * Description:
 *   Test helper: initialize a port using a board select callback that
 *   rejects every request.
 *
 * Input Parameters:
 *   port - SPI port number (0 or 1).
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -ENODEV if
 *   initialization fails.
 *
 ****************************************************************************/

int bl616cl_spi_test_initialize(int port);

/****************************************************************************
 * Name: bl616cl_spi_test_addref
 *
 * Description:
 *   Test helper: add a reference to an already initialized port.
 *
 * Input Parameters:
 *   port - SPI port number (0 or 1).
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -ENODEV if the
 *   port is invalid, not initialized, or the reference count is exhausted.
 *
 ****************************************************************************/

int bl616cl_spi_test_addref(int port);

/****************************************************************************
 * Name: bl616cl_spi_test_device
 *
 * Description:
 *   Test helper: get the SPI device instance of an initialized port without
 *   taking a reference.
 *
 * Input Parameters:
 *   port - SPI port number (0 or 1).
 *
 * Returned Value:
 *   A pointer to the SPI device instance, or NULL if the port is invalid or
 *   not initialized.
 *
 ****************************************************************************/

struct spi_dev_s *bl616cl_spi_test_device(int port);

/****************************************************************************
 * Name: bl616cl_spi_test_get_diag
 *
 * Description:
 *   Test helper: copy the last error, error count, actual frequency, mode,
 *   word width and bit order of a port into a diagnostics structure.
 *
 * Input Parameters:
 *   port - SPI port number (0 or 1).
 *   diag - Location to return the diagnostics.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure. -EINVAL for an
 *   invalid port or NULL diag; -ENODEV if the port is not initialized.
 *
 ****************************************************************************/

int bl616cl_spi_test_get_diag(int port,
                              struct bl616cl_spi_test_diag_s *diag);

#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_SPI_H */
