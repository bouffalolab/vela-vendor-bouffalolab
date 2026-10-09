# SPDX-License-Identifier: Apache-2.0

if(NOT CONFIG_BL_COMPONENT_MACSW)
  return()
endif()

if(NOT CONFIG_ARCH_CHIP_BL616CL OR NOT CONFIG_MACSW_SELECT STREQUAL "vela_bl616cl")
  message(FATAL_ERROR "The Vela MACSW port supports BL616CL/vela_bl616cl")
endif()

set(CHIP bl616cl)

if(BL_WIFI_PREBUILT)
  foreach(target macsw_${CHIP} macsw_config_${CHIP}_${CONFIG_MACSW_SELECT})
    nuttx_add_extra_library(${CMAKE_CURRENT_LIST_DIR}/prebuilt/lib/lib${target}.a)
  endforeach()
  return()
endif()

string(REGEX REPLACE "gcc$" "" CROSS_COMPILE "${CMAKE_C_COMPILER}")
set(PHYRF_INCLUDE
    ${BL_BOUFFALO_DRIVERS_DIR}/soc/bl616cl/phyrf/include)

# MACSW owns its source list. Its standalone project does not use sdk_* APIs.
add_subdirectory(macsw)

foreach(target macsw_bl616cl macsw_config_bl616cl_${CONFIG_MACSW_SELECT})
  # The Vela compiler implements the standard ISA but not the vendor tune name.
  get_target_property(options ${target} COMPILE_OPTIONS)
  list(REMOVE_ITEM options -mtune=e907 -march=rv32imafc_xtheade)
  set_property(TARGET ${target} PROPERTY COMPILE_OPTIONS ${options})
  target_compile_options(${target} PRIVATE -march=rv32imafc_zicsr_zifencei)
  # The NuttX kernel build injects -Wshadow/-Wundef which the macsw source
  # does not support under its own -Werror; silence them locally.
  target_compile_options(${target} PRIVATE -Wno-shadow -Wno-undef)
  # Keep the build path out of __FILE__ strings and debug info. -g2 overrides
  # the kernel -g3, whose macro info makes the archive about 170 MB.
  target_compile_options(${target} PRIVATE -ffile-prefix-map=${BL_SDK_ROOT}=.
                                           -g2)
  nuttx_add_external_library(${target} MODE KERNEL)
endforeach()
