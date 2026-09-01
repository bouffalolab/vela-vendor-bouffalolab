/****************************************************************************
 * apps/vendor/bouffalolab/boards/bl616cl/ai-m64l-32s-kit/src/ai_m64l_kit_uart.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include "bl616cl_uart.h"
#include "ai_m64l_kit.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AI_M64L_KIT_UART_PIN_RESERVED(pin)       \
  (((pin) >= 6 && (pin) <= 11) || (pin) == 21 || \
   ((pin) >= 32 && (pin) <= 36))

#if AI_M64L_KIT_UART1_TX_PIN == AI_M64L_KIT_UART1_RX_PIN
#error "UART1 TX and RX pins must be distinct"
#endif

#if AI_M64L_KIT_UART1_TX_PIN % 12 == AI_M64L_KIT_UART1_RX_PIN % 12
#error "UART1 TX and RX pins must use distinct signal slots"
#endif

#if AI_M64L_KIT_UART1_TX_PIN == CONFIG_BL616CL_UART0_TXPIN || \
  AI_M64L_KIT_UART1_TX_PIN == CONFIG_BL616CL_UART0_RXPIN ||   \
  AI_M64L_KIT_UART1_RX_PIN == CONFIG_BL616CL_UART0_TXPIN ||   \
  AI_M64L_KIT_UART1_RX_PIN == CONFIG_BL616CL_UART0_RXPIN
#error "UART1 pins must not overlap UART0 console pins"
#endif

#if AI_M64L_KIT_UART1_TX_PIN % 12 ==   \
    CONFIG_BL616CL_UART0_TXPIN % 12 || \
  AI_M64L_KIT_UART1_TX_PIN % 12 ==     \
    CONFIG_BL616CL_UART0_RXPIN % 12 || \
  AI_M64L_KIT_UART1_RX_PIN % 12 ==     \
    CONFIG_BL616CL_UART0_TXPIN % 12 || \
  AI_M64L_KIT_UART1_RX_PIN % 12 ==     \
    CONFIG_BL616CL_UART0_RXPIN % 12
#error "UART1 pins must not override UART0 console signal slots"
#endif

#if AI_M64L_KIT_UART_PIN_RESERVED(AI_M64L_KIT_UART1_TX_PIN) || \
  AI_M64L_KIT_UART_PIN_RESERVED(AI_M64L_KIT_UART1_RX_PIN)
#error "UART1 pin conflicts with a reserved board resource"
#endif

#if defined(CONFIG_AI_M64L_KIT_I2C0) && \
  (AI_M64L_KIT_UART1_TX_PIN ==          \
     CONFIG_AI_M64L_KIT_I2C0_SCL_PIN || \
   AI_M64L_KIT_UART1_TX_PIN ==          \
     CONFIG_AI_M64L_KIT_I2C0_SDA_PIN || \
   AI_M64L_KIT_UART1_RX_PIN ==          \
     CONFIG_AI_M64L_KIT_I2C0_SCL_PIN || \
   AI_M64L_KIT_UART1_RX_PIN ==          \
     CONFIG_AI_M64L_KIT_I2C0_SDA_PIN)
#error "UART1 and I2C0 pins must not overlap"
#endif

#if defined(CONFIG_AI_M64L_KIT_I2C1) && \
  (AI_M64L_KIT_UART1_TX_PIN ==          \
     CONFIG_AI_M64L_KIT_I2C1_SCL_PIN || \
   AI_M64L_KIT_UART1_TX_PIN ==          \
     CONFIG_AI_M64L_KIT_I2C1_SDA_PIN || \
   AI_M64L_KIT_UART1_RX_PIN ==          \
     CONFIG_AI_M64L_KIT_I2C1_SCL_PIN || \
   AI_M64L_KIT_UART1_RX_PIN ==          \
     CONFIG_AI_M64L_KIT_I2C1_SDA_PIN)
#error "UART1 and I2C1 pins must not overlap"
#endif

#if defined(CONFIG_AI_M64L_KIT_SPI0) &&  \
  (AI_M64L_KIT_UART1_TX_PIN ==           \
     CONFIG_AI_M64L_KIT_SPI0_CS_PIN ||   \
   AI_M64L_KIT_UART1_TX_PIN ==           \
     CONFIG_AI_M64L_KIT_SPI0_CLK_PIN ||  \
   AI_M64L_KIT_UART1_TX_PIN ==           \
     CONFIG_AI_M64L_KIT_SPI0_MISO_PIN || \
   AI_M64L_KIT_UART1_TX_PIN ==           \
     CONFIG_AI_M64L_KIT_SPI0_MOSI_PIN || \
   AI_M64L_KIT_UART1_RX_PIN ==           \
     CONFIG_AI_M64L_KIT_SPI0_CS_PIN ||   \
   AI_M64L_KIT_UART1_RX_PIN ==           \
     CONFIG_AI_M64L_KIT_SPI0_CLK_PIN ||  \
   AI_M64L_KIT_UART1_RX_PIN ==           \
     CONFIG_AI_M64L_KIT_SPI0_MISO_PIN || \
   AI_M64L_KIT_UART1_RX_PIN ==           \
     CONFIG_AI_M64L_KIT_SPI0_MOSI_PIN)
#error "UART1 and SPI0 pins must not overlap"
#endif

#if defined(CONFIG_AI_M64L_KIT_SPI0_TARGET1) && \
  (AI_M64L_KIT_UART1_TX_PIN ==                  \
     CONFIG_AI_M64L_KIT_SPI0_TARGET1_CS_PIN ||  \
   AI_M64L_KIT_UART1_RX_PIN ==                  \
     CONFIG_AI_M64L_KIT_SPI0_TARGET1_CS_PIN)
#error "UART1 and SPI0 target 1 CS pins must not overlap"
#endif

#if defined(CONFIG_AI_M64L_KIT_PWM) && \
  (AI_M64L_KIT_UART1_TX_PIN == 22 || AI_M64L_KIT_UART1_RX_PIN == 22)
#error "UART1 pins must not overlap the PWM GPIO22 output"
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ai_m64l_kit_uart_initialize(void)
{
  return bl616cl_uart1_register(AI_M64L_KIT_UART1_TX_PIN,
                                AI_M64L_KIT_UART1_RX_PIN);
}
