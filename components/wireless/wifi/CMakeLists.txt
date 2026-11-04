# SPDX-License-Identifier: Apache-2.0
#
# Bouffalo Wi-Fi components (macsw, wl80211, bl_wpa_supplicant).

# The macsw and wl80211 core builds take their ISA and ABI from
# BFLB_RISCV_ARCH_FLAGS, which the Bouffalo SDK toolchain probes. Without it
# the compiler default (ilp32d) applies; the cores must use the BL616CL
# kernel ABI.
set(BFLB_RISCV_ARCH_FLAGS -march=rv32imafc_zicsr_zifencei -mabi=ilp32f)

nuttx_add_subdirectory()

# The macsw and wl80211 core archives carry LTO bytecode, so every ALLSYMS
# link pass reruns LTRANS. Run the LTRANS partitions in parallel; this does
# not change the generated image.
if(CONFIG_BL_COMPONENT_MACSW OR CONFIG_BL_COMPONENT_WL80211)
  target_link_options(nuttx PRIVATE -flto=auto)
endif()
