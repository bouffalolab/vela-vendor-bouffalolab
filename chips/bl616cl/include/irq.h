/****************************************************************************
 * apps/vendor/bouffalolab/chips/bl616cl/include/irq.h
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

/* This file should never be included directly but, rather, only indirectly
 * through nuttx/irq.h
 */

#ifndef __VENDOR_BOUFFALOLAB_CHIP_BL616CL_INCLUDE_IRQ_H
#define __VENDOR_BOUFFALOLAB_CHIP_BL616CL_INCLUDE_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* These are NuttX IRQ numbers, not the SDK's raw CLIC indices. For example,
 * UART0 is raw 44 in LHAL and IRQ 60 in NuttX. bl616cl_irq.c and the LHAL
 * adapter translate at the boundary; do not add this offset twice.
 */

#define BL616CL_RISCV_IRQ_ASYNC 16

#define BL616CL_IRQ_NUM_SSOFT          (BL616CL_RISCV_IRQ_ASYNC + 1)
#define BL616CL_IRQ_NUM_MSOFT          (BL616CL_RISCV_IRQ_ASYNC + 3)
#define BL616CL_IRQ_NUM_STIME          (BL616CL_RISCV_IRQ_ASYNC + 5)
#define BL616CL_IRQ_NUM_MTIME          (BL616CL_RISCV_IRQ_ASYNC + 7)
#define BL616CL_IRQ_NUM_SEXT           (BL616CL_RISCV_IRQ_ASYNC + 9)
#define BL616CL_IRQ_NUM_MEXT           (BL616CL_RISCV_IRQ_ASYNC + 11)
#define BL616CL_IRQ_NUM_CLIC_SOFT_PEND (BL616CL_RISCV_IRQ_ASYNC + 12)

#define BL616CL_IRQ_NUM_FIRST (BL616CL_RISCV_IRQ_ASYNC + 16)

#define BL616CL_IRQ_NUM_BMX_MCU_BUS_ERR (BL616CL_IRQ_NUM_FIRST + 0)
#define BL616CL_IRQ_NUM_BMX_MCU_TO (BL616CL_IRQ_NUM_FIRST + 1)
#define BL616CL_IRQ_NUM_DBI (BL616CL_IRQ_NUM_FIRST + 2)
#define BL616CL_IRQ_NUM_SDU_SOFT_RST (BL616CL_IRQ_NUM_FIRST + 3)
#define BL616CL_IRQ_NUM_AUDAC (BL616CL_IRQ_NUM_FIRST + 4)
#define BL616CL_IRQ_NUM_RF_TOP_INT0 (BL616CL_IRQ_NUM_FIRST + 5)
#define BL616CL_IRQ_NUM_RF_TOP_INT1 (BL616CL_IRQ_NUM_FIRST + 6)
#define BL616CL_IRQ_NUM_SDIO (BL616CL_IRQ_NUM_FIRST + 7)
#define BL616CL_IRQ_NUM_WIFI_TBTT_SLEEP (BL616CL_IRQ_NUM_FIRST + 8)
#define BL616CL_IRQ_NUM_SEC_ENG_ID1_SHA_AES_TRNG_PKA_GMAC \
  (BL616CL_IRQ_NUM_FIRST + 9)
#define BL616CL_IRQ_NUM_SEC_ENG_ID0_SHA_AES_TRNG_PKA_GMAC \
  (BL616CL_IRQ_NUM_FIRST + 10)
#define BL616CL_IRQ_NUM_SEC_ENG_ID1_CDET (BL616CL_IRQ_NUM_FIRST + 11)
#define BL616CL_IRQ_NUM_SEC_ENG_ID0_CDET (BL616CL_IRQ_NUM_FIRST + 12)
#define BL616CL_IRQ_NUM_SF_CTRL_ID1 (BL616CL_IRQ_NUM_FIRST + 13)
#define BL616CL_IRQ_NUM_SF_CTRL_ID0 (BL616CL_IRQ_NUM_FIRST + 14)
#define BL616CL_IRQ_NUM_DMA0_ALL (BL616CL_IRQ_NUM_FIRST + 15)
#define BL616CL_IRQ_NUM_DVP2BUS_INT0 (BL616CL_IRQ_NUM_FIRST + 16)
#define BL616CL_IRQ_NUM_SDH (BL616CL_IRQ_NUM_FIRST + 17)
#define BL616CL_IRQ_NUM_DVP2BUS_INT1 (BL616CL_IRQ_NUM_FIRST + 18)
#define BL616CL_IRQ_NUM_WIFI_TBTT_WAKEUP (BL616CL_IRQ_NUM_FIRST + 19)
#define BL616CL_IRQ_NUM_TOUCH_V2 (BL616CL_IRQ_NUM_FIRST + 20)
#define BL616CL_IRQ_NUM_USB (BL616CL_IRQ_NUM_FIRST + 21)
#define BL616CL_IRQ_NUM_AUADC (BL616CL_IRQ_NUM_FIRST + 22)
#define BL616CL_IRQ_NUM_MJPEG (BL616CL_IRQ_NUM_FIRST + 23)
#define BL616CL_IRQ_NUM_EMAC (BL616CL_IRQ_NUM_FIRST + 24)
#define BL616CL_IRQ_NUM_GPADC_DMA (BL616CL_IRQ_NUM_FIRST + 25)
#define BL616CL_IRQ_NUM_EFUSE (BL616CL_IRQ_NUM_FIRST + 26)
#define BL616CL_IRQ_NUM_SPI0 (BL616CL_IRQ_NUM_FIRST + 27)
#define BL616CL_IRQ_NUM_UART0 (BL616CL_IRQ_NUM_FIRST + 28)
#define BL616CL_IRQ_NUM_UART1 (BL616CL_IRQ_NUM_FIRST + 29)
#define BL616CL_IRQ_NUM_RESERVED30 (BL616CL_IRQ_NUM_FIRST + 30)
#define BL616CL_IRQ_NUM_GPIO_DMA (BL616CL_IRQ_NUM_FIRST + 31)
#define BL616CL_IRQ_NUM_I2C0 (BL616CL_IRQ_NUM_FIRST + 32)
#define BL616CL_IRQ_NUM_PWM (BL616CL_IRQ_NUM_FIRST + 33)
#define BL616CL_IRQ_NUM_PEC_INT0 (BL616CL_IRQ_NUM_FIRST + 34)
#define BL616CL_IRQ_NUM_PEC_INT1 (BL616CL_IRQ_NUM_FIRST + 35)
#define BL616CL_IRQ_NUM_TIMER0 (BL616CL_IRQ_NUM_FIRST + 36)
#define BL616CL_IRQ_NUM_TIMER1 (BL616CL_IRQ_NUM_FIRST + 37)
#define BL616CL_IRQ_NUM_WDG (BL616CL_IRQ_NUM_FIRST + 38)
#define BL616CL_IRQ_NUM_I2C1 (BL616CL_IRQ_NUM_FIRST + 39)
#define BL616CL_IRQ_NUM_I2S (BL616CL_IRQ_NUM_FIRST + 40)
#define BL616CL_IRQ_NUM_ANA_OCP_OUT_TO_CPU_0 (BL616CL_IRQ_NUM_FIRST + 41)
#define BL616CL_IRQ_NUM_ANA_OCP_OUT_TO_CPU_1 (BL616CL_IRQ_NUM_FIRST + 42)
#define BL616CL_IRQ_NUM_XTAL_RDY_SCAN (BL616CL_IRQ_NUM_FIRST + 43)
#define BL616CL_IRQ_NUM_GPIO_INT0 (BL616CL_IRQ_NUM_FIRST + 44)
#define BL616CL_IRQ_NUM_DM (BL616CL_IRQ_NUM_FIRST + 45)
#define BL616CL_IRQ_NUM_BT (BL616CL_IRQ_NUM_FIRST + 46)
#define BL616CL_IRQ_NUM_UART2 (BL616CL_IRQ_NUM_FIRST + 47)
#define BL616CL_IRQ_NUM_SPI1 (BL616CL_IRQ_NUM_FIRST + 48)
#define BL616CL_IRQ_NUM_MJDEC (BL616CL_IRQ_NUM_FIRST + 49)
#define BL616CL_IRQ_NUM_PDS_WAKEUP (BL616CL_IRQ_NUM_FIRST + 50)
#define BL616CL_IRQ_NUM_HBN_OUT0 (BL616CL_IRQ_NUM_FIRST + 51)
#define BL616CL_IRQ_NUM_HBN_OUT1 (BL616CL_IRQ_NUM_FIRST + 52)
#define BL616CL_IRQ_NUM_BOD (BL616CL_IRQ_NUM_FIRST + 53)
#define BL616CL_IRQ_NUM_WIFI (BL616CL_IRQ_NUM_FIRST + 54)
#define BL616CL_IRQ_NUM_BZ_PHY_INT (BL616CL_IRQ_NUM_FIRST + 55)
#define BL616CL_IRQ_NUM_BLE (BL616CL_IRQ_NUM_FIRST + 56)
#define BL616CL_IRQ_NUM_MAC_INT_TIMER (BL616CL_IRQ_NUM_FIRST + 57)
#define BL616CL_IRQ_NUM_MAC_INT_MISC (BL616CL_IRQ_NUM_FIRST + 58)
#define BL616CL_IRQ_NUM_MAC_INT_RX_TRIGGER (BL616CL_IRQ_NUM_FIRST + 59)
#define BL616CL_IRQ_NUM_MAC_INT_TX_TRIGGER (BL616CL_IRQ_NUM_FIRST + 60)
#define BL616CL_IRQ_NUM_MAC_INT_GEN (BL616CL_IRQ_NUM_FIRST + 61)
#define BL616CL_IRQ_NUM_MAC_INT_PROT_TRIGGER (BL616CL_IRQ_NUM_FIRST + 62)
#define BL616CL_IRQ_NUM_WIFI_IPC (BL616CL_IRQ_NUM_FIRST + 63)
#define BL616CL_IRQ_NUM_EXP_PERI (BL616CL_IRQ_NUM_FIRST + 64)
#define BL616CL_IRQ_NUM_LTMR_GPIO_LAT (BL616CL_IRQ_NUM_FIRST + 65)
#define BL616CL_IRQ_NUM_WDT1 (BL616CL_IRQ_NUM_FIRST + 66)

#define NR_IRQS (BL616CL_IRQ_NUM_WDT1 + 1)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __VENDOR_BOUFFALOLAB_CHIP_BL616CL_INCLUDE_IRQ_H */
