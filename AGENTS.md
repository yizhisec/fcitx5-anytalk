# Repository Guidelines

## Project Structure & Module Organization
- `src/`: C++ Fcitx5 addon sources (`addon.*`, `ipc_client.*`).
- `anytalk-daemon/`: Rust daemon (Cargo project). Main entry is `anytalk-daemon/src/main.rs`.
- `data/`: Fcitx5 config and icon assets (`anytalk.conf`, `anytalk-addon.conf`, `anytalk*.png`).
- `CMakeLists.txt`: Top-level build that also builds the Rust daemon.
- `build/`: Local build output directory (generated).

## Build, Test, and Development Commands
- `cmake -S . -B build`: Configure the CMake project.
- `cmake --build build`: Build the C++ addon and the Rust daemon.
- `cmake --install build`: Install addon, configs, icons, and daemon (uses CMake install rules).
- `cargo build --release` (from `anytalk-daemon/`): Build daemon only.
- `./anytalk-daemon/target/release/anytalk-daemon`: Run the daemon directly after building.

## Coding Style & Naming Conventions
- C++ code is C++20; follow existing style (4-space indentation, braces on the same line).
- Rust follows `edition = "2021"` conventions; use `cargo fmt` formatting if you touch Rust code.
- Keep file names lowercase with hyphens for assets (e.g., `anytalk-rec.png`).

## Testing Guidelines
- There are no automated tests in this repository yet. If you add tests, include a short note in your PR and document how to run them (for Rust, `cargo test` in `anytalk-daemon/`).

## Commit & Pull Request Guidelines
- Commit messages follow a `Verb: Description` pattern (e.g., `Optimize: Robust UI state reset on disconnection`). Use the same style.
- PRs should include: a clear summary, the affected areas (`src/`, `anytalk-daemon/`, `data/`), and any manual test steps (e.g., “rebuilt via CMake; verified Fcitx5 status icon updates”).

## Configuration & Runtime Notes
- The CMake build installs Fcitx5 addon configs and icons from `data/`; keep these in sync with code changes.
- The daemon binary is installed from `anytalk-daemon/target/release/anytalk-daemon` via the CMake install step.
