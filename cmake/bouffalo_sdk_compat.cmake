# SPDX-License-Identifier: Apache-2.0

function(bl_normalize_paths out_var)
  set(paths)
  foreach(path IN LISTS ARGN)
    if(IS_ABSOLUTE "${path}")
      list(APPEND paths "${path}")
    else()
      list(APPEND paths "${CMAKE_CURRENT_SOURCE_DIR}/${path}")
    endif()
  endforeach()
  set(${out_var}
      ${paths}
      PARENT_SCOPE)
endfunction()

function(bl_include_directories)
  cmake_parse_arguments(BL "" "SCOPE" "DIRECTORIES" ${ARGN})
  bl_normalize_paths(paths ${BL_DIRECTORIES})

  if("${BL_SCOPE}" STREQUAL "APPS" OR "${BL_SCOPE}" STREQUAL "GLOBAL")
    nuttx_include_directories_for_all_apps(${paths})
  endif()

  if("${BL_SCOPE}" STREQUAL "CHIP_BOARD" OR "${BL_SCOPE}" STREQUAL "GLOBAL")
    if(TARGET arch)
      target_include_directories(arch PUBLIC ${paths})
    endif()
    if(TARGET board)
      target_include_directories(board PUBLIC ${paths})
    endif()
  endif()
endfunction()

function(bl_add_driver_subdirectories drivers_dir)
  if(NOT IS_DIRECTORY "${drivers_dir}")
    return()
  endif()

  foreach(driver_dir lhal)
    if(EXISTS "${drivers_dir}/${driver_dir}/CMakeLists.txt")
      add_subdirectory("${drivers_dir}/${driver_dir}" "drivers/${driver_dir}")
    endif()
  endforeach()
endfunction()

function(bl_strip_compile_definitions out_var)
  set(defs)
  foreach(def IN LISTS ARGN)
    string(REGEX REPLACE "^-D" "" clean_def "${def}")
    list(APPEND defs "${clean_def}")
  endforeach()
  set(${out_var}
      ${defs}
      PARENT_SCOPE)
endfunction()

function(bl_sdk_apply_target_defaults target)
  if(NOT DEFINED BL_BOUFFALO_DRIVERS_DIR OR "${CHIP}" STREQUAL "")
    return()
  endif()

  target_compile_definitions(
    ${target} PUBLIC APP_VER_X=0 APP_VER_Y=0 APP_VER_Z=0 ARCH_RISCV=1
                     CONFIG_IRQ_NUM=83)

  target_include_directories(
    ${target}
    PUBLIC
      ${BL_BOUFFALO_DRIVERS_DIR}/lhal/include
      ${BL_BOUFFALO_DRIVERS_DIR}/lhal/include/arch
      ${BL_BOUFFALO_DRIVERS_DIR}/lhal/include/arch/risc-v/t-head
      ${BL_BOUFFALO_DRIVERS_DIR}/lhal/include/arch/risc-v/t-head/Core/Include
      ${BL_BOUFFALO_DRIVERS_DIR}/lhal/config/${CHIP}
      ${BL_BOUFFALO_DRIVERS_DIR}/lhal/src/flash
      ${BL_BOUFFALO_DRIVERS_DIR}/soc/${CHIP}/std/include
      ${BL_BOUFFALO_DRIVERS_DIR}/soc/${CHIP}/std/include/hardware
      ${BL_BOUFFALO_DRIVERS_DIR}/sys
      ${BL_BOUFFALO_DRIVERS_DIR}/sys/${CHIP})
endfunction()

macro(sdk_generate_library)
  if(${ARGC})
    set(library_name ${ARGV0})
  else()
    get_filename_component(library_name ${CMAKE_CURRENT_LIST_DIR} NAME)
  endif()

  set(CURRENT_STATIC_LIBRARY "bl_${library_name}")
  add_library(${CURRENT_STATIC_LIBRARY} STATIC)
  set_target_properties(${CURRENT_STATIC_LIBRARY} PROPERTIES OUTPUT_NAME
                                                             "${library_name}")
  set_property(GLOBAL APPEND PROPERTY NUTTX_EXTRA_LIBRARIES
                                      ${CURRENT_STATIC_LIBRARY})
  bl_sdk_apply_target_defaults(${CURRENT_STATIC_LIBRARY})
endmacro()

function(sdk_library_add_sources)
  set(sources)
  foreach(source IN LISTS ARGN)
    if(IS_DIRECTORY "${source}")
      message(FATAL_ERROR "sdk_library_add_sources() was called on a directory")
    endif()

    if(IS_ABSOLUTE "${source}")
      list(APPEND sources "${source}")
    else()
      list(APPEND sources "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    endif()
  endforeach()

  target_sources(${CURRENT_STATIC_LIBRARY} PRIVATE ${sources})
endfunction()

function(sdk_library_add_sources_ifdef feature)
  if(${feature})
    sdk_library_add_sources(${ARGN})
  endif()
endfunction()

function(sdk_add_include_directories)
  bl_normalize_paths(paths ${ARGN})
  target_include_directories(${CURRENT_STATIC_LIBRARY} PUBLIC ${paths})
endfunction()

function(sdk_add_private_include_directories)
  bl_normalize_paths(paths ${ARGN})
  target_include_directories(${CURRENT_STATIC_LIBRARY} PRIVATE ${paths})
endfunction()

function(sdk_add_include_directories_ifdef feature)
  if(${feature})
    sdk_add_include_directories(${ARGN})
  endif()
endfunction()

function(sdk_add_compile_definitions)
  bl_strip_compile_definitions(defs ${ARGN})
  target_compile_definitions(${CURRENT_STATIC_LIBRARY} PUBLIC ${defs})
endfunction()

function(sdk_add_compile_definitions_ifdef feature)
  if(${feature})
    sdk_add_compile_definitions(${ARGN})
  endif()
endfunction()

function(sdk_add_static_library)
  foreach(library IN LISTS ARGN)
    if(IS_ABSOLUTE "${library}")
      set(path "${library}")
    else()
      set(path "${CMAKE_CURRENT_SOURCE_DIR}/${library}")
    endif()

    if(EXISTS "${path}")
      target_link_libraries(${CURRENT_STATIC_LIBRARY} PRIVATE "${path}")
    endif()
  endforeach()
endfunction()

function(sdk_add_static_library_ifdef feature)
  if(${feature})
    sdk_add_static_library(${ARGN})
  endif()
endfunction()

function(sdk_add_subdirectory_ifdef feature dir)
  if(${feature})
    add_subdirectory("${dir}")
  endif()
endfunction()

function(sdk_add_subdirectory_ifexists dir)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
    add_subdirectory("${dir}")
  endif()
endfunction()
