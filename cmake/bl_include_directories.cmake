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
      target_include_directories(arch PRIVATE ${paths})
    endif()
    if(TARGET board)
      target_include_directories(board PRIVATE ${paths})
    endif()
  endif()
endfunction()
