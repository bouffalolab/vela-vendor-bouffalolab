/****************************************************************************
 * vendor/bouffalolab/chips/bl618dg/bl618dg_irq_adapter.c
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

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <arch/irq.h>

#include "bflb_irq.h"
#include "bl618dg_irq.h"
#include "hardware/bl618dg_core.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL618DG_CHECK_IRQ(name) \
  _Static_assert(BL618DG_IRQ_NUM_##name == \
                 RISCV_IRQ_ASYNC + BL618DG_IRQ_##name, \
                 "BL618DG SDK/NuttX IRQ mismatch: " #name)

/* Check the SDK AP map without importing its startup or vector table. */

_Static_assert(BL618DG_IRQ_NUM_FIRST ==
               RISCV_IRQ_ASYNC + BL618DG_IRQ_NUM_BASE,
               "BL618DG peripheral IRQ base mismatch");
_Static_assert(RISCV_IRQ_MTIMER ==
               RISCV_IRQ_ASYNC + BL618DG_IRQ_MTIME,
               "BL618DG timer IRQ mismatch");
_Static_assert(BL618DG_ECLIC_IRQ_COUNT == BL618DG_IRQ_AUDAC + 1,
               "BL618DG ECLIC slot count mismatch");
BL618DG_CHECK_IRQ(BMX_MCU_BUS_ERR);
BL618DG_CHECK_IRQ(MJDEC);
BL618DG_CHECK_IRQ(DISPLAY);
BL618DG_CHECK_IRQ(USB);
BL618DG_CHECK_IRQ(WIFI_TO_CPU);
BL618DG_CHECK_IRQ(RF_TOP_INT0);
BL618DG_CHECK_IRQ(RF_TOP_INT1);
BL618DG_CHECK_IRQ(SDIO);
BL618DG_CHECK_IRQ(WIFI_TBTT_SLEEP);
BL618DG_CHECK_IRQ(SEC_ENG_ID1_SHA_AES_TRNG_PKA_GMAC);
BL618DG_CHECK_IRQ(SEC_ENG_ID0_SHA_AES_TRNG_PKA_GMAC);
BL618DG_CHECK_IRQ(SEC_ENG_ID1_CDET);
BL618DG_CHECK_IRQ(SEC_ENG_ID0_CDET);
BL618DG_CHECK_IRQ(SF_CTRL_ID1);
BL618DG_CHECK_IRQ(SF_CTRL_ID0);
BL618DG_CHECK_IRQ(DMA0_ALL);
BL618DG_CHECK_IRQ(DVP2BUS_INT0);
BL618DG_CHECK_IRQ(SDH);
BL618DG_CHECK_IRQ(DVP2BUS_INT1);
BL618DG_CHECK_IRQ(WIFI_TBTT_WAKEUP);
BL618DG_CHECK_IRQ(IR);
BL618DG_CHECK_IRQ(DMA1_ALL);
BL618DG_CHECK_IRQ(AUADC);
BL618DG_CHECK_IRQ(MJPEG);
BL618DG_CHECK_IRQ(ETH);
BL618DG_CHECK_IRQ(GPADC_DMA);
BL618DG_CHECK_IRQ(EFUSE);
BL618DG_CHECK_IRQ(SPI0);
BL618DG_CHECK_IRQ(UART0);
BL618DG_CHECK_IRQ(UART1);
BL618DG_CHECK_IRQ(UART2);
BL618DG_CHECK_IRQ(KYS);
BL618DG_CHECK_IRQ(GPIO_DMA);
BL618DG_CHECK_IRQ(I2C0);
BL618DG_CHECK_IRQ(PWM);
BL618DG_CHECK_IRQ(TIMER0);
BL618DG_CHECK_IRQ(TIMER1);
BL618DG_CHECK_IRQ(WDG);
BL618DG_CHECK_IRQ(I2C1);
BL618DG_CHECK_IRQ(I2S);
BL618DG_CHECK_IRQ(ANA_OCP_OUT_TO_CPU);
BL618DG_CHECK_IRQ(RESERVED42);
BL618DG_CHECK_IRQ(XTAL_RDY_SCAN);
BL618DG_CHECK_IRQ(GPIO_INT0);
BL618DG_CHECK_IRQ(DM);
BL618DG_CHECK_IRQ(BT);
BL618DG_CHECK_IRQ(M154_REQ_ACK);
BL618DG_CHECK_IRQ(M154);
BL618DG_CHECK_IRQ(M154_AES);
BL618DG_CHECK_IRQ(PDS_WAKEUP);
BL618DG_CHECK_IRQ(HBN_OUT0);
BL618DG_CHECK_IRQ(HBN_OUT1);
BL618DG_CHECK_IRQ(BOD);
BL618DG_CHECK_IRQ(DMA2D);
BL618DG_CHECK_IRQ(BZ_PHY_INT);
BL618DG_CHECK_IRQ(BLE);
BL618DG_CHECK_IRQ(MAC_INT_TIMER);
BL618DG_CHECK_IRQ(MAC_INT_MISC);
BL618DG_CHECK_IRQ(MAC_INT_RX_TRIGGER);
BL618DG_CHECK_IRQ(MAC_INT_TX_TRIGGER);
BL618DG_CHECK_IRQ(MAC_INT_GEN);
BL618DG_CHECK_IRQ(MAC_INT_PROT_TRIGGER);
BL618DG_CHECK_IRQ(WIFI_IPC);
BL618DG_CHECK_IRQ(IPC0_CH0);
BL618DG_CHECK_IRQ(IPC0_CH1);
BL618DG_CHECK_IRQ(IPC1_CH0);
BL618DG_CHECK_IRQ(IPC1_CH1);
BL618DG_CHECK_IRQ(CAN0);
BL618DG_CHECK_IRQ(CAN1);
BL618DG_CHECK_IRQ(CAN2);
BL618DG_CHECK_IRQ(DMA2_INT0);
BL618DG_CHECK_IRQ(DMA2_INT1);
BL618DG_CHECK_IRQ(DMA2_INT2);
BL618DG_CHECK_IRQ(DMA2_INT3);
BL618DG_CHECK_IRQ(PWM1);
BL618DG_CHECK_IRQ(I2C2);
BL618DG_CHECK_IRQ(TIMER2);
BL618DG_CHECK_IRQ(TIMER3);
BL618DG_CHECK_IRQ(WDG1);
BL618DG_CHECK_IRQ(UART3);
BL618DG_CHECK_IRQ(SPI3);
BL618DG_CHECK_IRQ(RASTER2BLOCK);
BL618DG_CHECK_IRQ(DTSRC);
BL618DG_CHECK_IRQ(OSD0);
BL618DG_CHECK_IRQ(OSD1);
BL618DG_CHECK_IRQ(MM_SEOF0);
BL618DG_CHECK_IRQ(MM_SEOF1);
BL618DG_CHECK_IRQ(DSI);
BL618DG_CHECK_IRQ(SPI1);
BL618DG_CHECK_IRQ(SPI2);
BL618DG_CHECK_IRQ(TOUCH);
BL618DG_CHECK_IRQ(AUDAC);

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl618dg_irq_adapter_s
{
  irq_callback handler;
  void *arg;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bl618dg_irq_adapter_s
  g_bl618dg_irq_adapter[BL618DG_ECLIC_IRQ_COUNT];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl618dg_raw_irq_valid
 *
 * Description:
 *   Validate a raw SDK ECLIC slot before indexing the adapter or controller.
 *
 ****************************************************************************/

static bool bl618dg_raw_irq_valid(int irq)
{
  return irq >= 0 && irq < BL618DG_ECLIC_IRQ_COUNT;
}

/****************************************************************************
 * Name: bl618dg_lhal_interrupt
 *
 * Description:
 *   Pass the raw SDK number and registered argument to a LHAL callback.
 *   NuttX retains ownership of the saved context and interrupt return.
 *
 ****************************************************************************/

static int bl618dg_lhal_interrupt(int irq, void *context, void *arg)
{
  struct bl618dg_irq_adapter_s *adapter = arg;

  if (adapter->handler != NULL)
    {
      adapter->handler(irq - RISCV_IRQ_ASYNC, adapter->arg);
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bflb_irq_initialize
 *
 * Description:
 *   NuttX initializes the controller in up_irqinitialize(). SDK clients must
 *   not reset the live controller or replace the NuttX vector table.
 *
 ****************************************************************************/

void bflb_irq_initialize(void)
{
}

/****************************************************************************
 * Name: bflb_irq_save / bflb_irq_restore
 *
 * Description:
 *   Preserve the NuttX global interrupt state for SDK critical sections.
 *
 ****************************************************************************/

uintptr_t bflb_irq_save(void)
{
  return up_irq_save();
}

void bflb_irq_restore(uintptr_t flags)
{
  up_irq_restore(flags);
}

/****************************************************************************
 * Name: bflb_irq_attach / bflb_irq_detach
 *
 * Description:
 *   Translate a raw SDK number once at the NuttX API boundary. Update the
 *   callback and its argument atomically on this single-core port. Detach
 *   disables the source before releasing the callback.
 *
 * Returned Value:
 *   Zero on success, or a negated errno value on failure.
 *
 ****************************************************************************/

int bflb_irq_attach(int irq, irq_callback isr, void *arg)
{
  irqstate_t flags;
  int ret;

  if (!bl618dg_raw_irq_valid(irq) || isr == NULL)
    {
      return -EINVAL;
    }

  flags = up_irq_save();
  ret = irq_attach(irq + RISCV_IRQ_ASYNC, bl618dg_lhal_interrupt,
                   &g_bl618dg_irq_adapter[irq]);
  if (ret == OK)
    {
      g_bl618dg_irq_adapter[irq].handler = isr;
      g_bl618dg_irq_adapter[irq].arg = arg;
    }

  up_irq_restore(flags);
  return ret;
}

int bflb_irq_detach(int irq)
{
  irqstate_t flags;
  int ret;

  if (!bl618dg_raw_irq_valid(irq))
    {
      return -EINVAL;
    }

  flags = up_irq_save();
  up_disable_irq(irq + RISCV_IRQ_ASYNC);
  ret = irq_detach(irq + RISCV_IRQ_ASYNC);
  if (ret == OK)
    {
      g_bl618dg_irq_adapter[irq].handler = NULL;
      g_bl618dg_irq_adapter[irq].arg = NULL;
    }

  up_irq_restore(flags);
  return ret;
}

/****************************************************************************
 * Name: bflb_irq_enable / bflb_irq_disable
 *
 * Description:
 *   Enable or disable a raw SDK source through the NuttX controller API.
 *
 ****************************************************************************/

void bflb_irq_enable(int irq)
{
  if (bl618dg_raw_irq_valid(irq))
    {
      up_enable_irq(irq + RISCV_IRQ_ASYNC);
    }
}

void bflb_irq_disable(int irq)
{
  if (bl618dg_raw_irq_valid(irq))
    {
      up_disable_irq(irq + RISCV_IRQ_ASYNC);
    }
}

/****************************************************************************
 * Name: bflb_irq_set_pending / bflb_irq_clear_pending
 *
 * Description:
 *   Change the pending bit in the raw ECLIC slot.
 *
 ****************************************************************************/

void bflb_irq_set_pending(int irq)
{
  if (bl618dg_raw_irq_valid(irq))
    {
      __ECLIC_SetPendingIRQ(irq);
    }
}

void bflb_irq_clear_pending(int irq)
{
  if (bl618dg_raw_irq_valid(irq))
    {
      __ECLIC_ClearPendingIRQ(irq);
    }
}

/****************************************************************************
 * Name: bflb_irq_set_nlbits / bflb_irq_set_priority
 *
 * Description:
 *   Set the level/priority split and the source's SDK priority. Clamp the
 *   split to the controller's implemented bits. This port dispatches all
 *   interrupts through the common NuttX trap entry without nesting.
 *
 ****************************************************************************/

void bflb_irq_set_nlbits(uint8_t nlbits)
{
  uint32_t bits = __ECLIC_GetInfoCtlbits();

  __ECLIC_SetCfgNlbits(nlbits > bits ? bits : nlbits);
}

void bflb_irq_set_priority(int irq, uint8_t preemptprio, uint8_t subprio)
{
  irqstate_t flags;

  if (bl618dg_raw_irq_valid(irq))
    {
      flags = up_irq_save();
      __ECLIC_SetLevelIRQ(irq, preemptprio);
      __ECLIC_SetPriorityIRQ(irq, subprio);
      up_irq_restore(flags);
    }
}
