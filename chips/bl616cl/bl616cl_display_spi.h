/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_display_spi.h
 *
 * SPI controller bus backend of the display framework (components/display),
 * the BL616CL port of the SDK's bus/spi/display_spi.h.
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 ****************************************************************************/

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_DISPLAY_SPI_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_DISPLAY_SPI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include "bus/display_bus_driver.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Static definition of the SPI bus, as on the SDK. The bus implements
 * DISPLAY_IFACE_DBI_C4 (SCK, MOSI and a GPIO DC) with RGB565 sent as
 * 16-bit frames and XRGB8888 as 24-bit frames. The controller is the one
 * chosen by CONFIG_BL616CL_DISPLAY_SPI_PORT0/1 and belongs to the display.
 * DMA0 feeds the pixels when CONFIG_BL616CL_DISPLAY_SPI_DMA is set, on the
 * channel of the DMA0 channel assignment menu, with
 * CONFIG_BL616CL_DISPLAY_SPI_DMA_LLI_COUNT nodes worth of pixels per draw.
 * CS is a GPIO of the endpoint, driven by the bus core.
 */

#define DISPLAY_SPI_BUS(...) \
  { .base = DISPLAY_BUS_BASE(&display_spi_bus_ops), .cfg = { __VA_ARGS__ } }

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bflb_device_s;
struct dma_chan_s;

/* SPI bus configuration */

typedef struct
{
  display_pin_t dc;          /* Required; "active" means data phase,
                              * usually DISPLAY_PIN_HIGH(n) */
  void (*func_init)(void);   /* Board: mux SCK and MOSI when the first
                              * endpoint is attached. Optional */
  void (*func_deinit)(void); /* Board: release the pins when the last
                              * endpoint leaves. Optional */
} display_spi_bus_cfg_t;

/* SPI bus object */

typedef struct
{
  display_bus_t base;        /* Bus core, must be first */
  display_spi_bus_cfg_t cfg; /* Configuration */
  struct bflb_device_s *spi; /* Runtime: controller */
  struct dma_chan_s *dma;    /* Runtime: DMA channel, NULL without DMA */
  uint8_t dma_width;         /* Runtime: DMA unit in bytes, 0xff: none */
  bool exclusive;            /* Runtime: an endpoint without CS owns it */
} display_spi_bus_t;

/****************************************************************************
 * Public Data
 ****************************************************************************/

extern const display_bus_ops_t display_spi_bus_ops;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_display_spi_gpio_init
 *
 * Description:
 *   Mux the clock and data pins to the SPI controller of the display bus,
 *   for the board pin function of the bus (display_spi_bus_cfg_t.
 *   func_init). Pull and drive strength are the SDK board's.
 *
 * Input Parameters:
 *   sck  - GPIO number of the clock.
 *   mosi - GPIO number of the data output.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void bl616cl_display_spi_gpio_init(uint8_t sck, uint8_t mosi);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_DISPLAY_SPI_H */
