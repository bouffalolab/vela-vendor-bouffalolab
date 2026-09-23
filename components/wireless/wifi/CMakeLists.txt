# SPDX-License-Identifier: Apache-2.0
#
# Bouffalo Wi-Fi components (macsw, wl80211, bl_wpa_supplicant).

nuttx_add_subdirectory()
nuttx_generate_kconfig(MENUDESC "Wi-Fi")

# The macsw and wl80211 core archives carry LTO bytecode, so every ALLSYMS
# link pass reruns LTRANS. Run the LTRANS partitions in parallel; this does
# not change the generated image.
if(CONFIG_BL_COMPONENT_MACSW OR CONFIG_BL_COMPONENT_WL80211)
  target_link_options(nuttx PRIVATE -flto=auto)
endif()
