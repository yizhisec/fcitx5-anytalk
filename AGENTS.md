# Repository Guidelines

## Project Structure & Module Organization
- `src/`: C++ Fcitx5 addon sources (`addon.*`, `constants.h`).
- `anytalk-lib/`: Zig 共享库，提供音频采集、ASR WebSocket 通信等核心功能，通过 C API (`include/anytalk_api.h`) 暴露给 C++ 层。
- `data/`: Fcitx5 config and icon assets (`anytalk.conf`, `anytalk-addon.conf`, `anytalk*.png`).
- `CMakeLists.txt`: Top-level build that builds both the C++ addon and the Zig shared library.
- `build/`: Local build output directory (generated).

## Build, Test, and Development Commands
- `cmake -S . -B build`: Configure the CMake project.
- `cmake --build build`: Build the C++ addon and the Zig shared library (`libanytalk.so`).
- `cmake --install build`: Install addon, configs, icons, and shared library (uses CMake install rules).
- `cd anytalk-lib && zig build`: Build the Zig library only.
- `cd anytalk-lib && zig build test`: Run Zig tests.

## Coding Style & Naming Conventions
- C++ code is C++20; follow existing style (4-space indentation, braces on the same line).
- Zig code follows standard Zig conventions; use `zig fmt` if you touch Zig code.
- Keep file names lowercase with hyphens for assets (e.g., `anytalk-rec.png`).

## Testing Guidelines
- There are no automated tests in this repository yet. If you add tests, include a short note in your PR and document how to run them (for Zig, `zig build test` in `anytalk-lib/`).

## Commit & Pull Request Guidelines
- Commit messages follow a `Verb: Description` pattern (e.g., `Optimize: Robust UI state reset on disconnection`). Use the same style.
- PRs should include: a clear summary, the affected areas (`src/`, `anytalk-lib/`, `data/`), and any manual test steps (e.g., “rebuilt via CMake; verified Fcitx5 status icon updates”).

## Configuration & Runtime Notes
- The CMake build installs Fcitx5 addon configs and icons from `data/`; keep these in sync with code changes.
- The Zig shared library (`libanytalk.so`) is installed from `anytalk-lib/zig-out/lib/` via the CMake install step.
