# ##############################################################################
# vendor/bouffalolab/boards/bl618dg/bl618dgdk/CMakeLists.txt
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

set_property(GLOBAL PROPERTY LD_SCRIPT
             ${CMAKE_CURRENT_LIST_DIR}/scripts/ld.script)

# RAM code shares a load image with data on a core without an MMU.
# Keep orphan warnings and let ld derive the expected segment permissions.
target_link_options(nuttx PRIVATE -Wl,--build-id=none -Wl,-n
                    -Wl,--orphan-handling=warn -Wl,--no-warn-rwx-segments
                    -Wl,-u,g_bl618dgdk_fw_header)

get_filename_component(BOUFFALOLAB_VENDOR_DIR "${NUTTX_BOARD_ABS_DIR}/../../.."
                       ABSOLUTE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(BL618DG_PARTITION_CONFIG
    "${NUTTX_BOARD_ABS_DIR}/config/partition_cfg_4M.toml")
set(BL618DG_PARTITION_SIZE_TOOL
    "${BOUFFALOLAB_VENDOR_DIR}/tools/bl616cl/app_partition_size.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${BL618DG_PARTITION_CONFIG} ${BL618DG_PARTITION_SIZE_TOOL})
execute_process(
  COMMAND ${Python3_EXECUTABLE} ${BL618DG_PARTITION_SIZE_TOOL}
          ${BL618DG_PARTITION_CONFIG}
  OUTPUT_VARIABLE BL618DG_APP_SIZE
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
target_link_options(nuttx PRIVATE
                    -Wl,--defsym=__bl618dg_app_size=${BL618DG_APP_SIZE})

set(BL618DG_POSTPROCESS
    "${BOUFFALOLAB_VENDOR_DIR}/tools/bl616cl/postprocess_bl616cl.sh")
set(BL618DG_POSTPROCESS_STAMP "${CMAKE_BINARY_DIR}/nuttx.postprocessed")
set(BL618DG_RAW_IMAGE "${CMAKE_BINARY_DIR}/nuttx.raw.bin")
set(BL618DG_POSTPROCESS_INPUTS
    ${NUTTX_BOARD_ABS_DIR}/config/bl_factory_params_IoTKitA_auto.dts
    ${NUTTX_BOARD_ABS_DIR}/config/boot2_bl618dg_isp_release_v8.2.5.bin
    ${BL618DG_PARTITION_CONFIG}
    ${BL618DG_PARTITION_SIZE_TOOL}
    ${BOUFFALOLAB_VENDOR_DIR}/tools/bl616cl/flash_factory_cfg.ini)

foreach(BL618DG_HOST_SUFFIX IN ITEMS -ubuntu -macos .exe)
  list(APPEND BL618DG_POSTPROCESS_INPUTS
       ${BOUFFALOLAB_VENDOR_DIR}/tools/bflb_flash_tools/bflb_fw_post_proc/bflb_fw_post_proc${BL618DG_HOST_SUFFIX}
       ${BOUFFALOLAB_VENDOR_DIR}/tools/bflb_flash_tools/bouffalo_flash_cube/BLFlashCommand${BL618DG_HOST_SUFFIX})
endforeach()
list(APPEND BL618DG_POSTPROCESS_INPUTS
     ${BOUFFALOLAB_VENDOR_DIR}/tools/bflb_flash_tools/bouffalo_flash_cube/chips/bl618dg/eflash_loader/eflash_loader_cfg.conf
     ${BOUFFALOLAB_VENDOR_DIR}/tools/bflb_flash_tools/bouffalo_flash_cube/chips/bl618dg/efuse_bootheader/efuse_bootheader_cfg.conf
     ${BOUFFALOLAB_VENDOR_DIR}/tools/bflb_flash_tools/bouffalo_flash_cube/chips/bl618dg/efuse_bootheader/flash_para.bin)

if(CONFIG_ALLSYMS OR CONFIG_MM_KASAN_GLOBAL)
  set(BL618DG_FINAL_TARGET final_nuttx)
else()
  set(BL618DG_FINAL_TARGET nuttx)
endif()

add_custom_command(
  OUTPUT ${BL618DG_RAW_IMAGE}
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${BL618DG_FINAL_TARGET}>
          ${BL618DG_RAW_IMAGE}
  DEPENDS ${BL618DG_FINAL_TARGET}
  COMMENT "Generate BL618DG raw firmware image"
  VERBATIM)

add_custom_command(
  OUTPUT ${BL618DG_POSTPROCESS_STAMP}
  COMMAND
    bash ${BL618DG_POSTPROCESS} --chip bl618dg --raw-image ${BL618DG_RAW_IMAGE}
    --image ${CMAKE_BINARY_DIR}/nuttx.bin
    --board-config ${NUTTX_BOARD_ABS_DIR}/config
  COMMAND ${CMAKE_COMMAND} -E touch ${BL618DG_POSTPROCESS_STAMP}
  DEPENDS ${CMAKE_BINARY_DIR}/nuttx.bin ${BL618DG_RAW_IMAGE}
          ${BL618DG_POSTPROCESS} ${BL618DG_POSTPROCESS_INPUTS}
  BYPRODUCTS ${CMAKE_BINARY_DIR}/nuttx.whole.bin
             ${CMAKE_BINARY_DIR}/boot2_bl618dg_isp_release_v8.2.5.bin
             ${CMAKE_BINARY_DIR}/partition.bin
             ${CMAKE_BINARY_DIR}/flash_prog_cfg.ini
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Postprocess BL618DG firmware image"
  VERBATIM)

add_custom_target(
  nuttx_post_build DEPENDS ${CMAKE_BINARY_DIR}/nuttx.bin ${BL618DG_RAW_IMAGE}
                          ${BL618DG_POSTPROCESS_STAMP})
