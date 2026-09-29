/****************************************************************************
 * vendor/bouffalolab/chips/bl616cl/bl616cl_serial.c
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

#include "bl616cl_lhal.h"
#include "bflb_clock.h"
#include "bflb_uart.h"
#include "bl616cl_uart.h"
#include "bl616cl_lowputc.h"
#include "hardware/uart_reg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if defined(CONFIG_BL616CL_UART1) && !defined(CONFIG_UART0_SERIAL_CONSOLE)
#  error "BL616CL UART1 requires UART0 as the serial console"
#endif

#if defined(CONFIG_BL616CL_UART1) && \
    (defined(CONFIG_UART1_SERIAL_CONSOLE) || \
     defined(CONFIG_UART1_IFLOWCONTROL) || \
     defined(CONFIG_UART1_OFLOWCONTROL) || defined(CONFIG_UART1_RXDMA) || \
     defined(CONFIG_UART1_TXDMA))
#  error "BL616CL UART1 console, flow control, and DMA are not supported"
#endif

#ifdef CONFIG_UART0_SERIAL_CONSOLE
#  define CONSOLE_DEV g_uart0port
#endif

#ifdef CONFIG_BL616CL_UART0
#  define TTYS0_DEV g_uart0port
#endif

#define BL616CL_UART_DIVISOR_LIMIT 0xffff

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

#ifdef CONFIG_BL616CL_UART1
static char g_uart1rxbuffer[CONFIG_UART1_RXBUFSIZE];
static char g_uart1txbuffer[CONFIG_UART1_TXBUFSIZE];

static uart_dev_t g_uart1port =
{
  .isconsole = false,
  .recv =
  {
    .size   = CONFIG_UART1_RXBUFSIZE,
    .buffer = g_uart1rxbuffer,
  },
  .xmit =
  {
    .size   = CONFIG_UART1_TXBUFSIZE,
    .buffer = g_uart1txbuffer,
  },
  .ops  = &g_uart_ops,
  .priv = &g_uart1_config,
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
/****************************************************************************
 * Name: bl616cl_disableuartint
 *
 * Description:
 *   Mask the TX, RX and error interrupts of the UART.
 *
 * Input Parameters:
 *   dev - An instance of the internal, device-specific UART structure.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_disableuartint(struct uart_dev_s *dev)
{
  struct bl616cl_uart_s *priv = dev->priv;

  bflb_uart_txint_mask(priv->device, true);
  bflb_uart_rxint_mask(priv->device, true);
  bflb_uart_errint_mask(priv->device, true);
}

#ifdef CONFIG_SERIAL_TERMIOS
/****************************************************************************
 * Name: bl616cl_apply_termios
 *
 * Description:
 *   Apply the baud rate, data bits, stop bits and parity of config to the
 *   UART with bflb_uart_feature_control(), stopping at the first failure.
 *
 * Input Parameters:
 *   priv - UART whose hardware is programmed.
 *   config - Settings to apply.
 *
 * Returned Value:
 *   Zero on success; the first negative value returned by
 *   bflb_uart_feature_control() on failure.
 *
 ****************************************************************************/

static int bl616cl_apply_termios(struct bl616cl_uart_s *priv,
                                const struct bl616cl_uart_s *config)
{
  int ret;

  ret = bflb_uart_feature_control(priv->device, UART_CMD_SET_BAUD_RATE,
                                  config->baud);
  if (ret < 0)
    {
      return ret;
    }

  ret = bflb_uart_feature_control(priv->device, UART_CMD_SET_DATA_BITS,
                                  config->data_bits - 5);
  if (ret < 0)
    {
      return ret;
    }

  ret = bflb_uart_feature_control(priv->device, UART_CMD_SET_STOP_BITS,
                                  config->stop_b2 ? UART_STOP_BITS_2 :
                                                    UART_STOP_BITS_1);
  if (ret < 0)
    {
      return ret;
    }

  return bflb_uart_feature_control(priv->device, UART_CMD_SET_PARITY_BITS,
                                   config->parity);
}
#endif

/****************************************************************************
 * Name: bl616cl_validate_baud
 *
 * Description:
 *   Check that a baud rate can be generated from the current UART peripheral
 *   clock. The rate is rejected if it is zero, if the clock is zero, if twice
 *   the rate is not below the clock, or if the rounded divisor is zero or not
 *   below BL616CL_UART_DIVISOR_LIMIT.
 *
 * Input Parameters:
 *   priv - UART whose peripheral clock is used.
 *   baud - Requested baud rate.
 *
 * Returned Value:
 *   Zero (OK) if the rate is usable; -EINVAL otherwise.
 *
 ****************************************************************************/

static int bl616cl_validate_baud(struct bl616cl_uart_s *priv, uint32_t baud)
{
  uint64_t scaled;
  uint32_t clock;
  uint32_t divisor;

  if (baud == 0)
    {
      return -EINVAL;
    }

  clock = bflb_clk_get_peripheral_clock(BFLB_DEVICE_TYPE_UART, priv->id);
  if (clock == 0)
    {
      return -EINVAL;
    }

  if ((uint64_t)baud * 2 >= clock)
    {
      return -EINVAL;
    }

  scaled = (uint64_t)clock * 10 / baud;
  divisor = (uint32_t)((scaled + 5) / 10);
  if (divisor == 0 || divisor >= BL616CL_UART_DIVISOR_LIMIT)
    {
      return -EINVAL;
    }

  return OK;
}

/****************************************************************************
 * Name: bl616cl_validate_format
 *
 * Description:
 *   Check the frame format of the UART: 5 to 8 data bits, parity value at
 *   most 2 and stop bit setting at most 1.
 *
 * Input Parameters:
 *   priv - UART whose configured format is checked.
 *
 * Returned Value:
 *   Zero (OK) if the format is valid; -EINVAL otherwise.
 *
 ****************************************************************************/

static int bl616cl_validate_format(const struct bl616cl_uart_s *priv)
{
  if (priv->data_bits < 5 || priv->data_bits > 8 || priv->parity > 2 ||
      priv->stop_b2 > 1)
    {
      return -EINVAL;
    }

  return OK;
}

/****************************************************************************
 * Name: bl616cl_setup
 *
 * Description:
 *   Implement the uart_ops_s setup method. Validate the configured baud rate
 *   and format, configure the UART with bl616cl_lowputc_config() and mask all
 *   UART interrupts.
 *
 * Input Parameters:
 *   dev - An instance of the internal, device-specific UART structure.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -EINVAL - The baud rate or frame format is invalid.
 *     -ENODEV - No lhal UART device is available.
 *     Other errors are returned from bl616cl_lowputc_config().
 *
 ****************************************************************************/

static int bl616cl_setup(struct uart_dev_s *dev)
{
  struct bl616cl_uart_s *priv = dev->priv;
  int ret;

  ret = bl616cl_validate_baud(priv, priv->baud);
  if (ret < 0)
    {
      return ret;
    }

  ret = bl616cl_validate_format(priv);
  if (ret < 0)
    {
      return ret;
    }

  ret = bl616cl_lowputc_config(priv);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->device == NULL)
    {
      return -ENODEV;
    }

  bl616cl_disableuartint(dev);
  return OK;
}

/****************************************************************************
 * Name: bl616cl_shutdown
 *
 * Description:
 *   Implement the uart_ops_s shutdown method. Mask all UART interrupts and
 *   disable the UART. Does nothing if the UART was never set up.
 *
 * Input Parameters:
 *   dev - An instance of the internal, device-specific UART structure.
 *
 * Returned Value:
 *   None.
 *
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
 *
 * Description:
 *   Implement the uart_ops_s attach method. Attach bl616cl_interrupt() to the
 *   UART interrupt and enable it if the attach succeeds.
 *
 * Input Parameters:
 *   dev - An instance of the internal, device-specific UART structure.
 *
 * Returned Value:
 *   Zero (OK) on success; the negated errno value from irq_attach() on
 *   failure.
 *
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
 *
 * Description:
 *   Implement the uart_ops_s detach method. Disable and detach the UART
 *   interrupt.
 *
 * Input Parameters:
 *   dev - An instance of the internal, device-specific UART structure.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_detach(struct uart_dev_s *dev)
{
  struct bl616cl_uart_s *priv = dev->priv;

  up_disable_irq(priv->irq);
  irq_detach(priv->irq);
}

/****************************************************************************
 * Name: bl616cl_interrupt
 *
 * Description:
 *   UART interrupt handler. Transmit pending characters on a TX FIFO
 *   interrupt with uart_xmitchars(), and receive characters on an RX FIFO or
 *   receive timeout interrupt with uart_recvchars(). The receive timeout flag
 *   is cleared.
 *
 * Input Parameters:
 *   irq - IRQ number (unused).
 *   context - Interrupt register state save area (unused).
 *   arg - The uart_dev_s instance that was passed to irq_attach().
 *
 * Returned Value:
 *   Always OK.
 *
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
 *
 * Description:
 *   Implement the uart_ops_s ioctl method. Supported commands are
 *   TIOCSERGSTRUCT (copy of the UART state, with
 *   CONFIG_SERIAL_TIOCSERGSTRUCT), and TCGETS and TCSETS (with
 *   CONFIG_SERIAL_TERMIOS). TCSETS validates the baud rate, applies the new
 *   format and rolls back to the old one if that fails; hardware flow control
 *   is not supported.
 *
 * Input Parameters:
 *   filep - File structure of the serial device.
 *   cmd - The ioctl command.
 *   arg - The ioctl argument.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure:
 *
 *     -EINVAL     - A required pointer is NULL, or the baud rate or data
 *                   size is invalid.
 *     -EOPNOTSUPP - CRTSCTS was requested in TCSETS.
 *     -ENOTTY     - The command is not supported.
 *     Other errors are returned from bflb_uart_feature_control().
 *
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
              struct bl616cl_uart_s snapshot;
              irqstate_t flags = uart_spinlock(dev, true);

              snapshot = *(struct bl616cl_uart_s *)dev->priv;
              uart_spinunlock(dev, true, flags);
              memcpy(user, &snapshot, sizeof(snapshot));
            }
        }
        break;
#endif

#ifdef CONFIG_SERIAL_TERMIOS
      case TCGETS:
        {
          struct termios *termiosp = (struct termios *)arg;
          struct bl616cl_uart_s *priv = dev->priv;
          struct bl616cl_uart_s snapshot;
          irqstate_t flags;

          if (termiosp == NULL)
            {
              ret = -EINVAL;
              break;
            }

          flags = uart_spinlock(dev, true);
          snapshot = *priv;
          uart_spinunlock(dev, true, flags);

          termiosp->c_cflag = ((snapshot.parity != 0) ? PARENB : 0) |
                              ((snapshot.parity == 1) ? PARODD : 0);
          termiosp->c_cflag |= snapshot.stop_b2 ? CSTOPB : 0;

          switch (snapshot.data_bits)
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

          cfsetispeed(termiosp, snapshot.baud);
        }
        break;

      case TCSETS:
        {
          struct termios *termiosp = (struct termios *)arg;
          struct bl616cl_uart_s *priv = dev->priv;
          struct bl616cl_uart_s candidate;
          irqstate_t flags;
          uint32_t baud;
          uint8_t bits = 8;
          uint8_t parity;
          uint8_t stop2;

          if (termiosp == NULL)
            {
              ret = -EINVAL;
              break;
            }

          if ((termiosp->c_cflag & CRTSCTS) != 0)
            {
              ret = -EOPNOTSUPP;
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

          ret = bl616cl_validate_baud(priv, baud);
          if (ret < 0)
            {
              break;
            }

          parity = ((termiosp->c_cflag & PARENB) != 0) ?
                   ((termiosp->c_cflag & PARODD) != 0 ? 1 : 2) : 0;
          stop2 = ((termiosp->c_cflag & CSTOPB) != 0) ? 1 : 0;

          flags = uart_spinlock(dev, true);
          candidate = *priv;
          candidate.baud = baud;
          candidate.parity = parity;
          candidate.data_bits = bits;
          candidate.stop_b2 = stop2;

          ret = bl616cl_apply_termios(priv, &candidate);
          if (ret < 0)
            {
              int rollback;

              rollback = bl616cl_apply_termios(priv, priv);
              if (rollback < 0)
                {
                  ret = rollback;
                }
            }
          else
            {
              *priv = candidate;
            }

          uart_spinunlock(dev, true, flags);
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
 *
 * Description:
 *   Implement the uart_ops_s receive method. Read one character from the
 *   UART; the reported status is always 0.
 *
 * Input Parameters:
 *   dev - An instance of the internal, device-specific UART structure.
 *   status - Location to return the receive status, may be NULL.
 *
 * Returned Value:
 *   The received character.
 *
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
 *
 * Description:
 *   Implement the uart_ops_s rxint method. Unmask or mask the RX interrupt.
 *
 * Input Parameters:
 *   dev - An instance of the internal, device-specific UART structure.
 *   enable - true to enable the RX interrupt, false to disable it.
 *
 * Returned Value:
 *   None.
 *
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
 *
 * Description:
 *   Implement the uart_ops_s rxavailable method.
 *
 * Input Parameters:
 *   dev - An instance of the internal, device-specific UART structure.
 *
 * Returned Value:
 *   true if the UART has received data available; false otherwise.
 *
 ****************************************************************************/

static bool bl616cl_rxavailable(struct uart_dev_s *dev)
{
  struct bl616cl_uart_s *priv = dev->priv;

  return bflb_uart_rxavailable(priv->device);
}

/****************************************************************************
 * Name: bl616cl_send
 *
 * Description:
 *   Implement the uart_ops_s send method. Write one character to the UART.
 *
 * Input Parameters:
 *   dev - An instance of the internal, device-specific UART structure.
 *   ch - Character to send.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_send(struct uart_dev_s *dev, int ch)
{
  struct bl616cl_uart_s *priv = dev->priv;

  bflb_uart_putchar(priv->device, ch);
}

/****************************************************************************
 * Name: bl616cl_txint
 *
 * Description:
 *   Implement the uart_ops_s txint method. Enabling unmasks the TX interrupt
 *   and immediately sends pending characters with uart_xmitchars(); disabling
 *   masks it.
 *
 * Input Parameters:
 *   dev - An instance of the internal, device-specific UART structure.
 *   enable - true to enable the TX interrupt, false to disable it.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl616cl_txint(struct uart_dev_s *dev, bool enable)
{
  struct bl616cl_uart_s *priv = dev->priv;
  irqstate_t flags;

  flags = enter_critical_section();
  if (enable)
    {
      bflb_uart_txint_mask(priv->device, false);
      uart_xmitchars(dev);
    }
  else
    {
      bflb_uart_txint_mask(priv->device, true);
    }

  leave_critical_section(flags);
}

/****************************************************************************
 * Name: bl616cl_txready
 *
 * Description:
 *   Implement the uart_ops_s txready method.
 *
 * Input Parameters:
 *   dev - An instance of the internal, device-specific UART structure.
 *
 * Returned Value:
 *   true if the UART can accept another character; false otherwise.
 *
 ****************************************************************************/

static bool bl616cl_txready(struct uart_dev_s *dev)
{
  struct bl616cl_uart_s *priv = dev->priv;

  return bflb_uart_txready(priv->device);
}

/****************************************************************************
 * Name: bl616cl_txempty
 *
 * Description:
 *   Implement the uart_ops_s txempty method. The transmitter is empty when
 *   the TX FIFO is empty and the TX bus is not busy.
 *
 * Input Parameters:
 *   dev - An instance of the internal, device-specific UART structure.
 *
 * Returned Value:
 *   true if all transmission has completed; false otherwise.
 *
 ****************************************************************************/

static bool bl616cl_txempty(struct uart_dev_s *dev)
{
  struct bl616cl_uart_s *priv = dev->priv;

  return bflb_uart_txempty(priv->device) &&
         (getreg32(priv->device->reg_base + UART_STATUS_OFFSET) &
          UART_STS_UTX_BUS_BUSY) == 0;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: riscv_earlyserialinit
 *
 * Description:
 *   Initialize the console UART for early output. When USE_EARLYSERIALINIT is
 *   defined and a console device exists, mark it as the console and set it up
 *   with bl616cl_setup().
 *
 *   This is the RISC-V architecture hook for early serial initialization.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
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
 *
 * Description:
 *   Register the serial devices: /dev/console (when UART0 is the console) and
 *   /dev/ttyS0 (when CONFIG_BL616CL_UART0 is set).
 *
 *   This is the RISC-V architecture hook for serial driver registration.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
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

#ifdef CONFIG_BL616CL_UART1
/****************************************************************************
 * Name: bl616cl_uart1_register
 *
 * Description:
 *   Set the TX and RX pins of UART1 and register it as /dev/ttyS1.
 *
 * Input Parameters:
 *   txpin - GPIO pin used for UART1 TX.
 *   rxpin - GPIO pin used for UART1 RX.
 *
 * Returned Value:
 *   The result of uart_register(): zero (OK) on success; a negated errno
 *   value on failure.
 *
 ****************************************************************************/

int bl616cl_uart1_register(uint8_t txpin, uint8_t rxpin)
{
  g_uart1_config.txpin = txpin;
  g_uart1_config.rxpin = rxpin;
  return uart_register("/dev/ttyS1", &g_uart1port);
}
#endif

/****************************************************************************
 * Name: up_putc
 *
 * Description:
 *   Implement the NuttX up_putc() interface. Output one character on the
 *   serial console with riscv_lowputc(), sending a carriage return before
 *   each newline. Does nothing without HAVE_SERIAL_CONSOLE.
 *
 * Input Parameters:
 *   ch - Character to output.
 *
 * Returned Value:
 *   None.
 *
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
