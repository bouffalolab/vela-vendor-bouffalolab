# SPDX-License-Identifier: Apache-2.0
#
# I2C touch controllers of the Bouffalo SDK (bsp/common/touch), ported to
# NuttX I2C and polled into the NuttX touchscreen upper half.

if(NOT CONFIG_BL_COMPONENT_TOUCH)
  return()
endif()

set(TOUCH_SRCS touch_poll.c)

if(CONFIG_BL_COMPONENT_TOUCH_GT911)
  list(APPEND TOUCH_SRCS touch_gt911.c)
endif()

if(CONFIG_BL_COMPONENT_TOUCH_FT6X36)
  list(APPEND TOUCH_SRCS touch_ft6x36.c)
endif()

nuttx_add_kernel_library(bl_touch)
target_sources(bl_touch PRIVATE ${TOUCH_SRCS})
