# Studio USB transport

`studio-usb-independent.patch` targets the ZMK revision pinned in `config/west.yml`.
The keyboard module applies it during CMake configuration, only when
`CONFIG_TORABO_STUDIO_USB_INDEPENDENT=y`. Reconfiguration is idempotent; an
incompatible ZMK source stops the build rather than silently omitting the fix.

Only Studio's transport selection changes. A configured USB host takes priority
for Studio, even if keyboard/mouse HID output is BLE. USB connect/disconnect
events also refresh Studio when the HID endpoint does not change. Without a USB
host, Studio retains ZMK's existing transport selection. USB Studio and BLE
Studio are not simultaneous sessions; BLE keyboard/mouse input remains usable.

BLE parameters, bonding, persisted output preferences, keymap, AML, protobuf,
and the core device-info handler are unchanged. The diagnostic CDC must not be
used for Studio; it emits plain-text logs and is not included in normal releases.

The patch is deliberately guarded by the local Kconfig option so a shared west
checkout remains usable for peripheral or diagnostic builds after a central build.

Run regression checks against a checkout of the pinned ZMK revision:

```sh
python tests/test_studio_usb.py --zmk /path/to/zmk
```

The checks exercise patch application/reconfiguration and compile the actual
transport selector and event-dispatch branches with host-side stubs. Hardware
USB/BLE integration still requires verification on the keyboard.
