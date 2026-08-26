# ##############################################################################
# vendor/bouffalolab/boards/bl616cl/ai-m64l-32s-kit/CMakeLists.txt
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

add_subdirectory(src)

target_link_options(nuttx PRIVATE -Wl,--build-id=none
                    -Wl,-u,g_ai_m64l_kit_fw_header)

get_filename_component(BOUFFALOLAB_VENDOR_DIR "${NUTTX_BOARD_ABS_DIR}/../../.."
                       ABSOLUTE)
set(BL616CL_POSTPROCESS
    "${BOUFFALOLAB_VENDOR_DIR}/tools/ai-m64l-32s-kit/postprocess_bl616cl.sh")
set(BL616CL_POSTPROCESS_STAMP "${CMAKE_BINARY_DIR}/nuttx.postprocessed")
set(BL616CL_RAW_IMAGE "${CMAKE_BINARY_DIR}/nuttx.raw.bin")
set(BL616CL_POSTPROCESS_INPUTS
    ${NUTTX_BOARD_ABS_DIR}/config/bl_factory_params_IoTKitA_auto.dts
    ${NUTTX_BOARD_ABS_DIR}/config/boot2_bl616cl_isp_release_v8.2.1.bin
    ${NUTTX_BOARD_ABS_DIR}/config/partition_cfg_4M.toml
    ${BOUFFALOLAB_VENDOR_DIR}/tools/ai-m64l-32s-kit/flash_factory_cfg.ini
    ${BOUFFALOLAB_VENDOR_DIR}/tools/bflb_fw_post_proc/bflb_fw_post_proc-ubuntu
    ${BOUFFALOLAB_VENDOR_DIR}/tools/bflb_fw_post_proc/bflb_fw_post_proc-macos
    ${BOUFFALOLAB_VENDOR_DIR}/tools/bflb_fw_post_proc/bflb_fw_post_proc.exe
    ${BOUFFALOLAB_VENDOR_DIR}/tools/bouffalo_flash_cube/BLFlashCommand-ubuntu
    ${BOUFFALOLAB_VENDOR_DIR}/tools/bouffalo_flash_cube/BLFlashCommand-macos
    ${BOUFFALOLAB_VENDOR_DIR}/tools/bouffalo_flash_cube/BLFlashCommand.exe
    ${BOUFFALOLAB_VENDOR_DIR}/tools/bouffalo_flash_cube/chips/bl616cl/eflash_loader/eflash_loader_cfg.conf
    ${BOUFFALOLAB_VENDOR_DIR}/tools/bouffalo_flash_cube/chips/bl616cl/efuse_bootheader/efuse_bootheader_cfg.conf
    ${BOUFFALOLAB_VENDOR_DIR}/tools/bouffalo_flash_cube/chips/bl616cl/efuse_bootheader/flash_para.bin
)

add_custom_command(
  OUTPUT ${BL616CL_RAW_IMAGE}
  COMMAND ${CMAKE_OBJCOPY} -O binary ${CMAKE_BINARY_DIR}/final_nuttx
          ${BL616CL_RAW_IMAGE}
  DEPENDS ${CMAKE_BINARY_DIR}/final_nuttx
  COMMENT "Generate BL616CL raw firmware image"
  VERBATIM)

add_custom_command(
  OUTPUT ${BL616CL_POSTPROCESS_STAMP}
  COMMAND
    bash ${BL616CL_POSTPROCESS} --raw-image ${BL616CL_RAW_IMAGE} --image
    ${CMAKE_BINARY_DIR}/nuttx.bin --board-config ${NUTTX_BOARD_ABS_DIR}/config
  COMMAND ${CMAKE_COMMAND} -E touch ${BL616CL_POSTPROCESS_STAMP}
  DEPENDS ${CMAKE_BINARY_DIR}/nuttx.bin ${BL616CL_RAW_IMAGE}
          ${BL616CL_POSTPROCESS} ${BL616CL_POSTPROCESS_INPUTS}
  BYPRODUCTS ${CMAKE_BINARY_DIR}/nuttx.whole.bin
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Postprocess BL616CL firmware image"
  VERBATIM)

add_custom_target(
  nuttx_post_build DEPENDS ${CMAKE_BINARY_DIR}/nuttx.bin ${BL616CL_RAW_IMAGE}
                           ${BL616CL_POSTPROCESS_STAMP})

file(GLOB EXTRA_LIBS
     ${NUTTX_BOARD_ABS_DIR}/libs/${CONFIG_ARCH_BOARD_CUSTOM_NAME}/*.a)

set_property(GLOBAL APPEND PROPERTY NUTTX_EXTRA_LIBRARIES ${EXTRA_LIBS})

set_property(GLOBAL PROPERTY LD_SCRIPT ${NUTTX_BOARD_ABS_DIR}/scripts/ld.script)
