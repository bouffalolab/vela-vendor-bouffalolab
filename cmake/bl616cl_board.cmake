# SPDX-License-Identifier: Apache-2.0
#
# Build glue shared by every BL616CL board: link options, the application
# partition size, firmware post-processing, the Wi-Fi hot code check and the
# linker script. A board CMakeLists.txt includes this file before its src/
# and then calls bl616cl_board():
#
#   include(${NUTTX_DIR}/../vendor/bouffalolab/cmake/bl616cl_board.cmake)
#   add_subdirectory(src)
#   bl616cl_board(FW_HEADER g_<board>_fw_header)
#
# The board directory may live outside vendor/bouffalolab, for example in a
# customer repository at vendor/<customer>/boards/bl616cl/<board>. NuttX
# adds the common/ directory next to the board on its own; for any other
# board this file adds the SDK common directory, so the board keeps no copy.
#
# The board provides include/board.h, bl616cl_board_initialize(), a 256 byte
# .fw_header object named by FW_HEADER and config/ with the boot2 image,
# partition table and factory parameters.
#
# Sets BL616CL_VENDOR_DIR to the vendor/bouffalolab directory.

get_filename_component(BL616CL_VENDOR_DIR "${CMAKE_CURRENT_LIST_DIR}/.."
                       ABSOLUTE)

function(bl616cl_board)
  cmake_parse_arguments(ARG "" "FW_HEADER" "" ${ARGN})
  if(NOT ARG_FW_HEADER)
    message(FATAL_ERROR "bl616cl_board: FW_HEADER is required")
  endif()

  set(common_dir "${BL616CL_VENDOR_DIR}/boards/bl616cl/common")
  get_filename_component(board_parent "${NUTTX_BOARD_ABS_DIR}/.." REALPATH)
  get_filename_component(sdk_boards "${common_dir}/.." REALPATH)
  if(NOT board_parent STREQUAL sdk_boards)
    add_subdirectory(${common_dir} ${CMAKE_CURRENT_BINARY_DIR}/bl616cl_common)
  endif()

  # Warn when an input section is not placed by ld.script. RAM code shares
  # its load image with .data, and the image is flashed as a BIN on a core
  # without an MMU, so an RWX segment is expected; -n keeps the ELF headers
  # out of the load segments.
  target_link_options(
    nuttx PRIVATE -Wl,--build-id=none -Wl,--orphan-handling=warn -Wl,-n
    -Wl,--no-warn-rwx-segments -Wl,-u,${ARG_FW_HEADER})

  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(partition_config "${NUTTX_BOARD_ABS_DIR}/config/partition_cfg_4M.toml")
  set(partition_size_tool
      "${BL616CL_VENDOR_DIR}/tools/bl616cl/app_partition_size.py")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                                         ${partition_config} ${partition_size_tool})
  execute_process(
    COMMAND ${Python3_EXECUTABLE} ${partition_size_tool} ${partition_config}
    OUTPUT_VARIABLE app_size
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  target_link_options(nuttx PRIVATE -Wl,--defsym=__bl616cl_app_size=${app_size}
                      -Wl,--defsym=_wifi_log_flag=1)

  set(postprocess "${BL616CL_VENDOR_DIR}/tools/bl616cl/postprocess_bl616cl.sh")
  set(postprocess_stamp "${CMAKE_BINARY_DIR}/nuttx.postprocessed")
  set(raw_image "${CMAKE_BINARY_DIR}/nuttx.raw.bin")
  set(flash_tools "${BL616CL_VENDOR_DIR}/tools/bflb_flash_tools")
  set(postprocess_inputs
      ${NUTTX_BOARD_ABS_DIR}/config/bl_factory_params_IoTKitA_auto.dts
      ${NUTTX_BOARD_ABS_DIR}/config/boot2_bl616cl_isp_release_v8.2.1.bin
      ${partition_config}
      ${partition_size_tool}
      ${BL616CL_VENDOR_DIR}/tools/bl616cl/flash_factory_cfg.ini
      ${flash_tools}/bflb_fw_post_proc/bflb_fw_post_proc-ubuntu
      ${flash_tools}/bflb_fw_post_proc/bflb_fw_post_proc-macos
      ${flash_tools}/bflb_fw_post_proc/bflb_fw_post_proc.exe
      ${flash_tools}/bouffalo_flash_cube/BLFlashCommand-ubuntu
      ${flash_tools}/bouffalo_flash_cube/BLFlashCommand-macos
      ${flash_tools}/bouffalo_flash_cube/BLFlashCommand.exe
      ${flash_tools}/bouffalo_flash_cube/chips/bl616cl/eflash_loader/eflash_loader_cfg.conf
      ${flash_tools}/bouffalo_flash_cube/chips/bl616cl/efuse_bootheader/efuse_bootheader_cfg.conf
      ${flash_tools}/bouffalo_flash_cube/chips/bl616cl/efuse_bootheader/flash_para.bin
  )

  add_custom_command(
    OUTPUT ${raw_image}
    COMMAND ${CMAKE_OBJCOPY} -O binary ${CMAKE_BINARY_DIR}/final_nuttx
            ${raw_image}
    DEPENDS ${CMAKE_BINARY_DIR}/final_nuttx
    COMMENT "Generate BL616CL raw firmware image"
    VERBATIM)

  add_custom_command(
    OUTPUT ${postprocess_stamp}
    COMMAND bash ${postprocess} --raw-image ${raw_image} --image
            ${CMAKE_BINARY_DIR}/nuttx.bin --board-config ${NUTTX_BOARD_ABS_DIR}/config
    COMMAND ${CMAKE_COMMAND} -E touch ${postprocess_stamp}
    DEPENDS ${CMAKE_BINARY_DIR}/nuttx.bin ${raw_image} ${postprocess}
            ${postprocess_inputs}
    BYPRODUCTS ${CMAKE_BINARY_DIR}/nuttx.whole.bin
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Postprocess BL616CL firmware image"
    VERBATIM)

  set(post_build_outputs ${postprocess_stamp})

  # Warn when Wi-Fi hot list entries no longer match the image.
  if(CONFIG_BL616CL_WIRELESS_WIFI)
    set(layout_check "${BL616CL_VENDOR_DIR}/tools/bl616cl/perf/layout_check.py")
    set(layout_stamp "${CMAKE_BINARY_DIR}/nuttx.layout_checked")
    add_custom_command(
      OUTPUT ${layout_stamp}
      COMMAND ${Python3_EXECUTABLE} ${layout_check} --hot-list
              ${common_dir}/scripts/bl616cl_wifi_hot.ld --map
              ${CMAKE_BINARY_DIR}/nuttx.map
      COMMAND ${CMAKE_COMMAND} -E touch ${layout_stamp}
      DEPENDS ${CMAKE_BINARY_DIR}/final_nuttx ${layout_check}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Check BL616CL Wi-Fi hot code layout"
      VERBATIM)
    list(APPEND post_build_outputs ${layout_stamp})
  endif()

  add_custom_target(nuttx_post_build DEPENDS ${CMAKE_BINARY_DIR}/nuttx.bin
                                             ${raw_image} ${post_build_outputs})

  file(GLOB extra_libs
       ${NUTTX_BOARD_ABS_DIR}/libs/${CONFIG_ARCH_BOARD_CUSTOM_NAME}/*.a)
  set_property(GLOBAL APPEND PROPERTY NUTTX_EXTRA_LIBRARIES ${extra_libs})

  set_property(GLOBAL PROPERTY LD_SCRIPT ${common_dir}/scripts/ld.script)
endfunction()
