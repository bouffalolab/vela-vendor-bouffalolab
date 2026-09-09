# SPDX-License-Identifier: Apache-2.0
# vendor/bouffalolab/components/CMakeLists.txt
#
# Bouffalo Lab components / middleware. Each component lives in its own
# subdirectory and defines its own library so it can be shipped as an
# independent .a (source or prebuilt). New components are discovered
# automatically. See README.md, including how to wrap an independent git repo.

nuttx_add_subdirectory()
nuttx_generate_kconfig(MENUDESC "Bouffalo Components")
