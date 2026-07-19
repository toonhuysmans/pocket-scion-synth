# Factory banks 0–7

These eight JSON files preserve the compiled factory defaults for the original
v2.4 multitimbral bank library: 128 patches with IDs 0–127. They were exported
through the firmware's read-only factory scopes, so saved device overrides are
not included.

Each file uses editor schema version 4 and can be imported with **Load bank
JSON** in the browser editor. Loading a file previews its values; use **Save
bank settings** and **Save patch** where appropriate to make changes persistent
on the device.

The archive is kept separately because firmware v2.5 retains these banks while
adding the authored style banks 8–15, bringing the complete library to 256
patches. Recreate the archive from a connected device with:

```sh
swiftc tools/export_factory_banks.swift -o /tmp/export_factory_banks
/tmp/export_factory_banks presets/factory-v2.4
```
