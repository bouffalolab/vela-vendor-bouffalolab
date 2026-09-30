# SPDX-License-Identifier: Apache-2.0

# mible_test.c is part of zblue but left out of its default build; it
# compiles here with the include paths and options of the zblue library.
# mible_main.c must stay first: only the first source gets main renamed.
if(CONFIG_BL_WIRELESS_TESTS_MIBLE)
  nuttx_add_application(
    NAME
    ${CONFIG_BL_WIRELESS_TESTS_MIBLE_PROGNAME}
    PRIORITY
    ${CONFIG_BL_WIRELESS_TESTS_MIBLE_PRIORITY}
    STACKSIZE
    ${CONFIG_BL_WIRELESS_TESTS_MIBLE_STACKSIZE}
    MODULE
    ${CONFIG_BL_WIRELESS_TESTS_MIBLE}
    SRCS
    mible_main.c
    ${NUTTX_APPS_DIR}/external/zblue/zblue/subsys/bluetooth/host/shell/mible_test.c
  )

  # mible_test.c still includes the zblue 3.x header paths; zblue 4.x keeps
  # the API but moved the headers under zephyr/. Forward the old paths.
  set(MIBLE_COMPAT_DIR ${CMAKE_CURRENT_BINARY_DIR}/compat)
  set(MIBLE_COMPAT_HEADERS
      "zephyr.h=zephyr/kernel.h"
      "sys/byteorder.h=zephyr/sys/byteorder.h"
      "settings/settings.h=zephyr/settings/settings.h"
      "shell/shell.h=zephyr/shell/shell.h"
      "bluetooth/bluetooth.h=zephyr/bluetooth/bluetooth.h"
      "bluetooth/conn.h=zephyr/bluetooth/conn.h"
      "bluetooth/uuid.h=zephyr/bluetooth/uuid.h"
      "bluetooth/gatt.h=zephyr/bluetooth/gatt.h"
      "bluetooth/hci.h=zephyr/bluetooth/hci.h"
      "bluetooth/l2cap.h=zephyr/bluetooth/l2cap.h"
      "bluetooth/rfcomm.h=zephyr/bluetooth/classic/rfcomm.h"
      "bluetooth/sdp.h=zephyr/bluetooth/classic/sdp.h")
  foreach(pair ${MIBLE_COMPAT_HEADERS})
    string(REPLACE "=" ";" pair ${pair})
    list(GET pair 0 old)
    list(GET pair 1 new)
    file(
      CONFIGURE
      OUTPUT
      ${MIBLE_COMPAT_DIR}/${old}
      CONTENT
      "#include <${new}>\n")
  endforeach()

  set(MIBLE_TARGET apps_${CONFIG_BL_WIRELESS_TESTS_MIBLE_PROGNAME})
  target_include_directories(
    ${MIBLE_TARGET} PRIVATE ${MIBLE_COMPAT_DIR}
                            $<TARGET_PROPERTY:zblue,INCLUDE_DIRECTORIES>)
  target_compile_options(${MIBLE_TARGET}
                         PRIVATE $<TARGET_PROPERTY:zblue,COMPILE_OPTIONS>)
  target_link_libraries(${MIBLE_TARGET} PRIVATE zblue_headers)
endif()
