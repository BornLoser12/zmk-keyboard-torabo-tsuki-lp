#
# Copyright (c) 2025 The ZMK Contributors
# SPDX-License-Identifier: MIT
#

# Suppress duplicate unit-address warnings for the inherited nRF52840 nodes.
list(APPEND EXTRA_DTC_FLAGS "-Wno-unique_unit_address_if_enabled")
