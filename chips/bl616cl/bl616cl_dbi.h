/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_dbi.h
 *
 * DBI controller bus backend of the display framework (components/display),
 * the BL616CL port of the SDK's bus/dbi/display_dbi.h.
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_DBI_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_DBI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stddef.h>
#include <stdint.h>

#include "bus/display_bus_driver.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* List of the interfaces of a DBI bus, with the board pin functions of
 * each, as on the SDK:
 *
 *   .ifaces = DISPLAY_DBI_IFACES({ DISPLAY_IFACE_DBI_B8, b8_init, NULL })
 */

#define DISPLAY_DBI_IFACES(...) \
  ((const display_dbi_iface_t[]){ __VA_ARGS__, \
                                  { DISPLAY_IFACE_NONE, NULL, NULL } })

/* Static definition of the DBI bus. The controller is DBI0; DMA0 feeds the
 * pixels when CONFIG_BL616CL_DBI_DMA is set, on the channel of the DMA0
 * channel assignment menu, with CONFIG_BL616CL_DBI_DMA_LLI_COUNT nodes
 * worth of pixels per draw.
 */

#define DISPLAY_DBI_BUS(...) \
  { .base = DISPLAY_BUS_BASE(&display_dbi_bus_ops), .cfg = { __VA_ARGS__ } }

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bflb_device_s;
struct dma_chan_s;

/* One interface the DBI bus is set up for, with the board functions that
 * mux its pins.
 */

typedef struct
{
  display_iface_t iface;     /* Interface; DISPLAY_IFACE_NONE ends the list */
  void (*func_init)(void);   /* Board: set the pins of this interface */
  void (*func_deinit)(void); /* Board: release the pins. Optional */
} display_dbi_iface_t;

/* DBI bus configuration */

typedef struct
{
  const display_dbi_iface_t *ifaces; /* Interfaces, see DISPLAY_DBI_IFACES */
} display_dbi_bus_cfg_t;

/* DBI bus object */

typedef struct
{
  display_bus_t base;        /* Bus core, must be first */
  display_dbi_bus_cfg_t cfg; /* Configuration */
  struct bflb_device_s *dbi; /* Runtime: controller */
  struct dma_chan_s *dma;    /* Runtime: DMA channel, NULL without DMA */
  uint8_t fifo_threshold;    /* Runtime: TX FIFO threshold of the burst */
  uint8_t iface;             /* Runtime: interface of the pins */
  uint8_t qspi_lines;        /* Runtime: QSPI wire layout, 0xff: unknown */
  bool exclusive;            /* Runtime: an endpoint without CS owns it */
} display_dbi_bus_t;

/****************************************************************************
 * Public Data
 ****************************************************************************/

extern const display_bus_ops_t display_dbi_bus_ops;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_dbi_gpio_init
 *
 * Description:
 *   Mux pins to the DBI function of an interface, for the board pin
 *   functions of a DBI bus (display_dbi_iface_t.func_init).
 *
 * Input Parameters:
 *   iface - Interface: DISPLAY_IFACE_DBI_B8, _DBI_C3, _DBI_C4 or _QSPI.
 *   pins  - GPIO numbers of the interface signals.
 *   npins - Number of pins.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void bl616cl_dbi_gpio_init(display_iface_t iface, const uint8_t *pins,
                           size_t npins);

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL616CL_BL616CL_DBI_H */
