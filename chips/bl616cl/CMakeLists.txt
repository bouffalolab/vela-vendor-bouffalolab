# ##############################################################################
# vendor/bouffalolab/chips/bl616cl/CMakeLists.txt
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

set(SRCS
    bl616cl_head.S
    bl616cl_start.c
    bl616cl_bod.c
    bl616cl_bus.c
    bl616cl_cpu.c
    bl616cl_flash.c
    bl616cl_memory.c
    bl616cl_cache.c
    bl616cl_clock.c
    bl616cl_irq.c
    bl616cl_irq_adapter.c
    bl616cl_irq_dispatch.c
    bl616cl_lowputc.c
    bl616cl_serial.c
    bl616cl_sec_mutex.c
    bl616cl_systemreset.c
    bl616cl_timerisr.c
    bl616cl_allocateheap.c)

if(CONFIG_BL616CL_WDT)
  list(APPEND SRCS bl616cl_wdt.c)
endif()

if(CONFIG_BL616CL_TRNG)
  list(APPEND SRCS bl616cl_rng.c)
endif()

if(CONFIG_BL616CL_SPI0 OR CONFIG_BL616CL_SPI1)
  list(APPEND SRCS bl616cl_spi.c)
endif()

if(CONFIG_BL616CL_RTC)
  list(APPEND SRCS bl616cl_rtc.c bl616cl_rtc_hw.c)
endif()

if(CONFIG_BL616CL_GPIO)
  list(APPEND SRCS bl616cl_gpio.c)
endif()

if(CONFIG_BL616CL_ONESHOT)
  list(APPEND SRCS bl616cl_oneshot.c)
endif()

if(CONFIG_BL616CL_TIMER)
  list(APPEND SRCS bl616cl_tim.c)
endif()

target_sources(arch PRIVATE ${SRCS})

target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/include)

target_compile_definitions(arch PRIVATE BL616CL)
