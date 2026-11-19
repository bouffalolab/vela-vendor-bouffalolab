/****************************************************************************
 * vendor/bouffalolab/chips/bl618dg/include/irq.h
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

#ifndef __VENDOR_BOUFFALOLAB_CHIPS_BL618DG_INCLUDE_IRQ_H
#define __VENDOR_BOUFFALOLAB_CHIPS_BL618DG_INCLUDE_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <arch/chip/chip.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* NuttX reserves 0..15 for synchronous exceptions. Peripheral offsets
 * follow the SDK AP interrupt map; raw ECLIC slots start at 19.
 */

#define BL618DG_IRQ_NUM_FIRST (16 + 19)

#define BL618DG_IRQ_NUM_BMX_MCU_BUS_ERR                   (BL618DG_IRQ_NUM_FIRST + 0)
#define BL618DG_IRQ_NUM_MJDEC                             (BL618DG_IRQ_NUM_FIRST + 1)
#define BL618DG_IRQ_NUM_DISPLAY                           (BL618DG_IRQ_NUM_FIRST + 2)
#define BL618DG_IRQ_NUM_USB                               (BL618DG_IRQ_NUM_FIRST + 3)
#define BL618DG_IRQ_NUM_WIFI_TO_CPU                       (BL618DG_IRQ_NUM_FIRST + 4)
#define BL618DG_IRQ_NUM_RF_TOP_INT0                       (BL618DG_IRQ_NUM_FIRST + 5)
#define BL618DG_IRQ_NUM_RF_TOP_INT1                       (BL618DG_IRQ_NUM_FIRST + 6)
#define BL618DG_IRQ_NUM_SDIO                              (BL618DG_IRQ_NUM_FIRST + 7)
#define BL618DG_IRQ_NUM_WIFI_TBTT_SLEEP                   (BL618DG_IRQ_NUM_FIRST + 8)
#define BL618DG_IRQ_NUM_SEC_ENG_ID1_SHA_AES_TRNG_PKA_GMAC (BL618DG_IRQ_NUM_FIRST + 9)
#define BL618DG_IRQ_NUM_SEC_ENG_ID0_SHA_AES_TRNG_PKA_GMAC (BL618DG_IRQ_NUM_FIRST + 10)
#define BL618DG_IRQ_NUM_SEC_ENG_ID1_CDET                  (BL618DG_IRQ_NUM_FIRST + 11)
#define BL618DG_IRQ_NUM_SEC_ENG_ID0_CDET                  (BL618DG_IRQ_NUM_FIRST + 12)
#define BL618DG_IRQ_NUM_SF_CTRL_ID1                       (BL618DG_IRQ_NUM_FIRST + 13)
#define BL618DG_IRQ_NUM_SF_CTRL_ID0                       (BL618DG_IRQ_NUM_FIRST + 14)
#define BL618DG_IRQ_NUM_DMA0_ALL                          (BL618DG_IRQ_NUM_FIRST + 15)
#define BL618DG_IRQ_NUM_DVP2BUS_INT0                      (BL618DG_IRQ_NUM_FIRST + 16)
#define BL618DG_IRQ_NUM_SDH                               (BL618DG_IRQ_NUM_FIRST + 17)
#define BL618DG_IRQ_NUM_DVP2BUS_INT1                      (BL618DG_IRQ_NUM_FIRST + 18)
#define BL618DG_IRQ_NUM_WIFI_TBTT_WAKEUP                  (BL618DG_IRQ_NUM_FIRST + 19)
#define BL618DG_IRQ_NUM_IR                                (BL618DG_IRQ_NUM_FIRST + 20)
#define BL618DG_IRQ_NUM_DMA1_ALL                          (BL618DG_IRQ_NUM_FIRST + 21)
#define BL618DG_IRQ_NUM_AUADC                             (BL618DG_IRQ_NUM_FIRST + 22)
#define BL618DG_IRQ_NUM_MJPEG                             (BL618DG_IRQ_NUM_FIRST + 23)
#define BL618DG_IRQ_NUM_ETH                               (BL618DG_IRQ_NUM_FIRST + 24)
#define BL618DG_IRQ_NUM_GPADC_DMA                         (BL618DG_IRQ_NUM_FIRST + 25)
#define BL618DG_IRQ_NUM_EFUSE                             (BL618DG_IRQ_NUM_FIRST + 26)
#define BL618DG_IRQ_NUM_SPI0                              (BL618DG_IRQ_NUM_FIRST + 27)
#define BL618DG_IRQ_NUM_UART0                             (BL618DG_IRQ_NUM_FIRST + 28)
#define BL618DG_IRQ_NUM_UART1                             (BL618DG_IRQ_NUM_FIRST + 29)
#define BL618DG_IRQ_NUM_UART2                             (BL618DG_IRQ_NUM_FIRST + 30)
#define BL618DG_IRQ_NUM_KYS                               (BL618DG_IRQ_NUM_FIRST + 30)
#define BL618DG_IRQ_NUM_GPIO_DMA                          (BL618DG_IRQ_NUM_FIRST + 31)
#define BL618DG_IRQ_NUM_I2C0                              (BL618DG_IRQ_NUM_FIRST + 32)
#define BL618DG_IRQ_NUM_PWM                               (BL618DG_IRQ_NUM_FIRST + 33)
#define BL618DG_IRQ_NUM_TIMER0                            (BL618DG_IRQ_NUM_FIRST + 36)
#define BL618DG_IRQ_NUM_TIMER1                            (BL618DG_IRQ_NUM_FIRST + 37)
#define BL618DG_IRQ_NUM_WDG                               (BL618DG_IRQ_NUM_FIRST + 38)
#define BL618DG_IRQ_NUM_I2C1                              (BL618DG_IRQ_NUM_FIRST + 39)
#define BL618DG_IRQ_NUM_I2S                               (BL618DG_IRQ_NUM_FIRST + 40)
#define BL618DG_IRQ_NUM_ANA_OCP_OUT_TO_CPU                (BL618DG_IRQ_NUM_FIRST + 41)
#define BL618DG_IRQ_NUM_RESERVED42                        (BL618DG_IRQ_NUM_FIRST + 42)
#define BL618DG_IRQ_NUM_XTAL_RDY_SCAN                     (BL618DG_IRQ_NUM_FIRST + 43)
#define BL618DG_IRQ_NUM_GPIO_INT0                         (BL618DG_IRQ_NUM_FIRST + 44)
#define BL618DG_IRQ_NUM_DM                                (BL618DG_IRQ_NUM_FIRST + 45)
#define BL618DG_IRQ_NUM_BT                                (BL618DG_IRQ_NUM_FIRST + 46)
#define BL618DG_IRQ_NUM_M154_REQ_ACK                      (BL618DG_IRQ_NUM_FIRST + 47)
#define BL618DG_IRQ_NUM_M154                              (BL618DG_IRQ_NUM_FIRST + 48)
#define BL618DG_IRQ_NUM_M154_AES                          (BL618DG_IRQ_NUM_FIRST + 49)
#define BL618DG_IRQ_NUM_PDS_WAKEUP                        (BL618DG_IRQ_NUM_FIRST + 50)
#define BL618DG_IRQ_NUM_HBN_OUT0                          (BL618DG_IRQ_NUM_FIRST + 51)
#define BL618DG_IRQ_NUM_HBN_OUT1                          (BL618DG_IRQ_NUM_FIRST + 52)
#define BL618DG_IRQ_NUM_BOD                               (BL618DG_IRQ_NUM_FIRST + 53)
#define BL618DG_IRQ_NUM_DMA2D                             (BL618DG_IRQ_NUM_FIRST + 54)
#define BL618DG_IRQ_NUM_BZ_PHY_INT                        (BL618DG_IRQ_NUM_FIRST + 55)
#define BL618DG_IRQ_NUM_BLE                               (BL618DG_IRQ_NUM_FIRST + 56)
#define BL618DG_IRQ_NUM_MAC_INT_TIMER                     (BL618DG_IRQ_NUM_FIRST + 57)
#define BL618DG_IRQ_NUM_MAC_INT_MISC                      (BL618DG_IRQ_NUM_FIRST + 58)
#define BL618DG_IRQ_NUM_MAC_INT_RX_TRIGGER                (BL618DG_IRQ_NUM_FIRST + 59)
#define BL618DG_IRQ_NUM_MAC_INT_TX_TRIGGER                (BL618DG_IRQ_NUM_FIRST + 60)
#define BL618DG_IRQ_NUM_MAC_INT_GEN                       (BL618DG_IRQ_NUM_FIRST + 61)
#define BL618DG_IRQ_NUM_MAC_INT_PROT_TRIGGER              (BL618DG_IRQ_NUM_FIRST + 62)
#define BL618DG_IRQ_NUM_WIFI_IPC                          (BL618DG_IRQ_NUM_FIRST + 63)
#define BL618DG_IRQ_NUM_IPC0_CH0                          (BL618DG_IRQ_NUM_FIRST + 64)
#define BL618DG_IRQ_NUM_IPC0_CH1                          (BL618DG_IRQ_NUM_FIRST + 65)
#define BL618DG_IRQ_NUM_IPC1_CH0                          (BL618DG_IRQ_NUM_FIRST + 66)
#define BL618DG_IRQ_NUM_IPC1_CH1                          (BL618DG_IRQ_NUM_FIRST + 67)
#define BL618DG_IRQ_NUM_CAN0                              (BL618DG_IRQ_NUM_FIRST + 68)
#define BL618DG_IRQ_NUM_CAN1                              (BL618DG_IRQ_NUM_FIRST + 69)
#define BL618DG_IRQ_NUM_CAN2                              (BL618DG_IRQ_NUM_FIRST + 70)
#define BL618DG_IRQ_NUM_DMA2_INT0                         (BL618DG_IRQ_NUM_FIRST + 71)
#define BL618DG_IRQ_NUM_DMA2_INT1                         (BL618DG_IRQ_NUM_FIRST + 72)
#define BL618DG_IRQ_NUM_DMA2_INT2                         (BL618DG_IRQ_NUM_FIRST + 73)
#define BL618DG_IRQ_NUM_DMA2_INT3                         (BL618DG_IRQ_NUM_FIRST + 74)
#define BL618DG_IRQ_NUM_PWM1                              (BL618DG_IRQ_NUM_FIRST + 79)
#define BL618DG_IRQ_NUM_I2C2                              (BL618DG_IRQ_NUM_FIRST + 80)
#define BL618DG_IRQ_NUM_TIMER2                            (BL618DG_IRQ_NUM_FIRST + 82)
#define BL618DG_IRQ_NUM_TIMER3                            (BL618DG_IRQ_NUM_FIRST + 83)
#define BL618DG_IRQ_NUM_WDG1                              (BL618DG_IRQ_NUM_FIRST + 84)
#define BL618DG_IRQ_NUM_UART3                             (BL618DG_IRQ_NUM_FIRST + 85)
#define BL618DG_IRQ_NUM_SPI3                              (BL618DG_IRQ_NUM_FIRST + 86)
#define BL618DG_IRQ_NUM_RASTER2BLOCK                      (BL618DG_IRQ_NUM_FIRST + 87)
#define BL618DG_IRQ_NUM_DTSRC                             (BL618DG_IRQ_NUM_FIRST + 88)
#define BL618DG_IRQ_NUM_OSD0                              (BL618DG_IRQ_NUM_FIRST + 89)
#define BL618DG_IRQ_NUM_OSD1                              (BL618DG_IRQ_NUM_FIRST + 90)
#define BL618DG_IRQ_NUM_MM_SEOF0                          (BL618DG_IRQ_NUM_FIRST + 91)
#define BL618DG_IRQ_NUM_MM_SEOF1                          (BL618DG_IRQ_NUM_FIRST + 92)
#define BL618DG_IRQ_NUM_DSI                               (BL618DG_IRQ_NUM_FIRST + 93)
#define BL618DG_IRQ_NUM_SPI1                              (BL618DG_IRQ_NUM_FIRST + 94)
#define BL618DG_IRQ_NUM_SPI2                              (BL618DG_IRQ_NUM_FIRST + 95)
#define BL618DG_IRQ_NUM_TOUCH                             (BL618DG_IRQ_NUM_FIRST + 96)
#define BL618DG_IRQ_NUM_AUDAC                             (BL618DG_IRQ_NUM_FIRST + 97)

#define NR_IRQS (BL618DG_IRQ_NUM_AUDAC + 1)

#endif /* __VENDOR_BOUFFALOLAB_CHIPS_BL618DG_INCLUDE_IRQ_H */
