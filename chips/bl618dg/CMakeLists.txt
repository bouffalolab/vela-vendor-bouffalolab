# ##############################################################################
# vendor/bouffalolab/chips/bl618dg/CMakeLists.txt
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

if(NOT CONFIG_BUILD_FLAT OR NOT CONFIG_ARCH_DPFPU)
  message(FATAL_ERROR "BL618DG requires a flat, ilp32d build")
endif()

target_sources(arch PRIVATE bl618dg_head.S bl618dg_start.c bl618dg_irq.c
                            bl618dg_timerisr.c bl618dg_memory.c)

# Nuclei core register definitions.
target_include_directories(
  arch SYSTEM PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../../drivers/lhal/include/arch/risc-v/n900/Core/Include)
target_compile_definitions(arch PRIVATE BL618DG CPU_AP)

set_property(
  SOURCE ${CMAKE_CURRENT_LIST_DIR}/bl618dg_start.c
         ${CMAKE_CURRENT_LIST_DIR}/bl618dg_memory.c
  TARGET_DIRECTORY arch APPEND PROPERTY COMPILE_OPTIONS
  -fno-sanitize=all -fno-stack-protector -fno-builtin)
