# SPDX-License-Identifier: Apache-2.0
# vendor/bouffalolab/CMakeLists.txt
#
# Bouffalo Lab vendor top-level.
#
# nuttx_add_subdirectory() globs ONLY the immediate "*/CMakeLists.txt" (one
# level, non-recursive) and add_subdirectory() each match. So it pulls in
# drivers/ examples/ components/ (which carry a CMakeLists.txt), while:
#   - tools/        is intentionally left without a CMakeLists.txt -> not built
#   - boards/ chips/ have no top-level CMakeLists.txt here; they are included by
#     the kernel/arch side via CONFIG_ARCH_BOARD_CUSTOM_DIR /
#     CONFIG_ARCH_CHIP_CUSTOM_DIR, NOT by this glob.
#
# Order matters: add subdirectories first (so their Kconfig menus are generated)
# then generate this menu which sources them.

nuttx_add_subdirectory()
nuttx_generate_kconfig(MENUDESC "Bouffalo Lab")
