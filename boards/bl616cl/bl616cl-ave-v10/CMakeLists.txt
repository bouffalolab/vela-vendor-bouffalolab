# ##############################################################################
# vendor/bouffalolab/boards/bl616cl/bl616cl-ave-v10/CMakeLists.txt
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

# Shared BL616CL board build glue: common, link options, post-processing and
# the linker script. Include it before src/ so that src/ can use
# BL616CL_VENDOR_DIR.
include(${NUTTX_DIR}/../vendor/bouffalolab/cmake/bl616cl_board.cmake)

add_subdirectory(src)

bl616cl_board(FW_HEADER g_bl616cl_ave_v10_fw_header)
