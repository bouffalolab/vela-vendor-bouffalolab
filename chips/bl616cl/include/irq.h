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

#define BL616CL_IRQ_SSOFT          (BL616CL_RISCV_IRQ_ASYNC + 1)
#define BL616CL_IRQ_MSOFT          (BL616CL_RISCV_IRQ_ASYNC + 3)
#define BL616CL_IRQ_STIME          (BL616CL_RISCV_IRQ_ASYNC + 5)
#define BL616CL_IRQ_MTIME          (BL616CL_RISCV_IRQ_ASYNC + 7)
#define BL616CL_IRQ_SEXT           (BL616CL_RISCV_IRQ_ASYNC + 9)
#define BL616CL_IRQ_MEXT           (BL616CL_RISCV_IRQ_ASYNC + 11)
#define BL616CL_IRQ_CLIC_SOFT_PEND (BL616CL_RISCV_IRQ_ASYNC + 12)

#define BL616CL_RISCV_IRQ_MTIMER BL616CL_IRQ_MTIME

#define BL616CL_IRQ_NUM_BASE (BL616CL_RISCV_IRQ_ASYNC + 16)
#define BL616CL_IRQ_FIRST    BL616CL_IRQ_NUM_BASE

#define BL616CL_IRQ_BMX_MCU_BUS_ERR (BL616CL_IRQ_FIRST + 0)
#define BL616CL_IRQ_BMX_MCU_TO (BL616CL_IRQ_FIRST + 1)
#define BL616CL_IRQ_DBI (BL616CL_IRQ_FIRST + 2)
#define BL616CL_IRQ_SDU_SOFT_RST (BL616CL_IRQ_FIRST + 3)
#define BL616CL_IRQ_AUDIO (BL616CL_IRQ_FIRST + 4)
#define BL616CL_IRQ_RF_TOP_INT0 (BL616CL_IRQ_FIRST + 5)
#define BL616CL_IRQ_RF_TOP_INT1 (BL616CL_IRQ_FIRST + 6)
#define BL616CL_IRQ_SDIO (BL616CL_IRQ_FIRST + 7)
#define BL616CL_IRQ_WIFI_TBTT_SLEEP (BL616CL_IRQ_FIRST + 8)
#define BL616CL_IRQ_SEC_ENG_ID1_SHA_AES_TRNG_PKA_GMAC \
  (BL616CL_IRQ_FIRST + 9)
#define BL616CL_IRQ_SEC_ENG_ID0_SHA_AES_TRNG_PKA_GMAC \
  (BL616CL_IRQ_FIRST + 10)
#define BL616CL_IRQ_SEC_ENG_ID1_CDET (BL616CL_IRQ_FIRST + 11)
#define BL616CL_IRQ_SEC_ENG_ID0_CDET (BL616CL_IRQ_FIRST + 12)
#define BL616CL_IRQ_SF_CTRL_ID1 (BL616CL_IRQ_FIRST + 13)
#define BL616CL_IRQ_SF_CTRL_ID0 (BL616CL_IRQ_FIRST + 14)
#define BL616CL_IRQ_DMA0_ALL (BL616CL_IRQ_FIRST + 15)
#define BL616CL_IRQ_DVP2BUS_INT0 (BL616CL_IRQ_FIRST + 16)
#define BL616CL_IRQ_SDH (BL616CL_IRQ_FIRST + 17)
#define BL616CL_IRQ_DVP2BUS_INT1 (BL616CL_IRQ_FIRST + 18)
#define BL616CL_IRQ_WIFI_TBTT_WAKEUP (BL616CL_IRQ_FIRST + 19)
#define BL616CL_IRQ_IRRX (BL616CL_IRQ_FIRST + 20)
#define BL616CL_IRQ_USB (BL616CL_IRQ_FIRST + 21)
#define BL616CL_IRQ_AUPDM (BL616CL_IRQ_FIRST + 22)
#define BL616CL_IRQ_MJPEG (BL616CL_IRQ_FIRST + 23)
#define BL616CL_IRQ_EMAC (BL616CL_IRQ_FIRST + 24)
#define BL616CL_IRQ_GPADC_DMA (BL616CL_IRQ_FIRST + 25)
#define BL616CL_IRQ_EFUSE (BL616CL_IRQ_FIRST + 26)
#define BL616CL_IRQ_SPI0 (BL616CL_IRQ_FIRST + 27)
#define BL616CL_IRQ_UART0 (BL616CL_IRQ_FIRST + 28)
#define BL616CL_IRQ_UART1 (BL616CL_IRQ_FIRST + 29)
#define BL616CL_IRQ_RESERVED01 (BL616CL_IRQ_FIRST + 30)
#define BL616CL_IRQ_GPIO_DMA (BL616CL_IRQ_FIRST + 31)
#define BL616CL_IRQ_I2C0 (BL616CL_IRQ_FIRST + 32)
#define BL616CL_IRQ_PWM (BL616CL_IRQ_FIRST + 33)
#define BL616CL_IRQ_RESERVED0 (BL616CL_IRQ_FIRST + 34)
#define BL616CL_IRQ_RESERVED1 (BL616CL_IRQ_FIRST + 35)
#define BL616CL_IRQ_TIMER0_CH0 (BL616CL_IRQ_FIRST + 36)
#define BL616CL_IRQ_TIMER0_CH1 (BL616CL_IRQ_FIRST + 37)
#define BL616CL_IRQ_TIMER0_WDT (BL616CL_IRQ_FIRST + 38)
#define BL616CL_IRQ_I2C1 (BL616CL_IRQ_FIRST + 39)
#define BL616CL_IRQ_I2S (BL616CL_IRQ_FIRST + 40)
#define BL616CL_IRQ_ANA_OCP_OUT_TO_CPU_0 (BL616CL_IRQ_FIRST + 41)
#define BL616CL_IRQ_ANA_OCP_OUT_TO_CPU_1 (BL616CL_IRQ_FIRST + 42)
#define BL616CL_IRQ_XTAL_RDY_SCAN (BL616CL_IRQ_FIRST + 43)
#define BL616CL_IRQ_GPIO_INT0 (BL616CL_IRQ_FIRST + 44)
#define BL616CL_IRQ_DM (BL616CL_IRQ_FIRST + 45)
#define BL616CL_IRQ_BT (BL616CL_IRQ_FIRST + 46)
#define BL616CL_IRQ_M154_REQ_ACK (BL616CL_IRQ_FIRST + 47)
#define BL616CL_IRQ_M154_INT (BL616CL_IRQ_FIRST + 48)
#define BL616CL_IRQ_M154_AES (BL616CL_IRQ_FIRST + 49)
#define BL616CL_IRQ_PDS_WAKEUP (BL616CL_IRQ_FIRST + 50)
#define BL616CL_IRQ_HBN_OUT0 (BL616CL_IRQ_FIRST + 51)
#define BL616CL_IRQ_HBN_OUT1 (BL616CL_IRQ_FIRST + 52)
#define BL616CL_IRQ_BOD (BL616CL_IRQ_FIRST + 53)
#define BL616CL_IRQ_WIFI (BL616CL_IRQ_FIRST + 54)
#define BL616CL_IRQ_BZ_PHY_INT (BL616CL_IRQ_FIRST + 55)
#define BL616CL_IRQ_BLE (BL616CL_IRQ_FIRST + 56)
#define BL616CL_IRQ_MAC_INT_TIMER (BL616CL_IRQ_FIRST + 57)
#define BL616CL_IRQ_MAC_INT_MISC (BL616CL_IRQ_FIRST + 58)
#define BL616CL_IRQ_MAC_INT_RX_TRIGGER (BL616CL_IRQ_FIRST + 59)
#define BL616CL_IRQ_MAC_INT_TX_TRIGGER (BL616CL_IRQ_FIRST + 60)
#define BL616CL_IRQ_MAC_INT_GEN (BL616CL_IRQ_FIRST + 61)
#define BL616CL_IRQ_MAC_INT_PROT_TRIGGER (BL616CL_IRQ_FIRST + 62)
#define BL616CL_IRQ_WIFI_IPC (BL616CL_IRQ_FIRST + 63)
#define BL616CL_IRQ_EXP_PERI (BL616CL_IRQ_FIRST + 64)
#define BL616CL_IRQ_LTMR_GPIO_LAT (BL616CL_IRQ_FIRST + 65)
#define BL616CL_IRQ_WDT1 (BL616CL_IRQ_FIRST + 66)

/* LHAL names for CLIC slots named differently by the std header. */

#define BL616CL_IRQ_AUDAC BL616CL_IRQ_AUDIO
#define BL616CL_IRQ_TOUCH_V2 BL616CL_IRQ_IRRX
#define BL616CL_IRQ_AUADC BL616CL_IRQ_AUPDM
#define BL616CL_IRQ_RESERVED30 BL616CL_IRQ_RESERVED01
#define BL616CL_IRQ_PEC_INT0 BL616CL_IRQ_RESERVED0
#define BL616CL_IRQ_PEC_INT1 BL616CL_IRQ_RESERVED1
#define BL616CL_IRQ_UART2 BL616CL_IRQ_M154_REQ_ACK
#define BL616CL_IRQ_SPI1 BL616CL_IRQ_M154_INT
#define BL616CL_IRQ_MJDEC BL616CL_IRQ_M154_AES

/* Compatibility names used by existing drivers. */

#define BL616CL_IRQ_BMX_MCU_TIMEOUT BL616CL_IRQ_BMX_MCU_TO
#define BL616CL_IRQ_TIMER0 BL616CL_IRQ_TIMER0_CH0
#define BL616CL_IRQ_TIMER1 BL616CL_IRQ_TIMER0_CH1
#define BL616CL_IRQ_WDG BL616CL_IRQ_TIMER0_WDT

#define NR_IRQS (BL616CL_IRQ_WDT1 + 1)

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
