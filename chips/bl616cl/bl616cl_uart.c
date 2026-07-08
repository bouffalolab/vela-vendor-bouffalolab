/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_uart.c
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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/serial/serial.h>

#include "chip.h"
#include "riscv_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define UART_UTX_CONFIG_OFFSET 0x00
#define UART_URX_CONFIG_OFFSET 0x04
#define UART_BIT_PRD_OFFSET 0x08
#define UART_INT_MASK_OFFSET 0x24
#define UART_INT_CLEAR_OFFSET 0x28
#define UART_FIFO_CONFIG_0_OFFSET 0x80
#define UART_FIFO_CONFIG_1_OFFSET 0x84
#define UART_FIFO_WDATA_OFFSET 0x88
#define UART_FIFO_RDATA_OFFSET 0x8c

#define UART_CR_UTX_EN (1 << 0)
#define UART_CR_UTX_FRM_EN (1 << 2)
#define UART_CR_UTX_BIT_CNT_D_SHIFT 8
#define UART_CR_UTX_BIT_CNT_P_SHIFT 11
#define UART_CR_URX_EN (1 << 0)
#define UART_CR_URX_BIT_CNT_D_SHIFT 8
#define UART_TX_FIFO_CLR (1 << 2)
#define UART_RX_FIFO_CLR (1 << 3)
#define UART_TX_FIFO_CNT_SHIFT 0
#define UART_TX_FIFO_CNT_MASK (0x3f << UART_TX_FIFO_CNT_SHIFT)
#define UART_RX_FIFO_CNT_SHIFT 8
#define UART_RX_FIFO_CNT_MASK (0x3f << UART_RX_FIFO_CNT_SHIFT)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl616cl_uart_s {
    uintptr_t base;
    uint32_t baud;
    int irq;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bl616cl_setup(struct uart_dev_s* dev);
static void bl616cl_shutdown(struct uart_dev_s* dev);
static int bl616cl_attach(struct uart_dev_s* dev);
static void bl616cl_detach(struct uart_dev_s* dev);
static int bl616cl_ioctl(struct file* filep, int cmd, unsigned long arg);
static int bl616cl_receive(struct uart_dev_s* dev, unsigned int* status);
static void bl616cl_rxint(struct uart_dev_s* dev, bool enable);
static bool bl616cl_rxavailable(struct uart_dev_s* dev);
static void bl616cl_send(struct uart_dev_s* dev, int ch);
static void bl616cl_txint(struct uart_dev_s* dev, bool enable);
static bool bl616cl_txready(struct uart_dev_s* dev);
static bool bl616cl_txempty(struct uart_dev_s* dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bl616cl_uart_s g_uart0priv = {
    .base = BL616CL_UART0_BASE,
    .baud = CONFIG_UART0_BAUD,
    .irq = BL616CL_IRQ_UART0,
};

static char g_uart0rxbuffer[CONFIG_UART0_RXBUFSIZE];
static char g_uart0txbuffer[CONFIG_UART0_TXBUFSIZE];

static const struct uart_ops_s g_uart_ops = {
    .setup = bl616cl_setup,
    .shutdown = bl616cl_shutdown,
    .attach = bl616cl_attach,
    .detach = bl616cl_detach,
    .ioctl = bl616cl_ioctl,
    .receive = bl616cl_receive,
    .rxint = bl616cl_rxint,
    .rxavailable = bl616cl_rxavailable,
    .send = bl616cl_send,
    .txint = bl616cl_txint,
    .txready = bl616cl_txready,
    .txempty = bl616cl_txempty,
};

static uart_dev_t g_uart0port = {
    .recv = {
        .size = CONFIG_UART0_RXBUFSIZE,
        .buffer = g_uart0rxbuffer,
    },
    .xmit = {
        .size = CONFIG_UART0_TXBUFSIZE,
        .buffer = g_uart0txbuffer,
    },
    .ops = &g_uart_ops,
    .priv = &g_uart0priv,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t bl616cl_serialin(struct bl616cl_uart_s* priv, int offset)
{
    return getreg32(priv->base + offset);
}

static void bl616cl_serialout(struct bl616cl_uart_s* priv, int offset,
    uint32_t value)
{
    putreg32(value, priv->base + offset);
}

static unsigned int bl616cl_txfifo_count(struct bl616cl_uart_s* priv)
{
    return (bl616cl_serialin(priv, UART_FIFO_CONFIG_1_OFFSET) & UART_TX_FIFO_CNT_MASK) >> UART_TX_FIFO_CNT_SHIFT;
}

static unsigned int bl616cl_rxfifo_count(struct bl616cl_uart_s* priv)
{
    return (bl616cl_serialin(priv, UART_FIFO_CONFIG_1_OFFSET) & UART_RX_FIFO_CNT_MASK) >> UART_RX_FIFO_CNT_SHIFT;
}

static void bl616cl_lowsetup(void)
{
    struct bl616cl_uart_s* priv = &g_uart0priv;
    uint32_t divisor = BL616CL_UART_CLOCK / priv->baud;
    uint32_t tx_cfg;
    uint32_t rx_cfg;

    if (divisor == 0) {
        divisor = 1;
    }

    tx_cfg = ((8 + 4) << UART_CR_UTX_BIT_CNT_D_SHIFT) | (1 << UART_CR_UTX_BIT_CNT_P_SHIFT) | UART_CR_UTX_FRM_EN;
    rx_cfg = ((8 + 4) << UART_CR_URX_BIT_CNT_D_SHIFT);

    bl616cl_serialout(priv, UART_UTX_CONFIG_OFFSET, 0);
    bl616cl_serialout(priv, UART_URX_CONFIG_OFFSET, 0);
    bl616cl_serialout(priv, UART_BIT_PRD_OFFSET,
        ((divisor - 1) << 16) | (divisor - 1));
    bl616cl_serialout(priv, UART_FIFO_CONFIG_0_OFFSET,
        UART_TX_FIFO_CLR | UART_RX_FIFO_CLR);
    bl616cl_serialout(priv, UART_INT_MASK_OFFSET, 0xffffffff);
    bl616cl_serialout(priv, UART_INT_CLEAR_OFFSET, 0xffffffff);
    bl616cl_serialout(priv, UART_UTX_CONFIG_OFFSET, tx_cfg | UART_CR_UTX_EN);
    bl616cl_serialout(priv, UART_URX_CONFIG_OFFSET, rx_cfg | UART_CR_URX_EN);
}

static int bl616cl_setup(struct uart_dev_s* dev)
{
    UNUSED(dev);
    bl616cl_lowsetup();
    return OK;
}

static void bl616cl_shutdown(struct uart_dev_s* dev)
{
    UNUSED(dev);
}

static int bl616cl_attach(struct uart_dev_s* dev)
{
    UNUSED(dev);
    return OK;
}

static void bl616cl_detach(struct uart_dev_s* dev)
{
    UNUSED(dev);
}

static int bl616cl_ioctl(struct file* filep, int cmd, unsigned long arg)
{
    UNUSED(filep);
    UNUSED(cmd);
    UNUSED(arg);
    return -ENOTTY;
}

static int bl616cl_receive(struct uart_dev_s* dev, unsigned int* status)
{
    struct bl616cl_uart_s* priv = dev->priv;

    *status = 0;
    return bl616cl_serialin(priv, UART_FIFO_RDATA_OFFSET) & 0xff;
}

static void bl616cl_rxint(struct uart_dev_s* dev, bool enable)
{
    UNUSED(dev);
    UNUSED(enable);
}

static bool bl616cl_rxavailable(struct uart_dev_s* dev)
{
    struct bl616cl_uart_s* priv = dev->priv;

    return bl616cl_rxfifo_count(priv) != 0;
}

static void bl616cl_send(struct uart_dev_s* dev, int ch)
{
    struct bl616cl_uart_s* priv = dev->priv;

    bl616cl_serialout(priv, UART_FIFO_WDATA_OFFSET, (uint32_t)ch & 0xff);
}

static void bl616cl_txint(struct uart_dev_s* dev, bool enable)
{
    UNUSED(dev);
    UNUSED(enable);
}

static bool bl616cl_txready(struct uart_dev_s* dev)
{
    struct bl616cl_uart_s* priv = dev->priv;

    return bl616cl_txfifo_count(priv) < BL616CL_UART_TXFIFO_SIZE;
}

static bool bl616cl_txempty(struct uart_dev_s* dev)
{
    struct bl616cl_uart_s* priv = dev->priv;

    return bl616cl_txfifo_count(priv) == 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: riscv_earlyserialinit
 ****************************************************************************/

void riscv_earlyserialinit(void)
{
    bl616cl_lowsetup();
}

/****************************************************************************
 * Name: riscv_serialinit
 ****************************************************************************/

void riscv_serialinit(void)
{
    uart_register("/dev/console", &g_uart0port);
    uart_register("/dev/ttyS0", &g_uart0port);
}

/****************************************************************************
 * Name: up_putc
 ****************************************************************************/

void up_putc(int ch)
{
    while (!bl616cl_txready(&g_uart0port))
        ;
    bl616cl_send(&g_uart0port, ch);
}
