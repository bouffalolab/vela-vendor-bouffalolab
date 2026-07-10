# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

if(NOT DEFINED DESTINATION_DIR OR "${DESTINATION_DIR}" STREQUAL "")
  message(FATAL_ERROR "DESTINATION_DIR is required")
endif()

if(NOT IS_DIRECTORY "${SOURCE_DIR}")
  message(FATAL_ERROR "source directory does not exist: ${SOURCE_DIR}")
endif()

file(MAKE_DIRECTORY "${DESTINATION_DIR}")
file(
  GLOB_RECURSE source_files
  LIST_DIRECTORIES FALSE
  RELATIVE "${SOURCE_DIR}"
  "${SOURCE_DIR}/*")

foreach(relative_path IN LISTS source_files)
  get_filename_component(relative_dir "${relative_path}" DIRECTORY)
  if(NOT "${relative_dir}" STREQUAL "")
    file(MAKE_DIRECTORY "${DESTINATION_DIR}/${relative_dir}")
  endif()

  execute_process(
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different "${SOURCE_DIR}/${relative_path}"
      "${DESTINATION_DIR}/${relative_path}"
    RESULT_VARIABLE copy_result)
  if(NOT copy_result EQUAL 0)
    message(FATAL_ERROR "failed to copy: ${relative_path}")
  endif()
endforeach()

file(
  GLOB_RECURSE destination_files
  LIST_DIRECTORIES FALSE
  RELATIVE "${DESTINATION_DIR}"
  "${DESTINATION_DIR}/*")

foreach(relative_path IN LISTS destination_files)
  if(NOT EXISTS "${SOURCE_DIR}/${relative_path}")
    file(REMOVE "${DESTINATION_DIR}/${relative_path}")
  endif()
endforeach()
