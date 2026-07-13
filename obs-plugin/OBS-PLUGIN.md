# Steam Controller Gamepad Viewer for OBS

Version 3.0.0 is a native Windows x64 OBS source for the Valve Steam Controller. It keeps the proven .NET, SDL3, and Valve HID input backend from the browser-source releases, but renders the controller directly into an OBS texture and exposes customization through normal source properties.

## Install

1. Extract the complete release zip.
2. Close OBS Studio.
3. Run `install-obs-plugin.bat` from the extracted folder.
4. Restart OBS and add **Steam Controller Gamepad Viewer** from the Sources menu.

The complete `steam-controller-gamepad-viewer` folder is required. The DLL cannot work by itself because its locale data and bundled input backend live beside it.

The plugin remains installed after restarting Windows. The bundled backend itself runs only while at least one viewer source is active in OBS and closes when the last viewer source becomes inactive. No separate exe, Browser Source URL, Startup shortcut, or internet connection is required.

For portable OBS installations, copy the complete `steam-controller-gamepad-viewer` folder into that installation's configured plugin directory instead of using the installer.

## Behavior

- The first active viewer source starts the hidden bundled `SteamControllerGamepadViewer.exe`.
- Sources poll its local `http://127.0.0.1:31337/api/state` endpoint without blocking OBS's render thread.
- The last inactive viewer source stops the backend.
- Controller geometry and state are rendered natively with GDI+ into a premultiplied OBS texture.
- New scene items initialize OBS Scale Filtering to **Bilinear**, which keeps the controller smooth at small stream-overlay sizes. Any later scale-filter choice made by the user is preserved.
- Line widths, colors, opacity, trigger appearance, and input shine are available in the source properties.

## Supported Inputs

- ABXY, Steam, View/Menu, bumpers, and directional dpad input.
- Analog joystick position, L3/R3 presses, and capacitive stick touch.
- Analog L2/R2 depth with progressive fill.
- Live left/right trackpad finger position, touch state, click pressure, and click feedback.
- Rear L4/L5/R4/R5 buttons and left/right grip sense.

Gyro is intentionally not displayed. A 2D gyro visualization was tested and removed because it was harder to understand than the rest of the overlay; a dedicated 3D controller viewer is a better fit for gyro movement.

## Build

Use Visual Studio 2022 or newer with the Desktop C++ workload and the .NET 8 SDK. From this directory:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
cmake --install build_x64 --prefix release/RelWithDebInfo --config RelWithDebInfo
```

`Build-Release.cmd` performs the complete build, removes developer debug symbols from the public package, and creates:

```text
artifacts\release\Steam.Controller.Gamepad.Viewer-v3.0.0-OBS-Plugin-Windows-x64.zip
```

## Licensing

The native OBS plugin and OBS build scaffolding are GPL-2.0-or-later; see `LICENSE-OBS-PLUGIN`. The bundled backend and browser-source application remain MIT-licensed under `LICENSE`. Third-party components and Valve assets are documented in `THIRD_PARTY_NOTICES.md`.
