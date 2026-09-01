# ##############################################################################
# vendor/bouffalolab/boards/bl616cl/ai-m64l-32s-kit/src/CMakeLists.txt
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################

set(SRCS ai_m64l_kit_board.c ai_m64l_kit_fw_header.c)

if(CONFIG_AI_M64L_KIT_UART1)
  list(APPEND SRCS ai_m64l_kit_uart.c)
endif()

if(CONFIG_BL616CL_GPIO)
  list(APPEND SRCS ai_m64l_kit_gpio.c)
endif()

if(CONFIG_AI_M64L_KIT_I2C0 OR CONFIG_AI_M64L_KIT_I2C1)
  list(APPEND SRCS ai_m64l_kit_i2c.c)
endif()

if(CONFIG_AI_M64L_KIT_SPI0)
  list(APPEND SRCS ai_m64l_kit_spi.c)
  target_compile_definitions(board PRIVATE BL616CL)
endif()

nuttx_add_romfs(
  NAME
  etc
  MOUNTPOINT
  etc
  RCSRCS
  etc/init.d/rcS
  etc/init.d/rc.sysinit
  RCRAWS
  etc/group
  PATH
  ${CMAKE_CURRENT_BINARY_DIR}/etc)

target_link_libraries(board PRIVATE romfs_etc)

target_sources(board PRIVATE ${SRCS})
