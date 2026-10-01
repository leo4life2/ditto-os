# Ditto Remote for macOS

Native menu-bar helper: hold to dictate, double-tap Return, and guarded light-shake Undo. The bundle keeps the `local.scout.remote` identifier and `ScoutRemote` executable name for compatibility; the displayed name is Ditto Remote.

From the repository root, create `.venv` and install `host/requirements.txt`, then:

```sh
.venv/bin/python tools/build_mac_remote.py --stage-only
# Builds, ad-hoc signs, verifies and self-tests dist/macos/.stage/Ditto Remote.app.
.venv/bin/python tools/build_mac_remote.py
# Installs /Applications/Ditto Remote.app; quit an existing instance first.
open '/Applications/Ditto Remote.app'
```

`DITTO_APP_PATH` overrides the install destination. A prior bundle is retained under `dist/macos/.rollback/`. The build records the invoking Python interpreter for the bundled bridge; keep that environment available. Mutable battery data belongs in `~/Library/Application Support/Ditto Remote/`. No login item is installed.

Enable Accessibility for keyboard control and Bluetooth for wireless controls when macOS asks. Ad-hoc signing does not guarantee permission persistence across rebuilds. The helper starts with keyboard control disabled on a fresh install, then restores saved preferences when authorized. Quit releases held keys. Focus-field inspection is optional for dictation/Enter and required only for guarded Undo.

USB audio uses the Ditto USB microphone selected in Wispr Flow. BLE microphone transport exists, but the complete BlackHole-to-Flow wireless route is not yet validated. See [current status](../../README.md#status) and [architecture](../../WIRELESS-AUDIO-AND-BATTERY.md).

The existing icon and code-native template menu symbol are in `Assets/` and `Menu.swift`. This source build is not a notarized downloadable macOS release.
