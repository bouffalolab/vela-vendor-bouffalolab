/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_serial.c
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

#include <debug.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#ifdef CONFIG_SERIAL_TERMIOS
#include <termios.h>
#endif

#include <nuttx/arch.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/irq.h>
#include <nuttx/serial/serial.h>

#include "riscv_internal.h"

#include "bflb_uart.h"
#include "bl616cl_lowputc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_UART0_SERIAL_CONSOLE
#  define CONSOLE_DEV g_uart0port
#endif

#ifdef CONFIG_BL616CL_UART0
#  define TTYS0_DEV g_uart0port
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

#ifdef HAVE_UART_DEVICE
static int bl616cl_setup(struct uart_dev_s *dev);
static void bl616cl_shutdown(struct uart_dev_s *dev);
static int bl616cl_attach(struct uart_dev_s *dev);
static void bl616cl_detach(struct uart_dev_s *dev);
static int bl616cl_interrupt(int irq, void *context, FAR void *arg);
static int bl616cl_ioctl(struct file *filep, int cmd, unsigned long arg);
static int bl616cl_receive(struct uart_dev_s *dev, unsigned int *status);
static void bl616cl_rxint(struct uart_dev_s *dev, bool enable);
static bool bl616cl_rxavailable(struct uart_dev_s *dev);
static void bl616cl_send(struct uart_dev_s *dev, int ch);
static void bl616cl_txint(struct uart_dev_s *dev, bool enable);
static bool bl616cl_txready(struct uart_dev_s *dev);
static bool bl616cl_txempty(struct uart_dev_s *dev);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef HAVE_UART_DEVICE
static const struct uart_ops_s g_uart_ops =
{
  .setup       = bl616cl_setup,
  .shutdown    = bl616cl_shutdown,
  .attach      = bl616cl_attach,
  .detach      = bl616cl_detach,
  .ioctl       = bl616cl_ioctl,
  .receive     = bl616cl_receive,
  .rxint       = bl616cl_rxint,
  .rxavailable = bl616cl_rxavailable,
  .send        = bl616cl_send,
  .txint       = bl616cl_txint,
  .txready     = bl616cl_txready,
  .txempty     = bl616cl_txempty,
};

#ifdef CONFIG_BL616CL_UART0
static char g_uart0rxbuffer[CONFIG_UART0_RXBUFSIZE];
static char g_uart0txbuffer[CONFIG_UART0_TXBUFSIZE];

static uart_dev_t g_uart0port =
{
#ifdef CONFIG_UART0_SERIAL_CONSOLE
  .isconsole = true,
#else
  .isconsole = false,
#endif
  .recv =
  {
    .size   = CONFIG_UART0_RXBUFSIZE,
    .buffer = g_uart0rxbuffer,
  },
  .xmit =
  {
    .size   = CONFIG_UART0_TXBUFSIZE,
    .buffer = g_uart0txbuffer,
  },
  .ops  = &g_uart_ops,
  .priv = &g_uart0_config,
#ifdef CONFIG_SERIAL_TERMIOS
  .minrecv = 1,
#endif
};
#endif
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef HAVE_UART_DEVICE
static void bl616cl_disableuartint(struct uart_dev_s *dev)
{
  struct bl616cl_uart_s *priv = dev->priv;

  bflb_uart_txint_mask(priv->device, true);
  bflb_uart_rxint_mask(priv->device, true);
  bflb_uart_errint_mask(priv->device, true);
}

/****************************************************************************
 * Name: bl616cl_setup
 ****************************************************************************/

static int bl616cl_setup(struct uart_dev_s *dev)
{
  struct bl616cl_uart_s *priv = dev->priv;

  bl616cl_lowputc_config(priv);
  if (priv->device == NULL)
    {
      return -ENODEV;
    }

  bl616cl_disableuartint(dev);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_shutdown
 ****************************************************************************/

static void bl616cl_shutdown(struct uart_dev_s *dev)
{
  struct bl616cl_uart_s *priv = dev->priv;

  if (priv->device == NULL)
    {
      return;
    }

  bl616cl_disableuartint(dev);
  bflb_uart_disable(priv->device);
}

/****************************************************************************
 * Name: bl616cl_attach
 ****************************************************************************/

static int bl616cl_attach(struct uart_dev_s *dev)
{
  struct bl616cl_uart_s *priv = dev->priv;
  int ret;

  ret = irq_attach(priv->irq, bl616cl_interrupt, dev);
  if (ret == OK)
    {
      up_enable_irq(priv->irq);
    }

  return ret;
}

/****************************************************************************
 * Name: bl616cl_detach
 ****************************************************************************/

static void bl616cl_detach(struct uart_dev_s *dev)
{
  struct bl616cl_uart_s *priv = dev->priv;

  up_disable_irq(priv->irq);
  irq_detach(priv->irq);
}

/****************************************************************************
 * Name: bl616cl_interrupt
 ****************************************************************************/

static int bl616cl_interrupt(int irq, void *context, FAR void *arg)
{
  struct uart_dev_s *dev = arg;
  struct bl616cl_uart_s *priv;
  uint32_t intstatus;

  UNUSED(irq);
  UNUSED(context);
  DEBUGASSERT(dev != NULL && dev->priv != NULL);

  priv = dev->priv;
  intstatus = bflb_uart_get_intstatus(priv->device);

  if ((intstatus & UART_INTSTS_TX_FIFO) != 0)
    {
      uart_xmitchars(dev);
      bflb_uart_txint_mask(priv->device, true);
    }

  if ((intstatus & (UART_INTSTS_RX_FIFO | UART_INTSTS_RTO)) != 0)
    {
      uart_recvchars(dev);

      if ((intstatus & UART_INTSTS_RTO) != 0)
        {
          bflb_uart_int_clear(priv->device, UART_INTCLR_RTO);
        }
    }

  return OK;
}

/****************************************************************************
 * Name: bl616cl_ioctl
 ****************************************************************************/

static int bl616cl_ioctl(struct file *filep, int cmd, unsigned long arg)
{
#if defined(CONFIG_SERIAL_TERMIOS) || defined(CONFIG_SERIAL_TIOCSERGSTRUCT)
  struct inode *inode = filep->f_inode;
  struct uart_dev_s *dev = inode->i_private;
#endif
  int ret = OK;

  switch (cmd)
    {
#ifdef CONFIG_SERIAL_TIOCSERGSTRUCT
      case TIOCSERGSTRUCT:
        {
          struct bl616cl_uart_s *user = (struct bl616cl_uart_s *)arg;

          if (user == NULL)
            {
              ret = -EINVAL;
            }
          else
            {
              memcpy(user, dev->priv, sizeof(struct bl616cl_uart_s));
            }
        }
        break;
#endif

#ifdef CONFIG_SERIAL_TERMIOS
      case TCGETS:
        {
          struct termios *termiosp = (struct termios *)arg;
          struct bl616cl_uart_s *priv = dev->priv;

          if (termiosp == NULL)
            {
              ret = -EINVAL;
              break;
            }

          termiosp->c_cflag = ((priv->parity != 0) ? PARENB : 0) |
                              ((priv->parity == 1) ? PARODD : 0);
          termiosp->c_cflag |= priv->stop_b2 ? CSTOPB : 0;

          switch (priv->data_bits)
            {
              case 5:
                termiosp->c_cflag |= CS5;
                break;

              case 6:
                termiosp->c_cflag |= CS6;
                break;

              case 7:
                termiosp->c_cflag |= CS7;
                break;

              default:
                termiosp->c_cflag |= CS8;
                break;
            }

          cfsetispeed(termiosp, priv->baud);
        }
        break;

      case TCSETS:
        {
          struct termios *termiosp = (struct termios *)arg;
          struct bl616cl_uart_s *priv = dev->priv;
          uint32_t baud;
          uint8_t bits = 8;
          uint8_t parity;
          uint8_t stop2;

          if (termiosp == NULL)
            {
              ret = -EINVAL;
              break;
            }

          baud = cfgetispeed(termiosp);

          switch (termiosp->c_cflag & CSIZE)
            {
              case CS5:
                bits = 5;
                break;

              case CS6:
                bits = 6;
                break;

              case CS7:
                bits = 7;
                break;

              case CS8:
                bits = 8;
                break;

              default:
                ret = -EINVAL;
                break;
            }

          if (ret != OK)
            {
              break;
            }

          parity = ((termiosp->c_cflag & PARENB) != 0) ?
                   ((termiosp->c_cflag & PARODD) != 0 ? 1 : 2) : 0;
          stop2 = ((termiosp->c_cflag & CSTOPB) != 0) ? 1 : 0;

          priv->baud = baud;
          priv->parity = parity;
          priv->data_bits = bits;
          priv->stop_b2 = stop2;

          bflb_uart_disable(priv->device);
          bl616cl_lowputc_config(priv);
        }
        break;
#endif

      default:
        ret = -ENOTTY;
        break;
    }

  return ret;
}

/****************************************************************************
 * Name: bl616cl_receive
 ****************************************************************************/

static int bl616cl_receive(struct uart_dev_s *dev, unsigned int *status)
{
  struct bl616cl_uart_s *priv = dev->priv;

  if (status != NULL)
    {
      *status = 0;
    }

  return bflb_uart_getchar(priv->device);
}

/****************************************************************************
 * Name: bl616cl_rxint
 ****************************************************************************/

static void bl616cl_rxint(struct uart_dev_s *dev, bool enable)
{
  struct bl616cl_uart_s *priv = dev->priv;
  irqstate_t flags;

  flags = enter_critical_section();
  bflb_uart_rxint_mask(priv->device, !enable);
  leave_critical_section(flags);
}

/****************************************************************************
 * Name: bl616cl_rxavailable
 ****************************************************************************/

static bool bl616cl_rxavailable(struct uart_dev_s *dev)
{
  struct bl616cl_uart_s *priv = dev->priv;

  return bflb_uart_rxavailable(priv->device);
}

/****************************************************************************
 * Name: bl616cl_send
 ****************************************************************************/

static void bl616cl_send(struct uart_dev_s *dev, int ch)
{
  struct bl616cl_uart_s *priv = dev->priv;

  bflb_uart_putchar(priv->device, ch);
}

/****************************************************************************
 * Name: bl616cl_txint
 ****************************************************************************/

static void bl616cl_txint(struct uart_dev_s *dev, bool enable)
{
  struct bl616cl_uart_s *priv = dev->priv;
  irqstate_t flags;

  flags = enter_critical_section();
  bflb_uart_txint_mask(priv->device, !enable);
  leave_critical_section(flags);
}

/****************************************************************************
 * Name: bl616cl_txready
 ****************************************************************************/

static bool bl616cl_txready(struct uart_dev_s *dev)
{
  struct bl616cl_uart_s *priv = dev->priv;

  return bflb_uart_txready(priv->device);
}

/****************************************************************************
 * Name: bl616cl_txempty
 ****************************************************************************/

static bool bl616cl_txempty(struct uart_dev_s *dev)
{
  struct bl616cl_uart_s *priv = dev->priv;

  return bflb_uart_txempty(priv->device);
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: riscv_earlyserialinit
 ****************************************************************************/

void riscv_earlyserialinit(void)
{
#if defined(USE_EARLYSERIALINIT) && defined(CONSOLE_DEV)
  CONSOLE_DEV.isconsole = true;
  bl616cl_setup(&CONSOLE_DEV);
#endif
}

/****************************************************************************
 * Name: riscv_serialinit
 ****************************************************************************/

void riscv_serialinit(void)
{
#ifdef CONSOLE_DEV
  uart_register("/dev/console", &CONSOLE_DEV);
#endif

#ifdef TTYS0_DEV
  uart_register("/dev/ttyS0", &TTYS0_DEV);
#endif
}

/****************************************************************************
 * Name: up_putc
 ****************************************************************************/

void up_putc(int ch)
{
#ifdef HAVE_SERIAL_CONSOLE
  if (ch == '\n')
    {
      riscv_lowputc('\r');
    }

  riscv_lowputc(ch);
#else
  UNUSED(ch);
#endif
}
