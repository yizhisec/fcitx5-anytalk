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
- **Force-copy after build** if `cmake --install` reports "Up-to-date" but binary is stale: `sudo cp -f build/anytalk-overlay/anytalk-overlay /usr/bin/anytalk-overlay`.
- Uninstall before reinstall (avoid stale files from renamed/moved targets): `sudo xargs rm -f < build/install_manifest.txt`.
- `pkill -x anytalk-overlay`: drop the running overlay; next `fcitx5 -r` will respawn it via the addon's `wakeOverlay()` (NOT auto-activated by F2 anymore).
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
- F2/Esc/Enter are watched via `InputContextKeyEvent` — they only fire when a focused app exposes an InputContext. Empty workspaces / desktop-with-no-window will not trigger the addon. For full coverage, bind the keys at compositor level to `busctl --user call ... ToggleRecording` (see README).
- Overlay lifecycle is owned by the addon. `wakeOverlay()` in the addon constructor sends a `Ping` to trigger D-Bus auto-activation at fcitx5 load / `fcitx5 -r`. F2/Esc go through `overlayCall()` which gates on `bus->serviceOwner(...).empty()` — so `pkill -x anytalk-overlay` is *final* until the next `fcitx5 -r`. This is intentional: re-spawning into a half-released kernel BT SCO state was the freeze.

## Wayland & Multi-monitor Notes
- LayerShellQt: not calling `setScreen()` does **not** mean "compositor decides". It falls back to `QWindow::screen()` (i.e. primary). To follow the active output use `ls->setWantsToBeOnActiveScreen(true)` (LayerShellQt ≥ 6.6).
- `setScreen()` only takes effect on first surface creation; to switch output across F2 presses, do `windowHandle()->destroy()` before the next `show()`.
- `KeyboardInteractivity` MUST be `None` for this overlay. `OnDemand` + destroy/recreate has wedged sway seat state in the past.
- fcitx5 `InputContext::cursorRect()` is **surface-local** on Wayland (text-input-v3 spec). Don't use it to find the active screen on Wayland; rely on `setWantsToBeOnActiveScreen` instead. On X11 the rect is screen-absolute so `QCursor::pos()` works for the X11 path.
- SIGTERM handler in `main.cpp` calls `::_Exit(0)`, not `QApplication::quit()` — Qt's destructor chain can deadlock on a stuck `pa_simple_read`, leaving the PA stream open and the mic locked. Kernel-level fd close is the only reliable cleanup.

## Volcengine ASR Protocol Notes
- One ws = one session: server kicks idle ws within seconds; can't reuse across F2 presses.
- Every frame must carry a sequence (`POS_SEQUENCE` / `NEG_WITH_SEQUENCE`); mixing seq/no-seq frames triggers server error `decode V1 protocol message autoAssignedSequence`.
- Audio packets ~100-200ms each (≈6400 bytes @ 16kHz S16LE); larger packets get silently dropped server-side. Slice handshake-buffered audio before flushing.
- Reference: official Python demo `sauc_websocket_demo.py` — canonical answer for protocol questions; mirrors our `VolcengineProtocol.cpp` layout.

## PulseAudio Capture Notes
- PA suspends idle sources after a few seconds; next stream open ships ~1s of *absolute zero* PCM padding (rms == 0, not even noise floor).
- `AudioCapture` keeps the PA stream open and the read thread running for the lifetime of the overlay so the source never suspends; `start()/stop()` only flip an `active_` flag controlling whether reads are emitted.
- PA may recycle long-lived streams behind us; `pa_simple_read` failure puts the thread into a dead state — next `start()` detects and rebuilds.
- The PA client identity is the overlay process, **not** the focused application. Switching applications does not affect our capture path.
- **Bluetooth mic warning**: HFP/SCO + always-on capture pattern hits a known kernel race (CVE-2025-40309 / CVE-2026-31408 family) — closing a long-running SCO stream can wedge the system requiring hard reboot. Symptom: `Bluetooth: hci0: corrupted SCO packet` in dmesg right before freeze. For dev work use built-in/wired mic; the BT mic path is unsafe in current kernels.

## Debug Recipes
- Coredump backtrace: `coredumpctl info fcitx5` for stack; `coredumpctl debug PID --debugger-arguments="-batch -x cmds.txt"` for scripted gdb (registers, disasm).
- Resolve a libFcitx5Core offset: `nm -D /usr/lib/libFcitx5Core.so.7 | sort` + `objdump -d --start-address=X --stop-address=Y -C lib.so` for the crash site.
- Watch overlay D-Bus signals live: `busctl --user monitor org.fcitx.Fcitx5.AnyTalk.Overlay`.
- Stale install residue lives in `/usr/local/share/fcitx5/` from prior CMake default-prefix builds — check there if fcitx5 sees a phantom addon name.
- Hard freezes during `pkill anytalk-overlay`: check `sudo journalctl --boot=-1 --dmesg | grep -i sco` for `corrupted SCO packet` — that's the BT SCO race fingerprint.
