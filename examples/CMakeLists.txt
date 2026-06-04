# SPDX-License-Identifier: Apache-2.0
# vendor/bouffalolab/examples/CMakeLists.txt
#
# Bouffalo Lab example applications. Each example lives in its own subdirectory
# and registers an app via nuttx_add_application(), gated by its own Kconfig.
# New examples are discovered automatically. See README.md for the shape.

nuttx_add_subdirectory()
nuttx_generate_kconfig(MENUDESC "Bouffalo Examples")
