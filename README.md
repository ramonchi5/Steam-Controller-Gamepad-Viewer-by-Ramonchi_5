# Steam Controller Gamepad Viewer

Steam Controller overlay for OBS with live trackpad finger tracking, analog triggers, grip sense, and capacitive stick touch. Input is read through SDL3 plus Valve HID reports.

Version 3.0.0 is a native Windows x64 OBS plugin. The v2.0.1 browser-source version remains available for users who prefer a portable exe and local URL.

## Native OBS Plugin v3.0.0

The native plugin:

- adds a **Steam Controller Gamepad Viewer** source type inside OBS;
- starts its bundled input backend only while at least one viewer source is active;
- stops that backend when the last viewer source becomes inactive;
- renders directly into an OBS texture instead of requiring a Browser Source;
- exposes line, body, button, trigger, opacity, color, and shine settings as OBS source properties;
- automatically initializes new scene items with Bilinear scale filtering for smooth small overlays while preserving later user choices.

Build and install notes are in `obs-plugin/OBS-PLUGIN.md`.

## AI Disclosure

This viewer was coded with help from OpenAI Codex. The project is human-directed and reviewed, but AI assistance was part of the implementation. If AI-assisted software is a deal-breaker for you, please use another viewer or OBS input overlay.

## Browser Source v2.0.1

The browser-source URL works only while `SteamControllerGamepadViewer.exe` is running. Its release is portable by default and also includes optional install/uninstall scripts for a Windows Startup shortcut. The browser release does not need internet access; its URL points only to `127.0.0.1` on the same computer.

## Presets

Default v2.0.1 Preset:

```text
http://127.0.0.1:31337/?bodyLines=10___innerBodyLines=10___joystickLines=10___btnLines=10___backBtnLines=10___linesColor=#ffffff___linesOpac=55%___bodyOpac=30%___bodyColor=#000000___btnIdle=#25a7ff___btnIdleOpac=0%___btnPressed=#25a7ff___btnPressedOpac=100%___triggerIdleOpac=0%___triggerIdleLinesOpac=0%___shine=100%
```

Slightly Thicker Outlines Preset:

```text
http://127.0.0.1:31337/?bodyLines=15___innerBodyLines=15___joystickLines=15___btnLines=15___backBtnLines=15___linesColor=#ffffff___linesOpac=55%___bodyOpac=30%___bodyColor=#000000___btnIdle=#25a7ff___btnIdleOpac=0%___btnPressed=#25a7ff___btnPressedOpac=100%___triggerIdleOpac=0%___triggerIdleLinesOpac=0%___shine=100%
```

Thicker Outlines Preset:

```text
http://127.0.0.1:31337/?bodyLines=20___innerBodyLines=20___joystickLines=20___btnLines=20___backBtnLines=20___linesColor=#ffffff___linesOpac=55%___bodyOpac=30%___bodyColor=#000000___btnIdle=#25a7ff___btnIdleOpac=0%___btnPressed=#25a7ff___btnPressedOpac=100%___triggerIdleOpac=0%___triggerIdleLinesOpac=0%___shine=100%
```

Much Thicker Outlines Preset:

```text
http://127.0.0.1:31337/?bodyLines=30___innerBodyLines=30___joystickLines=30___btnLines=30___backBtnLines=30___linesColor=#ffffff___linesOpac=55%___bodyOpac=30%___bodyColor=#000000___btnIdle=#25a7ff___btnIdleOpac=0%___btnPressed=#25a7ff___btnPressedOpac=100%___triggerIdleOpac=0%___triggerIdleLinesOpac=0%___shine=100%
```

## Customize

You can customize the overlay directly from the OBS Browser Source URL. Options can be separated with `___` for readability, as shown above, or with the normal `&` query separator. Both formats work.

Line thickness uses `10` as the default v2 look. Use `15` for 1.5x, `20` for 2x, `30` for 3x, or `0` to hide that line group.

Opacity values accept a percent sign, such as `55%` or `0%`. Colors use normal HTML hex colors, such as `#25a7ff`.

Useful URL options:

- `bodyLines=10` changes the outer controller body and bumper line thickness.
- `innerBodyLines=10` changes the inner body/handle line thickness.
- `joystickLines=10` changes the joystick-area circle line thickness.
- `btnLines=10` changes ABXY, dpad, Steam/View/Menu, touch-click, and trackpad line thickness.
- `backBtnLines=10` changes L4/L5/R4/R5 line thickness.
- `linesColor=#ffffff` changes idle line and icon color.
- `linesOpac=55%` changes idle line and icon opacity. Pressed input outlines stay white and fully visible.
- `bodyColor=#000000` changes the controller body fill color.
- `bodyOpac=30%` changes the controller body fill opacity.
- `btnIdle=#25a7ff` changes the unpressed input, dpad, joystick circle, and trackpad idle color.
- `btnIdleOpac=0%` changes the unpressed input, dpad, joystick circle, and trackpad idle opacity.
- `btnPressed=#25a7ff` changes the pressed/touched input color.
- `btnPressedOpac=100%` changes the pressed/touched input opacity.
- `triggerIdleOpac=0%` changes the unpressed trigger fill opacity.
- `triggerIdleLinesOpac=0%` changes the unpressed trigger line opacity. Pressed trigger lines are always fully visible.
- `shine=100%` changes input glow strength. Use `50%` for half glow or `0%` for no glow.

Extra testing options:

- `title=1` shows the title.
- `debug=1` shows connection/status text.
- `bg=solid` gives the page a dark background for normal browser testing.
- `preview=all` forces overlay layers visible for visual checking.

## Run

### Native OBS plugin v3.0.0

1. Download and extract the complete native OBS plugin zip.
2. Close OBS.
3. Run `install-obs-plugin.bat`.
4. Restart OBS and add **Steam Controller Gamepad Viewer** from the Sources menu.

The plugin remains installed after restarting Windows. It starts and stops its bundled backend automatically with the visibility of the OBS source; there is no separate exe or URL to launch.

### Browser Source v2.0.1

1. Download and extract the release zip.
2. Double-click `SteamControllerGamepadViewer.exe`.
3. Add one of the preset URLs above as an OBS Browser Source.

The release zip contains:

- `SteamControllerGamepadViewer.exe`
- `README.md`
- `LICENSE`
- `THIRD_PARTY_NOTICES.md`
- `Install Start With Windows.cmd` (optional)
- `Uninstall Start With Windows.cmd` (optional)

### From source

Use Command Prompt or double-click:

```text
Run-SteamControllerViewer.cmd
```

That script uses `dotnet run` and avoids PowerShell execution-policy issues. The PowerShell script is kept only as a developer convenience.

## OBS Setup

For v3.0.0, add **Steam Controller Gamepad Viewer** directly from the Sources menu and customize it through source properties. New scene items default to Bilinear scale filtering so downscaled curves remain smooth.

For the v2.0.1 release, add a Browser Source with:

```text
URL: use one of the preset URLs above
Width: 1200
Height: 900
Custom CSS: leave empty
```

The URL is local loopback, not a website. If the URL does not load, start the app first.

## Controller Support

The current target is Valve's Steam Controller. The app does not open Steam's controller tester window; it reads current state and redraws the overlay directly, so button releases are shown as releases too.

Supported inputs:

- ABXY, dpad, bumpers, analog triggers, sticks, L3/R3, capacitive stick touch, Steam/View/Menu.
- Left and right trackpad touch position, click pressure, and live finger position.
- Four rear grip buttons.
- Left and right grip-sense strips.

Capacitive stick touch controls whether the large moving joystick dots are visible. The small center dots stay visible as neutral reference points. If a stick moves more than about 8% from center, the moving dot is shown even if the capacitive touch sensor misses your thumb.

Gyro is not displayed. A 2D gyro display was tested and removed because it was harder to read than the rest of the overlay. A dedicated 3D controller viewer is a better fit if you want to show gyro movement clearly.

SDL3 is loaded from the app folder first, then from the default Steam install folders. You can override the path with `--sdl3 "C:\path\to\SDL3.dll"` or the `SDL3_PATH` environment variable.

If SDL3 cannot open the controller after a firmware update, the app falls back to fresh Valve HID reports when they are available. That keeps the overlay connected for Steam Controller firmware/device-id changes that still expose the same raw HID report shape.

## Building Release Zip

Build the native v3 plugin from a source checkout with:

```text
obs-plugin\Build-Release.cmd
```

This creates:

```text
artifacts\release\Steam.Controller.Gamepad.Viewer-v3.0.0-OBS-Plugin-Windows-x64.zip
```

Build the legacy v2.0.1 browser-source package with:

```text
Build-Release.cmd
```

The release zip is created under `artifacts\release`:

- `v2.0.1.Steam.Controller.Viewer.zip`

Do not upload the normal `publish`, `build_x64`, or intermediate `release` folders as GitHub release assets.

## License And Notices

The browser-source application and bundled backend are MIT licensed. The native OBS plugin and OBS build scaffolding under `obs-plugin/` are GPL-2.0-or-later. Third-party components, referenced projects, and Valve/Steam assets are documented in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Important: the Steam Controller name, Steam name, Valve trademarks, and the controller artwork/assets remain Valve property. This project is unofficial and not endorsed by Valve.
