/****************************************************************************
 * apps/vendor/bouffalolab/boards/bl616cl/ai-m64l-32s-kit/src/ai_m64l_kit_spi.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/param.h>

#include <nuttx/spi/spi.h>
#include <nuttx/spi/spi_transfer.h>

#include "bflb_gpio.h"
#include "bflb_name.h"
#include "bl616cl_spi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AI_M64L_KIT_SPI_GPIO_CFG \
  (GPIO_FUNC_SPI0 | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)
#define AI_M64L_KIT_SPI_CS_CFG \
  (GPIO_OUTPUT | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1)

#define AI_M64L_KIT_SPI_PIN_RESERVED(pin)        \
  (((pin) >= 6 && (pin) <= 11) || (pin) == 21 || \
   ((pin) >= 32 && (pin) <= 36))

#if CONFIG_AI_M64L_KIT_SPI0_CLK_PIN % 4 != 1
#error "SPI0 CLK pin must map to signal 1"
#endif
#if CONFIG_AI_M64L_KIT_SPI0_MISO_PIN % 4 != 2 && \
  CONFIG_AI_M64L_KIT_SPI0_MISO_PIN % 4 != 3
#error "SPI0 MISO pin must map to signal 2 or 3"
#endif
#if CONFIG_AI_M64L_KIT_SPI0_MOSI_PIN % 4 != 2 && \
  CONFIG_AI_M64L_KIT_SPI0_MOSI_PIN % 4 != 3
#error "SPI0 MOSI pin must map to signal 2 or 3"
#endif
#if CONFIG_AI_M64L_KIT_SPI0_MISO_PIN % 4 == \
  CONFIG_AI_M64L_KIT_SPI0_MOSI_PIN % 4
#error "SPI0 MISO and MOSI must use different signals"
#endif
#if CONFIG_AI_M64L_KIT_SPI0_CS_PIN == CONFIG_AI_M64L_KIT_SPI0_CLK_PIN || \
  CONFIG_AI_M64L_KIT_SPI0_CS_PIN == CONFIG_AI_M64L_KIT_SPI0_MISO_PIN ||  \
  CONFIG_AI_M64L_KIT_SPI0_CS_PIN == CONFIG_AI_M64L_KIT_SPI0_MOSI_PIN ||  \
  CONFIG_AI_M64L_KIT_SPI0_CLK_PIN == CONFIG_AI_M64L_KIT_SPI0_MISO_PIN || \
  CONFIG_AI_M64L_KIT_SPI0_CLK_PIN == CONFIG_AI_M64L_KIT_SPI0_MOSI_PIN || \
  CONFIG_AI_M64L_KIT_SPI0_MISO_PIN == CONFIG_AI_M64L_KIT_SPI0_MOSI_PIN
#error "SPI0 pins must be distinct"
#endif
#if AI_M64L_KIT_SPI_PIN_RESERVED(CONFIG_AI_M64L_KIT_SPI0_CS_PIN) || \
  AI_M64L_KIT_SPI_PIN_RESERVED(CONFIG_AI_M64L_KIT_SPI0_CLK_PIN) ||  \
  AI_M64L_KIT_SPI_PIN_RESERVED(CONFIG_AI_M64L_KIT_SPI0_MISO_PIN) || \
  AI_M64L_KIT_SPI_PIN_RESERVED(CONFIG_AI_M64L_KIT_SPI0_MOSI_PIN)
#error "SPI0 pin conflicts with a reserved board resource"
#endif

#if defined(CONFIG_AI_M64L_KIT_I2C0) &&                                   \
  (CONFIG_AI_M64L_KIT_SPI0_CS_PIN == CONFIG_AI_M64L_KIT_I2C0_SCL_PIN ||   \
   CONFIG_AI_M64L_KIT_SPI0_CS_PIN == CONFIG_AI_M64L_KIT_I2C0_SDA_PIN ||   \
   CONFIG_AI_M64L_KIT_SPI0_CLK_PIN == CONFIG_AI_M64L_KIT_I2C0_SCL_PIN ||  \
   CONFIG_AI_M64L_KIT_SPI0_CLK_PIN == CONFIG_AI_M64L_KIT_I2C0_SDA_PIN ||  \
   CONFIG_AI_M64L_KIT_SPI0_MISO_PIN == CONFIG_AI_M64L_KIT_I2C0_SCL_PIN || \
   CONFIG_AI_M64L_KIT_SPI0_MISO_PIN == CONFIG_AI_M64L_KIT_I2C0_SDA_PIN || \
   CONFIG_AI_M64L_KIT_SPI0_MOSI_PIN == CONFIG_AI_M64L_KIT_I2C0_SCL_PIN || \
   CONFIG_AI_M64L_KIT_SPI0_MOSI_PIN == CONFIG_AI_M64L_KIT_I2C0_SDA_PIN)
#error "SPI0 and I2C0 pins must not overlap"
#endif

#if defined(CONFIG_AI_M64L_KIT_I2C1) &&                                   \
  (CONFIG_AI_M64L_KIT_SPI0_CS_PIN == CONFIG_AI_M64L_KIT_I2C1_SCL_PIN ||   \
   CONFIG_AI_M64L_KIT_SPI0_CS_PIN == CONFIG_AI_M64L_KIT_I2C1_SDA_PIN ||   \
   CONFIG_AI_M64L_KIT_SPI0_CLK_PIN == CONFIG_AI_M64L_KIT_I2C1_SCL_PIN ||  \
   CONFIG_AI_M64L_KIT_SPI0_CLK_PIN == CONFIG_AI_M64L_KIT_I2C1_SDA_PIN ||  \
   CONFIG_AI_M64L_KIT_SPI0_MISO_PIN == CONFIG_AI_M64L_KIT_I2C1_SCL_PIN || \
   CONFIG_AI_M64L_KIT_SPI0_MISO_PIN == CONFIG_AI_M64L_KIT_I2C1_SDA_PIN || \
   CONFIG_AI_M64L_KIT_SPI0_MOSI_PIN == CONFIG_AI_M64L_KIT_I2C1_SCL_PIN || \
   CONFIG_AI_M64L_KIT_SPI0_MOSI_PIN == CONFIG_AI_M64L_KIT_I2C1_SDA_PIN)
#error "SPI0 and I2C1 pins must not overlap"
#endif

#ifdef CONFIG_AI_M64L_KIT_SPI0_TARGET1
#if CONFIG_AI_M64L_KIT_SPI0_TARGET1_CS_PIN == \
    CONFIG_AI_M64L_KIT_SPI0_CS_PIN ||         \
  CONFIG_AI_M64L_KIT_SPI0_TARGET1_CS_PIN ==   \
    CONFIG_AI_M64L_KIT_SPI0_CLK_PIN ||        \
  CONFIG_AI_M64L_KIT_SPI0_TARGET1_CS_PIN ==   \
    CONFIG_AI_M64L_KIT_SPI0_MISO_PIN ||       \
  CONFIG_AI_M64L_KIT_SPI0_TARGET1_CS_PIN ==   \
    CONFIG_AI_M64L_KIT_SPI0_MOSI_PIN
#error "SPI0 target 1 CS must use a distinct GPIO"
#endif
#if CONFIG_AI_M64L_KIT_SPI0_TARGET1_INDEX == \
  CONFIG_AI_M64L_KIT_SPI0_TARGET_INDEX
#error "SPI0 target indices must be distinct"
#endif
#if AI_M64L_KIT_SPI_PIN_RESERVED( \
  CONFIG_AI_M64L_KIT_SPI0_TARGET1_CS_PIN)
#error "SPI0 target 1 CS conflicts with a reserved board resource"
#endif
#if defined(CONFIG_AI_M64L_KIT_I2C0) &&      \
  (CONFIG_AI_M64L_KIT_SPI0_TARGET1_CS_PIN == \
     CONFIG_AI_M64L_KIT_I2C0_SCL_PIN ||      \
   CONFIG_AI_M64L_KIT_SPI0_TARGET1_CS_PIN == \
     CONFIG_AI_M64L_KIT_I2C0_SDA_PIN)
#error "SPI0 target 1 CS must not overlap I2C0"
#endif
#if defined(CONFIG_AI_M64L_KIT_I2C1) &&      \
  (CONFIG_AI_M64L_KIT_SPI0_TARGET1_CS_PIN == \
     CONFIG_AI_M64L_KIT_I2C1_SCL_PIN ||      \
   CONFIG_AI_M64L_KIT_SPI0_TARGET1_CS_PIN == \
     CONFIG_AI_M64L_KIT_I2C1_SDA_PIN)
#error "SPI0 target 1 CS must not overlap I2C1"
#endif
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct ai_m64l_kit_spi_target_s
{
  uint32_t devid;
  uint8_t cs_pin;
  bool active_high;
};

struct ai_m64l_kit_spi_bus_s
{
  struct bflb_device_s *gpio;
  const struct ai_m64l_kit_spi_target_s *targets;
  size_t ntargets;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct ai_m64l_kit_spi_target_s
g_ai_m64l_kit_spi0_targets[] =
{
  {
    .devid = SPIDEV_USER(CONFIG_AI_M64L_KIT_SPI0_TARGET_INDEX),
    .cs_pin = CONFIG_AI_M64L_KIT_SPI0_CS_PIN,
#ifdef CONFIG_AI_M64L_KIT_SPI0_CS_ACTIVE_HIGH
    .active_high = true,
#endif
  },
#ifdef CONFIG_AI_M64L_KIT_SPI0_TARGET1
  {
    .devid = SPIDEV_USER(CONFIG_AI_M64L_KIT_SPI0_TARGET1_INDEX),
    .cs_pin = CONFIG_AI_M64L_KIT_SPI0_TARGET1_CS_PIN,
#ifdef CONFIG_AI_M64L_KIT_SPI0_TARGET1_CS_ACTIVE_HIGH
    .active_high = true,
#endif
  },
#endif
};

static struct ai_m64l_kit_spi_bus_s g_ai_m64l_kit_spi0 =
{
  .targets = g_ai_m64l_kit_spi0_targets,
  .ntargets = nitems(g_ai_m64l_kit_spi0_targets),
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void ai_m64l_kit_spi_set_cs(
  struct ai_m64l_kit_spi_bus_s *bus,
  const struct ai_m64l_kit_spi_target_s *target, bool selected)
{
  bool asserted;

  asserted = selected == target->active_high;
  if (asserted)
    {
      bflb_gpio_set(bus->gpio, target->cs_pin);
    }
  else
    {
      bflb_gpio_reset(bus->gpio, target->cs_pin);
    }
}

static bool ai_m64l_kit_spi_select(void *arg, uint32_t devid, bool selected)
{
  struct ai_m64l_kit_spi_bus_s *bus = arg;
  size_t i;

  for (i = 0; i < bus->ntargets; i++)
    {
      if (devid == bus->targets[i].devid)
        {
          ai_m64l_kit_spi_set_cs(bus, &bus->targets[i], selected);
          return true;
        }
    }

  return false;
}

static const struct bl616cl_spi_board_ops_s g_ai_m64l_kit_spi_ops =
{
  .select = ai_m64l_kit_spi_select,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ai_m64l_kit_spi_initialize(void)
{
  struct spi_dev_s *spi;
  size_t i;
  int ret;

  g_ai_m64l_kit_spi0.gpio = bflb_device_get_by_name(BFLB_NAME_GPIO);
  if (g_ai_m64l_kit_spi0.gpio == NULL)
    {
      return -ENODEV;
    }

  ret = bl616cl_spi_configure_pins(0,
                                   CONFIG_AI_M64L_KIT_SPI0_MISO_PIN,
                                   CONFIG_AI_M64L_KIT_SPI0_MOSI_PIN);
  if (ret < 0)
    {
      return ret;
    }

  for (i = 0; i < g_ai_m64l_kit_spi0.ntargets; i++)
    {
      const struct ai_m64l_kit_spi_target_s *target =
        &g_ai_m64l_kit_spi0.targets[i];

      /* Set the inactive level before enabling the output driver. */

      ai_m64l_kit_spi_set_cs(&g_ai_m64l_kit_spi0, target, false);
      bflb_gpio_init(g_ai_m64l_kit_spi0.gpio, target->cs_pin,
                     AI_M64L_KIT_SPI_CS_CFG);
    }

  bflb_gpio_init(g_ai_m64l_kit_spi0.gpio,
                 CONFIG_AI_M64L_KIT_SPI0_CLK_PIN,
                 AI_M64L_KIT_SPI_GPIO_CFG);
  bflb_gpio_init(g_ai_m64l_kit_spi0.gpio,
                 CONFIG_AI_M64L_KIT_SPI0_MISO_PIN,
                 AI_M64L_KIT_SPI_GPIO_CFG);
  bflb_gpio_init(g_ai_m64l_kit_spi0.gpio,
                 CONFIG_AI_M64L_KIT_SPI0_MOSI_PIN,
                 AI_M64L_KIT_SPI_GPIO_CFG);

  spi = bl616cl_spibus_initialize(0, &g_ai_m64l_kit_spi_ops,
                                  &g_ai_m64l_kit_spi0);
  if (spi == NULL)
    {
      return -ENODEV;
    }

  ret = spi_register(spi, 0);
  if (ret < 0)
    {
      (void)bl616cl_spibus_uninitialize(spi);
    }

  return ret;
}
