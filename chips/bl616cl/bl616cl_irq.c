/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/bl616cl_irq.c
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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include <arch/csr.h>
#include <arch/irq.h>

#include "bl616cl_sdk.h"
#include "bl616cl_hbn.h"
#include "bl616cl_irq.h"
#include "bl616cl_irq_internal.h"
#include "hardware/bl616cl_core.h"
#include "hardware/bl616cl_memorymap.h"
#include "riscv_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL616CL_IRQ_DEFAULT_PRIORITY     0x40
#define BL616CL_HBN_GPIO_WAKE_MASK_ALL   0x3f

/* Keep the public NuttX IRQ contract independent of SDK headers, but fail
 * the build if an upstream IRQ map no longer matches the adapter.
 */

static_assert(BL616CL_RISCV_IRQ_ASYNC == RISCV_IRQ_ASYNC &&
              BL616CL_IRQ_CLIC_COUNT == IRQn_LAST &&
              NR_IRQS == IRQn_LAST + RISCV_IRQ_ASYNC,
              "BL616CL IRQ range differs from the SDK");

#define BL616CL_CHECK_IRQ(nuttx_irq, raw_irq) \
  static_assert((nuttx_irq) == (raw_irq) + RISCV_IRQ_ASYNC, \
                "BL616CL IRQ mapping mismatch: " #nuttx_irq)

BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_SSOFT, BL616CL_IRQ_SSOFT);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_MSOFT, BL616CL_IRQ_MSOFT);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_STIME, BL616CL_IRQ_STIME);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_MTIME, BL616CL_IRQ_MTIME);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_SEXT, BL616CL_IRQ_SEXT);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_MEXT, BL616CL_IRQ_MEXT);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_CLIC_SOFT_PEND,
                  BL616CL_IRQ_CLIC_SOFT_PEND);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_BMX_MCU_BUS_ERR,
                  BL616CL_IRQ_BMX_MCU_BUS_ERR);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_BMX_MCU_TO, BL616CL_IRQ_BMX_MCU_TO);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_DBI, BL616CL_IRQ_DBI);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_SDU_SOFT_RST,
                  BL616CL_IRQ_SDU_SOFT_RST);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_AUDAC, BL616CL_IRQ_AUDAC);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_RF_TOP_INT0, BL616CL_IRQ_RF_TOP_INT0);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_RF_TOP_INT1, BL616CL_IRQ_RF_TOP_INT1);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_SDIO, BL616CL_IRQ_SDIO);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_WIFI_TBTT_SLEEP,
                  BL616CL_IRQ_WIFI_TBTT_SLEEP);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_SEC_ENG_ID1_SHA_AES_TRNG_PKA_GMAC,
                  BL616CL_IRQ_SEC_ENG_ID1_SHA_AES_TRNG_PKA_GMAC);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_SEC_ENG_ID0_SHA_AES_TRNG_PKA_GMAC,
                  BL616CL_IRQ_SEC_ENG_ID0_SHA_AES_TRNG_PKA_GMAC);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_SEC_ENG_ID1_CDET,
                  BL616CL_IRQ_SEC_ENG_ID1_CDET);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_SEC_ENG_ID0_CDET,
                  BL616CL_IRQ_SEC_ENG_ID0_CDET);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_SF_CTRL_ID1, BL616CL_IRQ_SF_CTRL_ID1);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_SF_CTRL_ID0, BL616CL_IRQ_SF_CTRL_ID0);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_DMA0_ALL, BL616CL_IRQ_DMA0_ALL);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_DVP2BUS_INT0,
                  BL616CL_IRQ_DVP2BUS_INT0);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_SDH, BL616CL_IRQ_SDH);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_DVP2BUS_INT1,
                  BL616CL_IRQ_DVP2BUS_INT1);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_WIFI_TBTT_WAKEUP,
                  BL616CL_IRQ_WIFI_TBTT_WAKEUP);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_TOUCH_V2, BL616CL_IRQ_TOUCH_V2);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_USB, BL616CL_IRQ_USB);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_AUADC, BL616CL_IRQ_AUADC);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_MJPEG, BL616CL_IRQ_MJPEG);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_EMAC, BL616CL_IRQ_EMAC);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_GPADC_DMA, BL616CL_IRQ_GPADC_DMA);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_EFUSE, BL616CL_IRQ_EFUSE);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_SPI0, BL616CL_IRQ_SPI0);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_UART0, BL616CL_IRQ_UART0);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_UART1, BL616CL_IRQ_UART1);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_RESERVED30, BL616CL_IRQ_RESERVED30);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_GPIO_DMA, BL616CL_IRQ_GPIO_DMA);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_I2C0, BL616CL_IRQ_I2C0);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_PWM, BL616CL_IRQ_PWM);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_PEC_INT0, BL616CL_IRQ_PEC_INT0);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_PEC_INT1, BL616CL_IRQ_PEC_INT1);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_TIMER0, BL616CL_IRQ_TIMER0);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_TIMER1, BL616CL_IRQ_TIMER1);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_WDG, BL616CL_IRQ_WDG);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_I2C1, BL616CL_IRQ_I2C1);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_I2S, BL616CL_IRQ_I2S);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_ANA_OCP_OUT_TO_CPU_0,
                  BL616CL_IRQ_ANA_OCP_OUT_TO_CPU_0);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_ANA_OCP_OUT_TO_CPU_1,
                  BL616CL_IRQ_ANA_OCP_OUT_TO_CPU_1);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_XTAL_RDY_SCAN,
                  BL616CL_IRQ_XTAL_RDY_SCAN);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_GPIO_INT0, BL616CL_IRQ_GPIO_INT0);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_DM, BL616CL_IRQ_DM);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_BT, BL616CL_IRQ_BT);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_UART2, BL616CL_IRQ_UART2);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_SPI1, BL616CL_IRQ_SPI1);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_MJDEC, BL616CL_IRQ_MJDEC);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_PDS_WAKEUP, BL616CL_IRQ_PDS_WAKEUP);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_HBN_OUT0, BL616CL_IRQ_HBN_OUT0);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_HBN_OUT1, BL616CL_IRQ_HBN_OUT1);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_BOD, BL616CL_IRQ_BOD);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_WIFI, BL616CL_IRQ_WIFI);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_BZ_PHY_INT, BL616CL_IRQ_BZ_PHY_INT);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_BLE, BL616CL_IRQ_BLE);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_MAC_INT_TIMER,
                  BL616CL_IRQ_MAC_INT_TIMER);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_MAC_INT_MISC,
                  BL616CL_IRQ_MAC_INT_MISC);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_MAC_INT_RX_TRIGGER,
                  BL616CL_IRQ_MAC_INT_RX_TRIGGER);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_MAC_INT_TX_TRIGGER,
                  BL616CL_IRQ_MAC_INT_TX_TRIGGER);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_MAC_INT_GEN, BL616CL_IRQ_MAC_INT_GEN);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_MAC_INT_PROT_TRIGGER,
                  BL616CL_IRQ_MAC_INT_PROT_TRIGGER);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_WIFI_IPC, BL616CL_IRQ_WIFI_IPC);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_EXP_PERI, BL616CL_IRQ_EXP_PERI);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_LTMR_GPIO_LAT,
                  BL616CL_IRQ_LTMR_GPIO_LAT);
BL616CL_CHECK_IRQ(BL616CL_IRQ_NUM_WDT1, BL616CL_IRQ_WDT1);

#undef BL616CL_CHECK_IRQ

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uintptr_t bl616cl_clic_int_addr(int irq, uintptr_t offset)
{
  return CLIC_BASE + BL616CL_CLICINT_OFFSET +
         (irq * BL616CL_CLICINT_STRIDE) + offset;
}

static bool bl616cl_irq_raw_valid(int irq)
{
  return irq >= 0 && irq < BL616CL_IRQ_CLIC_COUNT;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl616cl_irq_raw_to_nuttx
 ****************************************************************************/

int bl616cl_irq_raw_to_nuttx(int irq)
{
  return irq + RISCV_IRQ_ASYNC;
}

/****************************************************************************
 * Name: bl616cl_irq_nuttx_to_raw
 ****************************************************************************/

int bl616cl_irq_nuttx_to_raw(int irq)
{
  return irq - RISCV_IRQ_ASYNC;
}

/****************************************************************************
 * Name: bl616cl_clic_enable_raw
 ****************************************************************************/

void bl616cl_clic_enable_raw(int irq)
{
  if (bl616cl_irq_raw_valid(irq))
    {
      putreg8(1, bl616cl_clic_int_addr(irq, BL616CL_CLICINT_IE_OFFSET));
    }
}

/****************************************************************************
 * Name: bl616cl_clic_disable_raw
 ****************************************************************************/

void bl616cl_clic_disable_raw(int irq)
{
  if (bl616cl_irq_raw_valid(irq))
    {
      putreg8(0, bl616cl_clic_int_addr(irq, BL616CL_CLICINT_IE_OFFSET));
    }
}

/****************************************************************************
 * Name: bl616cl_clic_set_pending_raw
 ****************************************************************************/

void bl616cl_clic_set_pending_raw(int irq)
{
  if (bl616cl_irq_raw_valid(irq))
    {
      putreg8(1, bl616cl_clic_int_addr(irq, BL616CL_CLICINT_IP_OFFSET));
    }
}

/****************************************************************************
 * Name: bl616cl_clic_clear_pending_raw
 ****************************************************************************/

void bl616cl_clic_clear_pending_raw(int irq)
{
  if (bl616cl_irq_raw_valid(irq))
    {
      putreg8(0, bl616cl_clic_int_addr(irq, BL616CL_CLICINT_IP_OFFSET));
    }
}

/****************************************************************************
 * Name: bl616cl_clic_set_nlbits
 ****************************************************************************/

void bl616cl_clic_set_nlbits(uint8_t nlbits)
{
  uint8_t cfg;

  cfg = (nlbits & 0x0f) << CLIC_CLICCFG_NLBIT_Pos;
  putreg8(cfg, CLIC_BASE + BL616CL_CLICCFG_OFFSET);
}

/****************************************************************************
 * Name: bl616cl_clic_set_priority_raw
 ****************************************************************************/

void bl616cl_clic_set_priority_raw(int irq, uint8_t preemptprio,
                                   uint8_t subprio)
{
  uint8_t ctl;
  uint8_t clic_int_cfg;
  uint8_t nlbits;

  if (bl616cl_irq_raw_valid(irq))
    {
      nlbits = (getreg8(CLIC_BASE + BL616CL_CLICCFG_OFFSET) >>
                CLIC_CLICCFG_NLBIT_Pos) & 0x0f;
      if (nlbits > 8)
        {
          nlbits = 8;
        }

      clic_int_cfg = getreg8(bl616cl_clic_int_addr(
        irq, BL616CL_CLICINT_CTL_OFFSET));
      ctl = (clic_int_cfg & 0x0f) |
            (preemptprio << (8 - nlbits)) |
            ((subprio & (0x0f >> nlbits)) << 4);
      putreg8(ctl, bl616cl_clic_int_addr(irq, BL616CL_CLICINT_CTL_OFFSET));
    }
}

/****************************************************************************
 * Name: up_irqinitialize
 ****************************************************************************/

void up_irqinitialize(void)
{
  uint32_t clicinfo;
  uint8_t nlbits;
  int irq;

  up_irq_save();

  /* No HBN GPIO wake source owns HBN_OUT0 during normal startup.  Establish
   * a masked baseline and discard stale GPIO status; a future wakeup owner
   * must explicitly unmask the pins it configures.
   */

  HBN_Pin_WakeUp_Mask(BL616CL_HBN_GPIO_WAKE_MASK_ALL);
  for (irq = HBN_INT_GPIO0; irq <= HBN_INT_GPIO5; irq++)
    {
      HBN_Clear_IRQ(irq);
    }

  clicinfo = getreg32(CLIC_BASE + BL616CL_CLICINFO_OFFSET);
  nlbits = (clicinfo & CLIC_INFO_CLICINTCTLBITS_Msk) >>
           CLIC_INFO_CLICINTCTLBITS_Pos;
  bl616cl_clic_set_nlbits(nlbits);

  for (irq = 0; irq < BL616CL_IRQ_CLIC_COUNT; irq++)
    {
      bl616cl_clic_disable_raw(irq);
      bl616cl_clic_clear_pending_raw(irq);
      putreg8(0, bl616cl_clic_int_addr(irq, BL616CL_CLICINT_ATTR_OFFSET));
      putreg8(BL616CL_IRQ_DEFAULT_PRIORITY,
              bl616cl_clic_int_addr(irq, BL616CL_CLICINT_CTL_OFFSET));
    }

  putreg8((1u << CLIC_INTATTR_TRIG_Pos),
          bl616cl_clic_int_addr(MSOFT_IRQn,
                               BL616CL_CLICINT_ATTR_OFFSET));
  putreg8((1u << CLIC_INTATTR_TRIG_Pos),
          bl616cl_clic_int_addr(SDU_SOFT_RST_IRQn,
                               BL616CL_CLICINT_ATTR_OFFSET));

  riscv_exception_attach();

#ifndef CONFIG_SUPPRESS_INTERRUPTS
  riscv_color_intstack();
  up_irq_enable();
#endif
}

/****************************************************************************
 * Name: up_enable_irq
 *
 * Description:
 *   Enable the interrupt specified by 'irq'
 *
 ****************************************************************************/

void up_enable_irq(int irq)
{
  int rawirq;

  if (irq < RISCV_IRQ_ASYNC)
    {
      return;
    }

  rawirq = bl616cl_irq_nuttx_to_raw(irq);
  if (!bl616cl_irq_raw_valid(rawirq))
    {
      ASSERT(false);
      return;
    }

  if (rawirq == MSOFT_IRQn)
    {
      SET_CSR(mie, MIE_MSIE);
    }
  else if (rawirq == MTIME_IRQn)
    {
      SET_CSR(mie, MIE_MTIE);
    }
  else if (rawirq == MEXT_IRQn)
    {
      SET_CSR(mie, MIE_MEIE);
    }

  bl616cl_clic_enable_raw(rawirq);
}

/****************************************************************************
 * Name: up_disable_irq
 *
 * Description:
 *   Disable the interrupt specified by 'irq'
 *
 ****************************************************************************/

void up_disable_irq(int irq)
{
  int rawirq;

  if (irq < RISCV_IRQ_ASYNC)
    {
      return;
    }

  rawirq = bl616cl_irq_nuttx_to_raw(irq);
  if (!bl616cl_irq_raw_valid(rawirq))
    {
      ASSERT(false);
      return;
    }

  if (rawirq == MSOFT_IRQn)
    {
      CLEAR_CSR(mie, MIE_MSIE);
    }
  else if (rawirq == MTIME_IRQn)
    {
      CLEAR_CSR(mie, MIE_MTIE);
    }
  else if (rawirq == MEXT_IRQn)
    {
      CLEAR_CSR(mie, MIE_MEIE);
    }

  bl616cl_clic_disable_raw(rawirq);
}

/****************************************************************************
 * Name: riscv_ack_irq
 *
 * Description:
 *   Acknowledge the IRQ
 *
 ****************************************************************************/

void riscv_ack_irq(int irq)
{
  int rawirq;

  if (irq < RISCV_IRQ_ASYNC)
    {
      return;
    }

  rawirq = bl616cl_irq_nuttx_to_raw(irq);
  bl616cl_clic_clear_pending_raw(rawirq);
}

/****************************************************************************
 * Name: up_irq_enable
 ****************************************************************************/

irqstate_t up_irq_enable(void)
{
  irqstate_t flags;

  SET_CSR(mie, MIE_MEIE);

  __asm__ __volatile__("csrrs %0, mstatus, %1\n"
                       : "=r"(flags)
                       : "r"(STATUS_IE)
                       : "memory");

  return flags;
}
