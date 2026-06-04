# SPDX-License-Identifier: Apache-2.0
# vendor/bouffalolab/drivers/CMakeLists.txt
#
# Bouffalo Lab drivers. Each driver lives in its own subdirectory and defines
# its own library so it produces a SEPARATE .a instead of being merged into
# libarch.a / libboard.a. New driver subdirectories (with a CMakeLists.txt and
# Kconfig) are discovered automatically. See README.md for the per-driver shape.

nuttx_add_subdirectory()
nuttx_generate_kconfig(MENUDESC "Bouffalo Drivers")
