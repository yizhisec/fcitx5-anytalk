# Repository Guidelines

## Project Structure & Module Organization
- `src/`: fcitx5 addon (Module type), C++20. Thin layer — only IM integration: F2/Esc hotkey, preedit/commit, D-Bus method calls into the overlay, D-Bus signal subscriptions back from it. Files: `addon.{h,cpp}`, `constants.h`.
- `anytalk-overlay/`: Standalone Qt6 process where audio capture, ASR transport, and UI live. Subdirectories under `src/`:
  - `audio/` — `AudioCapture` (libpulse-simple in a QThread).
  - `asr/` — `AsrBackend` interface, `AsrBackendFactory`, `VolcengineBackend` (QWebSocket) and its protocol codec.
  - top-level — `AsrController`, `OverlayWindow` (Aurora dock UI), `OverlayService` (D-Bus methods/signals), `SettingsDialog`, `Config`, `OverlayState`.
- `data/`: fcitx5 addon conf, icons (4 states × multi-size PNG/SVG), D-Bus service file (`org.fcitx.Fcitx5.AnyTalk.Overlay.service`), waybar CSS sample.
- `CMakeLists.txt`: top-level. Builds the addon, includes `anytalk-overlay/` as a sub-CMake project (gated by `-DBUILD_OVERLAY=ON`, default on).
- `build/`: local build output (generated).

## Build, Test, and Development Commands
- `cmake -S . -B build`: configure.
- `cmake --build build`: build addon + overlay.
- `sudo cmake --install build`: install both.
- `pkill -x anytalk-overlay`: drop the running overlay so the next F2 picks up the new binary (D-Bus activation re-spawns it).
- `fcitx5 -r`: reload the addon side after `anytalk.so` changes.
- `anytalk-overlay --settings`: open the settings dialog from the command line.

`-DBUILD_OVERLAY=OFF` skips the Qt6 overlay (only installs the addon).

## Coding Style & Naming Conventions
- C++20 throughout; 4-space indent, braces on the same line.
- Qt classes use camelCase methods, PascalCase types. State machine strings live in `OverlayState.h` (`state::Idle`, etc.) — do not reinvent literals in new files.
- Asset names: lowercase with hyphens (`anytalk-recording-48.png`).

## Testing Guidelines
- No automated tests. Manual verification path:
  1. `sudo cmake --install build` → `pkill -x anytalk-overlay` → `fcitx5 -r`.
  2. `busctl --user monitor org.fcitx.Fcitx5.AnyTalk.Overlay` to watch the signal stream.
  3. Press F2, speak, press F2 / Enter; verify `CommitText` arrives and the focus window receives the full transcript.

## Commit & Pull Request Guidelines
- Commit messages: `Verb: Description` (e.g., `Optimize: Robust UI state reset on disconnection`) or conventional-commit style (`feat: …`, `refactor: …`, `fix: …`) — both used in history.
- PRs should mention: affected areas (`src/`, `anytalk-overlay/`, `data/`), manual test steps, and any change to the D-Bus surface.

## Configuration & Runtime Notes
- User config lives at `~/.config/fcitx5/conf/anytalk.conf` (INI with `[Asr]` and per-backend sections like `[Volcengine]`). The addon does not read or write this file any more — the overlay is the sole owner.
- `anytalk-addon.conf` ships with `Configurable=False`; fcitx5-config-qt does not expose a settings page.
- D-Bus session-bus activation auto-launches the overlay on first method call. To make activation work on Sway / wlroots, the addon pushes WAYLAND_DISPLAY etc. into the bus daemon at load time via `UpdateActivationEnvironment`.
- The legacy `org.fcitx.Fcitx5.AnyTalk` `StateChanged` D-Bus signal is preserved for waybar custom-module integrations.
