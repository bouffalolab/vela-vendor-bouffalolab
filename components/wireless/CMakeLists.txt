# SPDX-License-Identifier: Apache-2.0
#
# Bouffalo wireless components. Wi-Fi lives under wifi/, BLE under ble/.
# One-level opt-in discovery, matching the components root.

nuttx_add_subdirectory()
nuttx_generate_kconfig(MENUDESC "Wireless")
