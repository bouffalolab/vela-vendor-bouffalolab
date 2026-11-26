/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_display_spi.c
 *
 * SPI controller bus backend of the display framework (components/display).
 * The logic is the SDK's bus/spi/display_spi.c; only the platform services
 * differ: the controller and the DMA channel are the fixed ones of Kconfig
 * instead of a device name and dma_mgr, DMA goes through the NuttX DMA API
 * and its completion callback takes the place of the DMA interrupt.
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <nuttx/dma/dma.h>

#include <arch/chip/bl616cl_dma.h>

#include "bl616cl_sdk.h"
#include "bl616cl_glb.h"
#include "bflb_clock.h"
#include "bflb_dma.h"
#include "bflb_gpio.h"
#include "bflb_name.h"
#include "bflb_peri.h"
#include "bflb_spi.h"
#include "bl616cl_display_spi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The core hands out &bus->base and this backend casts it back */

static_assert(offsetof(display_spi_bus_t, base) == 0,
              "display_bus_t must be the first member");

#if !SPI_FIFO_WIDTH_VARIABLE_SUPPORT
#  error "The display SPI bus needs a SPI controller with variable FIFO width"
#endif

#define SPI_FREQ_MAX       40000000u /* 80 MHz peripheral clock, divider 2 */
#define SPI_PARAM_MAX      4096u
#define DMA_LLI_ITEMS      4064u     /* transfer units per LLI node */
#define DMA_WIDTH_UNKNOWN  0xffu
#define SPI_FIFO_THRESHOLD (2 * 4 - 1) /* 2 bursts of INCR4, minus one */

/* The controller of the bus */

#ifdef CONFIG_BL616CL_DISPLAY_SPI_PORT1
#  define DISPLAY_SPI_NAME        BFLB_NAME_SPI1
#  define DISPLAY_SPI_PERIPHERAL  BFLB_PERIPHERAL_SPI1
#  define DISPLAY_SPI_GPIO_FUNC   GPIO_FUNC_SPI1
#  define DISPLAY_SPI_DMA_REQUEST DMA_REQUEST_SPI1_TX
#  define DISPLAY_SPI_DMA_ADDR    DMA_ADDR_SPI1_TDR
#  define display_spi_set_clk     GLB_Set_SPI1_CLK
#  define display_spi_swap_mosi   GLB_Swap_MCU_SPI_1_MOSI_With_MISO
#elif defined(CONFIG_BL616CL_DISPLAY_SPI_PORT0)
#  define DISPLAY_SPI_NAME        BFLB_NAME_SPI0
#  define DISPLAY_SPI_PERIPHERAL  BFLB_PERIPHERAL_SPI0
#  define DISPLAY_SPI_GPIO_FUNC   GPIO_FUNC_SPI0
#  define DISPLAY_SPI_DMA_REQUEST DMA_REQUEST_SPI0_TX
#  define DISPLAY_SPI_DMA_ADDR    DMA_ADDR_SPI0_TDR
#  define display_spi_set_clk     GLB_Set_SPI0_CLK
#  define display_spi_swap_mosi   GLB_Swap_MCU_SPI_0_MOSI_With_MISO
#else
#  error "No SPI controller is selected for the display SPI bus"
#endif

/* The largest draw takes one LLI node per 4064 units after the first */

#ifdef CONFIG_BL616CL_DISPLAY_SPI_DMA
static_assert(CONFIG_BL616CL_DMA0_LLI_NODES + 1 >=
              CONFIG_BL616CL_DISPLAY_SPI_DMA_LLI_COUNT,
              "BL616CL_DMA0_LLI_NODES cannot hold a display SPI draw of "
              "BL616CL_DISPLAY_SPI_DMA_LLI_COUNT nodes");
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Get the SPI bus from the core object (base is the first member) */

static display_spi_bus_t *to_spi_bus(display_bus_t *bus)
{
  return (display_spi_bus_t *)bus;
}

/* Set the SPI frame width (8 bit for commands, 16/24 bit for pixels) */

static void spi_set_width(display_spi_bus_t *bus, uint32_t width)
{
  bflb_spi_feature_control(bus->spi, SPI_CMD_SET_DATA_WIDTH, width);
}

#ifdef CONFIG_BL616CL_DISPLAY_SPI_DMA
/* Program the DMA channel width (bytes) when the pixel format changes */

static int spi_dma_config(display_spi_bus_t *bus, uint8_t width)
{
  struct dma_config_s dma_cfg;
  int ret;

  if (bus->dma_width == width)
    {
      return 0; /* already programmed */
    }

  memset(&dma_cfg, 0, sizeof(dma_cfg));
  dma_cfg.direction = DMA_MEM_TO_DEV;
  dma_cfg.src_width = width;
  dma_cfg.dst_width = width;
  dma_cfg.src_step = width;
  dma_cfg.dst_drq = DISPLAY_SPI_DMA_REQUEST;
  dma_cfg.option = BL616CL_DMA_OPTION_BURST_INCR4;

  ret = DMA_CONFIG(bus->dma, &dma_cfg);
  if (ret < 0)
    {
      bus->dma_width = DMA_WIDTH_UNKNOWN;
      return ret;
    }

  bus->dma_width = width;
  return 0;
}

/* DMA completion, the SDK's DMA interrupt. The FIFO still holds data when
 * the DMA finishes: wait until the wire is idle. Vela: the NuttX DMA API
 * also reports a DMA error here, which completes the transfer with it; as
 * on the SDK, only an asynchronous draw sees that error.
 */

static void spi_dma_done(struct dma_chan_s *chan, void *arg, ssize_t len)
{
  display_spi_bus_t *bus = arg;

  while (bflb_spi_isbusy(bus->spi))
    {
    }

  bflb_spi_link_txdma(bus->spi, false);
  bflb_spi_feature_control(bus->spi, SPI_CMD_CLEAR_RX_FIFO, 0);
  spi_set_width(bus, SPI_DATA_WIDTH_8BIT);
  display_bus_complete(&bus->base, len < 0 ? (int)len : 0);
}
#endif

/****************************************************************************
 * Bus operations
 ****************************************************************************/

/* Bus op init: clock and configure the controller, take the DMA channel
 * and mux the board pins.
 */

static int spi_init(display_bus_t *base)
{
  display_spi_bus_t *bus = to_spi_bus(base);
  struct bflb_spi_config_s cfg =
    {
      .freq = DISPLAY_IFACE_DEFAULT_HZ(DISPLAY_IFACE_DBI_C4),
      .role = SPI_ROLE_MASTER,
      .mode = CONFIG_BL616CL_DISPLAY_SPI_MODE,
      .data_width = SPI_DATA_WIDTH_8BIT,
      .bit_order = SPI_BIT_MSB,
      .byte_order = SPI_BYTE_MSB, /* 16/24-bit frames high byte first */
      .tx_fifo_threshold = SPI_FIFO_THRESHOLD,
      .rx_fifo_threshold = SPI_FIFO_THRESHOLD,
    };

  if (!display_pin_used(&bus->cfg.dc))
    {
      return -EINVAL;
    }

  bus->spi = bflb_device_get_by_name(DISPLAY_SPI_NAME);
  if (!bus->spi)
    {
      return -ENODEV;
    }

  /* Clock and MOSI/MISO assignment of the SDK boards (board_init) */

  display_spi_set_clk(ENABLE, GLB_SPI_CLK_MCU_MUXPLL_160M, 0);
  bflb_peripheral_clock_control(DISPLAY_SPI_PERIPHERAL, true);
  display_spi_swap_mosi(DISABLE);

  bflb_spi_init(bus->spi, &cfg);
  display_pin_output(&bus->cfg.dc, true);

  bus->dma = NULL;
  bus->dma_width = DMA_WIDTH_UNKNOWN;
#ifdef CONFIG_BL616CL_DISPLAY_SPI_DMA
  if (bl616cl_dma0_device())
    {
      bus->dma = DMA_GET_CHAN(bl616cl_dma0_device(),
                              CONFIG_BL616CL_DISPLAY_SPI_TX_DMA_CHANNEL);
    }

  if (!bus->dma || spi_dma_config(bus, 4) < 0)
    {
      if (bus->dma)
        {
          DMA_PUT_CHAN(bl616cl_dma0_device(), bus->dma);
          bus->dma = NULL;
        }

      bflb_spi_deinit(bus->spi);
      return -ENODEV;
    }
#endif

  if (bus->cfg.func_init)
    {
      bus->cfg.func_init();
    }

  return 0;
}

/* Bus op deinit: stop the DMA, release the controller and the board pins */

static void spi_deinit(display_bus_t *base)
{
  display_spi_bus_t *bus = to_spi_bus(base);

#ifdef CONFIG_BL616CL_DISPLAY_SPI_DMA
  if (bus->dma)
    {
      bflb_spi_link_txdma(bus->spi, false);
      DMA_STOP(bus->dma);
      DMA_PUT_CHAN(bl616cl_dma0_device(), bus->dma);
      bus->dma = NULL;
    }
#endif

  bflb_spi_deinit(bus->spi);
  if (bus->cfg.func_deinit)
    {
      bus->cfg.func_deinit();
    }

  bus->exclusive = false;
}

/* Bus op attach: only the 4-wire interface, RGB565 and XRGB8888 */

static int spi_attach(display_bus_t *base, display_io_t *io)
{
  display_spi_bus_t *bus = to_spi_bus(base);
  const display_io_cfg_t *cfg = &io->cfg;
  int ret;

  if (cfg->iface != DISPLAY_IFACE_DBI_C4)
    {
      return -ENOTSUP;
    }

  ret = display_bus_claim(base, &bus->exclusive, cfg);
  if (ret)
    {
      return ret;
    }

  io->caps = (display_io_caps_t)
    {
      /* RGB565 is sent as 16 bit; XRGB8888 as 3 bytes, i.e. 24 bit */

      .flags = DISPLAY_IO_CAP_FIXED_WIRE |
               (bus->dma ? DISPLAY_IO_CAP_DMA : 0),
      .format_mask = DISPLAY_FMT_BIT(DISPLAY_FMT_RGB565) |
                     DISPLAY_FMT_BIT(DISPLAY_FMT_XRGB8888),
      .max_param_bytes = SPI_PARAM_MAX,

      /* Conservative: LLI nodes counted at the smallest unit (16 bit);
       * 32-bit XRGB8888 units fit anyway.
       */

#ifdef CONFIG_BL616CL_DISPLAY_SPI_DMA
      .max_color_bytes = bus->dma ?
                         CONFIG_BL616CL_DISPLAY_SPI_DMA_LLI_COUNT *
                         DMA_LLI_ITEMS * 2u : UINT32_MAX,
#else
      .max_color_bytes = UINT32_MAX,
#endif
      .color_align = 4,
    };

  return 0;
}

/* Bus op apply: restore clock, mode and byte order for an endpoint */

static int spi_apply(display_bus_t *base, display_io_t *io)
{
  display_spi_bus_t *bus = to_spi_bus(base);
  uint32_t freq = display_io_get_freq(io);

  /* A borrower may have changed any of these: restore everything this
   * backend depends on.
   */

  bflb_spi_feature_control(bus->spi, SPI_CMD_SET_FREQ,
                           freq < SPI_FREQ_MAX ? freq : SPI_FREQ_MAX);
  bflb_spi_feature_control(bus->spi, SPI_CMD_SET_MODE,
                           CONFIG_BL616CL_DISPLAY_SPI_MODE);
  bflb_spi_feature_control(bus->spi, SPI_CMD_SET_BIT_ORDER, SPI_BIT_MSB);
  bflb_spi_feature_control(bus->spi, SPI_CMD_SET_BYTE_ORDER, SPI_BYTE_MSB);
  spi_set_width(bus, SPI_DATA_WIDTH_8BIT);
  return 0;
}

/* Command phase: DC at command level, the command byte as an 8-bit frame,
 * then DC at data level.
 */

static void spi_send_cmd(display_spi_bus_t *bus, uint32_t cmd)
{
  if (cmd != DISPLAY_IO_NO_CMD)
    {
      display_pin_set(&bus->cfg.dc, false);
      bflb_spi_poll_send(bus->spi, cmd & 0xff);
    }

  display_pin_set(&bus->cfg.dc, true);
}

/* Bus op write: send one message by polling; pixels use a wider frame,
 * 8 bit is restored after.
 */

static int spi_write(display_bus_t *base, display_io_t *io,
                     const display_io_msg_t *msg)
{
  display_spi_bus_t *bus = to_spi_bus(base);

  if (msg->flags & DISPLAY_IO_MSG_READ)
    {
      return -ENOTSUP;
    }

  spi_send_cmd(bus, msg->cmd);
  if (!msg->len)
    {
      return 0;
    }

  if (!(msg->flags & DISPLAY_IO_MSG_COLOR))
    {
      bflb_spi_poll_exchange(bus->spi, msg->tx, NULL, msg->len);
      return 0;
    }

  if (io->cfg.format == DISPLAY_FMT_RGB565)
    {
      spi_set_width(bus, SPI_DATA_WIDTH_16BIT);
      bflb_spi_poll_exchange(bus->spi, msg->tx, NULL, msg->len);
    }
  else
    {
      /* XRGB8888: one 24-bit frame per pixel */

      const uint32_t *px = msg->tx;
      uint32_t i;

      spi_set_width(bus, SPI_DATA_WIDTH_24BIT);
      for (i = 0; i < msg->len / 4u; i++)
        {
          bflb_spi_poll_send(bus->spi, px[i] & 0x00ffffffu);
        }
    }

  spi_set_width(bus, SPI_DATA_WIDTH_8BIT);
  return 0;
}

#ifdef CONFIG_BL616CL_DISPLAY_SPI_DMA
/* Bus op write_dma: send the command, then let the DMA feed the TX FIFO.
 * Completion is reported by spi_dma_done().
 */

static int spi_write_dma(display_bus_t *base, display_io_t *io,
                         const display_io_msg_t *msg)
{
  display_spi_bus_t *bus = to_spi_bus(base);
  bool rgb565 = io->cfg.format == DISPLAY_FMT_RGB565;
  uint8_t width = rgb565 ? 2 : 4;
  int ret;

  ret = spi_dma_config(bus, width);
  if (ret < 0)
    {
      return ret;
    }

  /* The SDK's LLI reload fails here for a draw longer than the nodes */

  if (msg->len > CONFIG_BL616CL_DISPLAY_SPI_DMA_LLI_COUNT * DMA_LLI_ITEMS *
                 width)
    {
      return -E2BIG;
    }

  /* The command goes out by polling before the DMA takes over the FIFO.
   * DMA_START loads and starts the channel in one call; if it fails, undo
   * the request link and the frame width so that nothing completes.
   */

  spi_send_cmd(bus, msg->cmd);
  spi_set_width(bus, rgb565 ? SPI_DATA_WIDTH_16BIT : SPI_DATA_WIDTH_24BIT);
  bflb_spi_link_txdma(bus->spi, true);
  ret = DMA_START(bus->dma, spi_dma_done, bus, DISPLAY_SPI_DMA_ADDR,
                  (uintptr_t)msg->tx, msg->len);
  if (ret < 0)
    {
      bflb_spi_link_txdma(bus->spi, false);
      spi_set_width(bus, SPI_DATA_WIDTH_8BIT);
      return ret;
    }

  return 0;
}
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

const display_bus_ops_t display_spi_bus_ops =
{
  .init = spi_init,
  .deinit = spi_deinit,
  .attach = spi_attach,
  .apply = spi_apply,
  .write = spi_write,
#ifdef CONFIG_BL616CL_DISPLAY_SPI_DMA
  .write_dma = spi_write_dma,
#endif
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bl616cl_display_spi_gpio_init(uint8_t sck, uint8_t mosi)
{
  struct bflb_device_s *gpio = bflb_device_get_by_name(BFLB_NAME_GPIO);

  bflb_gpio_init(gpio, mosi, DISPLAY_SPI_GPIO_FUNC | GPIO_ALTERNATE |
                 GPIO_PULLDOWN | GPIO_SMT_EN | GPIO_DRV_1);
  bflb_gpio_init(gpio, sck, DISPLAY_SPI_GPIO_FUNC | GPIO_ALTERNATE |
                 GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1);
}
