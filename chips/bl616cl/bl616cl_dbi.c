/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_dbi.c
 *
 * DBI controller bus backend of the display framework (components/display).
 * The logic is the SDK's bus/dbi/display_dbi.c; only the platform services
 * differ: the DMA channel is the fixed one of the DMA0 channel assignment
 * menu instead of dma_mgr, DMA goes through the NuttX DMA API and the
 * interrupt through irq_attach().
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

#include <nuttx/irq.h>
#include <nuttx/dma/dma.h>

#include <arch/chip/bl616cl_dma.h>

#include "bl616cl_sdk.h"
#include "bl616cl_glb.h"
#include "bflb_dbi.h"
#include "bflb_dma.h"
#include "bflb_gpio.h"
#include "bflb_name.h"
#include "bl616cl_dbi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The core hands out &bus->base and this backend casts it back */

static_assert(offsetof(display_dbi_bus_t, base) == 0,
              "display_bus_t must be the first member");

#define DBI_CLK_MAX_B8          54000000u
#define DBI_CLK_MAX_SERIAL      80000000u

#define DBI_PARAM_MAX           255u         /* data length of send_cmd_data */
#define DBI_READ_MAX            8u           /* DBI_READ_DATA_BYTE_MAX */
#define DBI_PIXEL_MAX           0x00ffffffu  /* 24-bit pixel counter */
#define DMA_LLI_BYTES           (4064u * 4u) /* one LLI node, 32-bit units */
#define QSPI_ADDR_BYTES         3u
#define QSPI_CODE_ADDR_MASK     0x00ffffffu  /* address template, 23..0 */
#define QSPI_CODE_OPCODE_SHIFT  24u          /* opcode in 31..24 */
#define QSPI_ADDR_CMD_SHIFT     8u           /* DCS command to bits 15..8 */
#define QSPI_LINES_NONE         0xffu        /* wire layout not known */

/* TX FIFO threshold (entries) matching the DMA burst: INCR4 -> 3,
 * INCR8 -> 7. An INCR8 burst of words is 32 bytes; the DMA0 channel FIFO
 * is 64 bytes on CH0..CH1, 32 bytes on CH2..CH3 and 16 bytes on CH4..CH7.
 */

#define DBI_FIFO_THRESHOLD_INCR4  3
#define DBI_FIFO_THRESHOLD_INCR8  7
#define DBI_DMA_BIG_FIFO_CHANNELS 4

/* The largest draw takes one LLI node per 16256 bytes after the first */

#ifdef CONFIG_BL616CL_DBI_DMA
static_assert(CONFIG_BL616CL_DMA0_LLI_NODES + 1 >=
              CONFIG_BL616CL_DBI_DMA_LLI_COUNT,
              "BL616CL_DMA0_LLI_NODES cannot hold a DBI draw of "
              "BL616CL_DBI_DMA_LLI_COUNT nodes");
#endif

#if defined(DBI_QSPI_SUPPORT) && DBI_QSPI_SUPPORT
#  define DBI_HAS_QSPI 1
#else
#  define DBI_HAS_QSPI 0
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Get the DBI bus from the core object (base is the first member) */

static display_dbi_bus_t *to_dbi_bus(display_bus_t *bus)
{
  return (display_dbi_bus_t *)bus;
}

/* Find an interface in the list of the bus */

static const display_dbi_iface_t *
dbi_find_iface(const display_dbi_bus_t *bus, display_iface_t iface)
{
  const display_dbi_iface_t *it = bus->cfg.ifaces;

  for (; it && it->iface != DISPLAY_IFACE_NONE; it++)
    {
      if (it->iface == iface)
        {
          return it;
        }
    }

  return NULL;
}

/* Switch the board pin functions from the active interface to another
 * one; DISPLAY_IFACE_NONE only releases the active one.
 */

static void dbi_switch_pins(display_dbi_bus_t *bus, display_iface_t iface)
{
  const display_dbi_iface_t *old =
    dbi_find_iface(bus, (display_iface_t)bus->iface);
  const display_dbi_iface_t *next = dbi_find_iface(bus, iface);

  if (old && old->func_deinit)
    {
      old->func_deinit();
    }

  if (next && next->func_init)
    {
      next->func_init();
    }

  bus->iface = (uint8_t)iface;
}

/* Map a memory format to the DBI FIFO input format */

static uint8_t dbi_input_format(display_format_t format)
{
  switch (format)
    {
      case DISPLAY_FMT_RGB888:
        return DBI_PIXEL_INPUT_FORMAT_RGB_888;

      case DISPLAY_FMT_XRGB8888:
        return DBI_PIXEL_INPUT_FORMAT_NRGB_8888;

#if defined(DBI_YUYV_SUPPORT) && DBI_YUYV_SUPPORT
      case DISPLAY_FMT_YUYV:
        return DBI_PIXEL_INPUT_FORMAT_YUYV;
#endif

      default:
        return DBI_PIXEL_INPUT_FORMAT_RGB_565;
    }
}

/* DBI interrupt: TC means the last pixel has left the pins */

static int dbi_isr(int irq, void *context, void *arg)
{
  display_dbi_bus_t *bus = arg;

  if (!(bflb_dbi_get_intstatus(bus->dbi) & DBI_INTSTS_TC))
    {
      return OK;
    }

  bflb_dbi_link_txdma(bus->dbi, false);
  bflb_dbi_int_clear(bus->dbi, DBI_INTCLR_TC);
  bflb_dbi_tcint_mask(bus->dbi, true);
#ifdef CONFIG_BL616CL_DBI_DMA
  DMA_STOP(bus->dma);
#endif
  display_bus_complete(&bus->base, 0);
  return OK;
}

/****************************************************************************
 * Bus operations
 ****************************************************************************/

/* Bus op init: look up the controller and DMA channel, install the TC
 * interrupt.
 */

static int dbi_init(display_bus_t *base)
{
  display_dbi_bus_t *bus = to_dbi_bus(base);
#ifdef CONFIG_BL616CL_DBI_DMA
  struct dma_dev_s *dma_dev = bl616cl_dma0_device();
  struct dma_config_s dma_cfg;
#endif

  if (!bus->cfg.ifaces || bus->cfg.ifaces->iface == DISPLAY_IFACE_NONE)
    {
      return -EINVAL;
    }

  bus->dbi = bflb_device_get_by_name(BFLB_NAME_DBI);
  if (!bus->dbi)
    {
      return -ENODEV;
    }

  /* DBI clock of the SDK boards: MCU MUXPLL 160 MHz, divided by the
   * controller.
   */

  GLB_Set_DBI_CLK(ENABLE, GLB_DBI_CLK_MCU_MUXPLL_160M, 0);

  bus->iface = DISPLAY_IFACE_NONE; /* pins are set up by the first apply */
  bus->dma = NULL;
  bus->fifo_threshold = DBI_FIFO_THRESHOLD_INCR4;
#ifdef CONFIG_BL616CL_DBI_DMA
  memset(&dma_cfg, 0, sizeof(dma_cfg));
  dma_cfg.direction = DMA_MEM_TO_DEV;
  dma_cfg.src_width = 4;
  dma_cfg.dst_width = 4;
  dma_cfg.src_step = 4;
  dma_cfg.dst_drq = DMA_REQUEST_DBI_TX;
  dma_cfg.option = BL616CL_DMA_OPTION_BURST_INCR4;
#  if CONFIG_BL616CL_DBI_TX_DMA_CHANNEL < DBI_DMA_BIG_FIFO_CHANNELS
  bus->fifo_threshold = DBI_FIFO_THRESHOLD_INCR8; /* larger FIFO: INCR8 */
  dma_cfg.option = BL616CL_DMA_OPTION_BURST_INCR8;
#  endif

  if (dma_dev)
    {
      bus->dma = DMA_GET_CHAN(dma_dev, CONFIG_BL616CL_DBI_TX_DMA_CHANNEL);
    }

  if (!bus->dma)
    {
      return -ENODEV;
    }

  if (DMA_CONFIG(bus->dma, &dma_cfg) < 0)
    {
      DMA_PUT_CHAN(dma_dev, bus->dma);
      bus->dma = NULL;
      return -ENODEV;
    }
#endif

  bflb_dbi_tcint_mask(bus->dbi, true);
  irq_attach(BL616CL_IRQ_NUM_DBI, dbi_isr, bus);
  up_enable_irq(BL616CL_IRQ_NUM_DBI);
  return 0;
}

/* Bus op deinit: stop the controller and DMA, release the board pins */

static void dbi_deinit(display_bus_t *base)
{
  display_dbi_bus_t *bus = to_dbi_bus(base);

  up_disable_irq(BL616CL_IRQ_NUM_DBI);
  irq_detach(BL616CL_IRQ_NUM_DBI);
  bflb_dbi_tcint_mask(bus->dbi, true);
#ifdef CONFIG_BL616CL_DBI_DMA
  if (bus->dma)
    {
      bflb_dbi_link_txdma(bus->dbi, false);
      DMA_STOP(bus->dma);
      DMA_PUT_CHAN(bl616cl_dma0_device(), bus->dma);
      bus->dma = NULL;
    }
#endif

  bflb_dbi_deinit(bus->dbi);
  dbi_switch_pins(bus, DISPLAY_IFACE_NONE);
  bus->exclusive = false;
}

/* Bus op attach: check the interface and report the capabilities */

static int dbi_attach(display_bus_t *base, display_io_t *io)
{
  display_dbi_bus_t *bus = to_dbi_bus(base);
  const display_io_cfg_t *cfg = &io->cfg;
  bool qspi = cfg->iface == DISPLAY_IFACE_QSPI;
  uint32_t mask = DISPLAY_FMT_BIT(DISPLAY_FMT_RGB565) |
                  DISPLAY_FMT_BIT(DISPLAY_FMT_RGB888) |
                  DISPLAY_FMT_BIT(DISPLAY_FMT_XRGB8888);
  int ret;

#if defined(DBI_YUYV_SUPPORT) && DBI_YUYV_SUPPORT
  mask |= DISPLAY_FMT_BIT(DISPLAY_FMT_YUYV);
#endif

  /* Only the interfaces this bus was set up for */

  if (!dbi_find_iface(bus, cfg->iface) || (qspi && !DBI_HAS_QSPI))
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
      .flags = (bus->dma ? DISPLAY_IO_CAP_DMA : 0) |
               (qspi ? 0 : DISPLAY_IO_CAP_READ),
      .format_mask = mask,
      .max_param_bytes = DBI_PARAM_MAX,
#ifdef CONFIG_BL616CL_DBI_DMA
      .max_color_bytes = bus->dma ?
                         CONFIG_BL616CL_DBI_DMA_LLI_COUNT * DMA_LLI_BYTES :
                         DBI_PIXEL_MAX,
#else
      .max_color_bytes = DBI_PIXEL_MAX,
#endif
      .color_align = 4, /* FIFO and DMA fetch 32-bit words */
    };

  return 0;
}

/* Bus op apply: reprogram the controller (mode, clock, pixel formats) for
 * an endpoint.
 */

static int dbi_apply(display_bus_t *base, display_io_t *io)
{
  display_dbi_bus_t *bus = to_dbi_bus(base);
  const display_io_cfg_t *cfg = &io->cfg;
  uint32_t limit = cfg->iface == DISPLAY_IFACE_DBI_B8 ?
                   DBI_CLK_MAX_B8 : DBI_CLK_MAX_SERIAL;
  uint32_t freq = display_io_get_freq(io);
  struct bflb_dbi_config_s dbi_cfg =
    {
      .pixel_input_format = dbi_input_format(cfg->format),
      .pixel_output_format = io->wire == DISPLAY_WIRE_16 ?
                             DBI_PIXEL_OUTPUT_FORMAT_RGB_565 :
                             DBI_PIXEL_OUTPUT_FORMAT_RGB_888,
      .clk_mode = CONFIG_BL616CL_DBI_CLK_MODE,
      .clk_freq_hz = freq < limit ? freq : limit,
      .tx_fifo_threshold = bus->fifo_threshold,
    };

  switch (cfg->iface)
    {
      case DISPLAY_IFACE_DBI_B8:
        dbi_cfg.dbi_mode = DBI_MODE_TYPE_B;
        break;

      case DISPLAY_IFACE_DBI_C4:
        dbi_cfg.dbi_mode = DBI_MODE_TYPE_C_4_WIRE;
        break;

      case DISPLAY_IFACE_DBI_C3:
        dbi_cfg.dbi_mode = DBI_MODE_TYPE_C_3_WIRE;
        break;

      default:
#if DBI_HAS_QSPI
        if (!io->link)
          {
            return -ENOTSUP; /* the QSPI protocol comes from the panel IC */
          }

        /* Wires are programmed per message by dbi_prepare_cmd() */

        dbi_cfg.dbi_mode = DBI_MODE_EX_QSPI;
        dbi_cfg.cmd_wire_mode = DBI_QSPI_WIRE_NUM_1;
        dbi_cfg.addr_wire_mode = DBI_QSPI_WIRE_NUM_1;
        dbi_cfg.data_wire_mode = DBI_QSPI_WIRE_NUM_1;
        break;
#else
        return -ENOTSUP;
#endif
    }

  if (bus->iface != cfg->iface)
    {
      dbi_switch_pins(bus, cfg->iface);
    }

  /* Until LHAL has per-field feature_control commands, reprogram the whole
   * controller. This only happens between transactions, with every
   * software CS inactive.
   */

  bflb_dbi_init(bus->dbi, &dbi_cfg);
  bus->qspi_lines = QSPI_LINES_NONE;

  /* Continuous mode (CS stays active across the words of a pixel
   * transfer). Without it the controller leaves a gap between the words:
   * the SDK measured 2554 us instead of 1474 us for 38400 bytes at 27 MHz
   * (Type B). It does not depend on how CS is driven.
   */

  bflb_dbi_feature_control(bus->dbi, DBI_CMD_CS_CONTINUE, true);
  bflb_dbi_tcint_mask(bus->dbi, true);
  return 0;
}

/* Command byte on the wire. QSPI: load the layout of the message kind
 * (parameters or pixels) from the panel IC: wires of the address and data
 * phases, and the 24-bit address with the DCS command in bits 15..8;
 * return the 1-wire opcode.
 */

static uint8_t dbi_prepare_cmd(display_dbi_bus_t *bus,
                               const display_io_t *io,
                               const display_io_msg_t *msg)
{
#if DBI_HAS_QSPI
  if (io->cfg.iface == DISPLAY_IFACE_QSPI)
    {
      /* apply() rejected a QSPI endpoint without link */

      const display_qspi_proto_t *proto = &io->link->qspi;
      const display_qspi_layout_t *layout =
        (msg->flags & DISPLAY_IO_MSG_COLOR) ? &proto->color : &proto->cmd;

      if (bus->qspi_lines != (uint8_t)layout->lines)
        {
          bflb_dbi_qspi_set_wire(bus->dbi, DBI_QSPI_WIRE_NUM_1,
                                 layout->lines == DISPLAY_QSPI_LINES_4_4 ?
                                 DBI_QSPI_WIRE_NUM_4 : DBI_QSPI_WIRE_NUM_1,
                                 layout->lines == DISPLAY_QSPI_LINES_1_1 ?
                                 DBI_QSPI_WIRE_NUM_1 : DBI_QSPI_WIRE_NUM_4);
          bus->qspi_lines = (uint8_t)layout->lines;
        }

      bflb_dbi_qspi_set_addr(bus->dbi, QSPI_ADDR_BYTES,
                             (layout->code & QSPI_CODE_ADDR_MASK) |
                             ((msg->cmd & 0xffu) << QSPI_ADDR_CMD_SHIFT));
      return (uint8_t)(layout->code >> QSPI_CODE_OPCODE_SHIFT);
    }
#else
  (void)bus;
  (void)io;
#endif

  return (uint8_t)msg->cmd;
}

/* Number of pixels of a color message */

static uint32_t dbi_pixel_count(const display_io_t *io,
                                const display_io_msg_t *msg)
{
  return msg->len / (uint32_t)display_format_bytes(io->cfg.format);
}

/* Bus op write: send one message with the CPU (FIFO), command phase
 * optional.
 */

static int dbi_write(display_bus_t *base, display_io_t *io,
                     const display_io_msg_t *msg)
{
  display_dbi_bus_t *bus = to_dbi_bus(base);
  bool no_cmd = msg->cmd == DISPLAY_IO_NO_CMD;
  uint8_t cmd;
  int ret;

  if (no_cmd && io->cfg.iface == DISPLAY_IFACE_QSPI)
    {
      return -ENOTSUP;
    }

  if ((msg->flags & DISPLAY_IO_MSG_READ) && msg->len > DBI_READ_MAX)
    {
      return -E2BIG;
    }

  cmd = no_cmd ? 0 : dbi_prepare_cmd(bus, io, msg);
  if (no_cmd)
    {
      bflb_dbi_feature_control(bus->dbi, DBI_CMD_MASK_CMD_PHASE, true);
    }

  if (msg->flags & DISPLAY_IO_MSG_READ)
    {
      ret = bflb_dbi_send_cmd_read_data(bus->dbi, cmd, (uint8_t)msg->len,
                                        msg->rx);
    }
  else if (msg->flags & DISPLAY_IO_MSG_COLOR)
    {
      uint32_t count = dbi_pixel_count(io, msg);

      ret = bflb_dbi_send_cmd_pixel(bus->dbi, cmd, count, (void *)msg->tx);
    }
  else
    {
      ret = bflb_dbi_send_cmd_data(bus->dbi, cmd, (uint8_t)msg->len,
                                   (uint8_t *)msg->tx);
    }

  if (no_cmd)
    {
      bflb_dbi_feature_control(bus->dbi, DBI_CMD_MASK_CMD_PHASE, false);
    }

  return ret < 0 ? -EIO : 0;
}

#ifdef CONFIG_BL616CL_DBI_DMA
/* Bus op write_dma: start the controller in pixel mode and let the DMA
 * feed its FIFO. Completion is reported by dbi_isr().
 */

static int dbi_write_dma(display_bus_t *base, display_io_t *io,
                         const display_io_msg_t *msg)
{
  display_dbi_bus_t *bus = to_dbi_bus(base);
  uint32_t count = dbi_pixel_count(io, msg);
  uint32_t size;
  int ret;

  if (msg->cmd == DISPLAY_IO_NO_CMD)
    {
      return -ENOTSUP;
    }

  /* Number of bytes the DMA has to feed (rounded up to 32-bit words) */

  size = (uint32_t)bflb_dbi_feature_control(bus->dbi,
                                            DBI_CMD_GET_SIZE_OF_PIXEL_CNT,
                                            count);
  if (size > CONFIG_BL616CL_DBI_DMA_LLI_COUNT * DMA_LLI_BYTES)
    {
      return -E2BIG;
    }

  /* Order: start the pixel command, unmask TC, link the DMA request, then
   * start the channel. DMA_START loads and starts the channel in one call;
   * if it fails, undo the request link and TC so that nothing completes,
   * and the next transfer reprograms the controller (the core forgets the
   * applied endpoint on error).
   */

  bflb_dbi_send_cmd_pixel(bus->dbi, dbi_prepare_cmd(bus, io, msg), count,
                          NULL); /* start only */
  bflb_dbi_tcint_mask(bus->dbi, false);
  bflb_dbi_link_txdma(bus->dbi, true);
  ret = DMA_START(bus->dma, NULL, NULL, DMA_ADDR_DBI_TDR,
                  (uintptr_t)msg->tx, size);
  if (ret < 0)
    {
      bflb_dbi_link_txdma(bus->dbi, false);
      bflb_dbi_tcint_mask(bus->dbi, true);
      return ret;
    }

  return 0;
}
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

const display_bus_ops_t display_dbi_bus_ops =
{
  .init = dbi_init,
  .deinit = dbi_deinit,
  .attach = dbi_attach,
  .apply = dbi_apply,
  .write = dbi_write,
#ifdef CONFIG_BL616CL_DBI_DMA
  .write_dma = dbi_write_dma,
#endif
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bl616cl_dbi_gpio_init(display_iface_t iface, const uint8_t *pins,
                           size_t npins)
{
  struct bflb_device_s *gpio = bflb_device_get_by_name(BFLB_NAME_GPIO);
  uint32_t func;
  size_t i;

  switch (iface)
    {
      case DISPLAY_IFACE_DBI_B8:
        func = GPIO_FUNC_DBI_B;
        break;

      case DISPLAY_IFACE_QSPI:
        func = GPIO_FUNC_DBI_QSPI;
        break;

      default:
        func = GPIO_FUNC_DBI_C;
        break;
    }

  for (i = 0; i < npins; i++)
    {
      bflb_gpio_init(gpio, pins[i], func | GPIO_ALTERNATE | GPIO_PULLUP |
                     GPIO_SMT_EN | GPIO_DRV_2);
    }
}
