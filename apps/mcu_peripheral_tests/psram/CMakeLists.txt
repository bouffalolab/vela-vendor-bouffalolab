# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BL_MCU_PERIPHERAL_TESTS_PSRAM)
  nuttx_add_application(
    NAME mcu_psram_test
    STACKSIZE 4096
    PRIORITY 100
    SRCS psram_test_main.c)
endif()
