# SPDX-License-Identifier: Apache-2.0

set(BL_COMPONENT_HELPER_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(bl_component_abs_path out_var path)
  if(IS_ABSOLUTE "${path}")
    set(result "${path}")
  else()
    set(result "${CMAKE_CURRENT_LIST_DIR}/${path}")
  endif()

  set(${out_var}
      "${result}"
      PARENT_SCOPE)
endfunction()

function(bl_component_chip out_var)
  if(DEFINED CHIP AND NOT "${CHIP}" STREQUAL "")
    set(chip "${CHIP}")
  elseif(DEFINED CONFIG_ARCH_CHIP_CUSTOM_NAME
         AND NOT "${CONFIG_ARCH_CHIP_CUSTOM_NAME}" STREQUAL "")
    set(chip "${CONFIG_ARCH_CHIP_CUSTOM_NAME}")
  elseif(DEFINED CONFIG_ARCH_CHIP AND NOT "${CONFIG_ARCH_CHIP}" STREQUAL "")
    set(chip "${CONFIG_ARCH_CHIP}")
  elseif(DEFINED BOARD_CONFIG)
    string(REGEX MATCH "(^|/)boards/([^/]+)/" chip_match "${BOARD_CONFIG}")
    set(chip "${CMAKE_MATCH_2}")
  endif()

  string(TOLOWER "${chip}" chip)
  set(${out_var}
      "${chip}"
      PARENT_SCOPE)
endfunction()

function(bl_component_option_name out_var name)
  string(TOUPPER "${name}" option)
  string(REGEX REPLACE "[^A-Z0-9]" "_" option "${option}")
  set(${out_var}
      "CONFIG_BL_${option}"
      PARENT_SCOPE)
endfunction()

function(bl_component_target_name out_var name)
  string(REGEX REPLACE "[^A-Za-z0-9_]" "_" suffix "${name}")
  set(${out_var}
      "bl_${suffix}"
      PARENT_SCOPE)
endfunction()

function(bl_add_component)
  cmake_parse_arguments(BL "" "NAME;SOURCE_DIR;LIBS_DIR;KCONFIG" "DEPENDS"
                        ${ARGN})

  if("${BL_NAME}" STREQUAL "")
    message(FATAL_ERROR "bl_add_component() requires NAME")
  endif()

  if("${BL_SOURCE_DIR}" STREQUAL "")
    set(BL_SOURCE_DIR "${BL_NAME}")
  endif()

  if("${BL_LIBS_DIR}" STREQUAL "")
    set(BL_LIBS_DIR "libs")
  endif()

  if("${BL_KCONFIG}" STREQUAL "")
    bl_component_option_name(BL_KCONFIG "${BL_NAME}")
  endif()

  if(NOT DEFINED ${BL_KCONFIG} OR NOT ${${BL_KCONFIG}})
    return()
  endif()

  bl_component_abs_path(source_dir "${BL_SOURCE_DIR}")
  bl_component_abs_path(libs_dir "${BL_LIBS_DIR}")
  bl_component_chip(chip)
  bl_component_target_name(target "${BL_NAME}")

  if("${chip}" STREQUAL "")
    message(FATAL_ERROR "cannot resolve chip for ${BL_NAME}")
  endif()

  set(force_libs ${BL_USE_LIB_COMPONENTS})
  string(REPLACE "," ";" force_libs "${force_libs}")
  list(FIND force_libs "${BL_NAME}" force_lib_index)

  if(force_lib_index GREATER -1)
    set(use_lib TRUE)
    if(IS_DIRECTORY "${source_dir}")
      message(WARNING "${BL_NAME} source exists but --use-lib selected")
    endif()
  elseif(IS_DIRECTORY "${source_dir}")
    set(use_lib FALSE)
  else()
    set(use_lib TRUE)
  endif()

  if(use_lib)
    set(lib_path "${libs_dir}/${chip}/lib${BL_NAME}.a")
    set(include_dir "${libs_dir}/include")

    if(NOT EXISTS "${lib_path}")
      message(FATAL_ERROR "missing prebuilt library: ${lib_path}")
    endif()

    if(NOT IS_DIRECTORY "${include_dir}")
      message(FATAL_ERROR "missing prebuilt headers: ${include_dir}")
    endif()

    nuttx_add_extra_library("${lib_path}")
    nuttx_export_header(TARGET ${target} INCLUDE_DIRECTORIES "${include_dir}")
    return()
  endif()

  set(source_include "${source_dir}/include")
  set(source_src "${source_dir}/src")

  if(NOT IS_DIRECTORY "${source_include}")
    message(FATAL_ERROR "missing source headers: ${source_include}")
  endif()

  if(NOT IS_DIRECTORY "${source_src}")
    message(FATAL_ERROR "missing source directory: ${source_src}")
  endif()

  file(GLOB component_sources CONFIGURE_DEPENDS "${source_src}/*.c")
  if("${component_sources}" STREQUAL "")
    message(FATAL_ERROR "missing source files: ${source_src}")
  endif()

  nuttx_add_library(${target} STATIC)
  target_sources(${target} PRIVATE ${component_sources})
  target_include_directories(${target} PRIVATE "${source_include}")
  nuttx_export_header(TARGET ${target} INCLUDE_DIRECTORIES "${source_include}")

  if(BL_DEPENDS)
    nuttx_add_dependencies(TARGET ${target} DEPENDS ${BL_DEPENDS})
  endif()

  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${libs_dir}/${chip}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${target}>
            "${libs_dir}/${chip}/lib${BL_NAME}.a"
    COMMAND
      ${CMAKE_COMMAND} "-DSOURCE_DIR=${source_include}"
      "-DDESTINATION_DIR=${libs_dir}/include" -P
      "${BL_COMPONENT_HELPER_DIR}/bl_sync_directory.cmake")
endfunction()
