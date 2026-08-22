# fcitx5-oxpinyin

A [fcitx5](https://fcitx-im.org) input-method addon for Simplified Chinese
pinyin (and, later, double pinyin and zhuyin), powered by the
[oxpinyin](https://github.com/shenghaoc/pinyin-rs) engine through its C ABI
(`libpinyin_capi` / `pinyin.h`).

This repository is the **thin C++20 shell only**: key-event handling, candidate
list, preedit, configuration, and the fcitx5 addon plumbing. All decoding,
candidate ranking, and user-model persistence live in the Rust engine. The
architecture follows the
[fcitx5-cskk](https://github.com/fcitx/fcitx5-cskk) pattern — a C++ fcitx5
addon over a Rust engine's C API.

**Status: pre-alpha.** The skeleton loads, engine wiring is under
construction.

## Scope

In scope, verified by the headless `TestFrontend` harness in `test/`:

- engine call sequencing (parse → guess → candidates → choose → commit),
- preedit and auxiliary-text presentation,
- configuration surface.

Explicitly **out of scope**: full-desktop and KDE/Plasma integration testing,
distribution packaging polish, and any visual behaviour beyond what the
harness and a nested compositor can exercise.

## Safety rule for development machines

This addon is **never** registered as the live session input method on a
development machine. All testing goes through the TestFrontend harness. The
rare visual check uses a nested compositor (weston/cage) or a VM. If an
experiment ever leaves the session without a working keyboard, switch to a TTY
(Ctrl+Alt+F3) and run `pkill fcitx5` to restore the previous input stack.

## Building

Requirements: CMake ≥ 3.21, a C++20 compiler, fcitx5 development files
(≥ 5.1.13), `extra-cmake-modules`, and oxpinyin installed where
`pkg-config --cflags --libs oxpinyin` finds it:

```sh
git clone https://github.com/shenghaoc/pinyin-rs ../pinyin-rs
cd ../pinyin-rs && git checkout <pinned-sha>   # see .github/workflows/ci.yml
cargo cinstall -p oxpinyin-capi --prefix=$HOME/.local/oxpinyin --libdir=lib
cd ../fcitx5-oxpinyin
export PKG_CONFIG_PATH=$HOME/.local/oxpinyin/lib/pkgconfig:$PKG_CONFIG_PATH
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

The engine's model data is not shipped by `cargo cinstall`. The test suite
needs a data directory containing the exported `.redb` tables and
`interpolation2.text`; point the engine at it with:

```sh
export OXPINYIN_SYSTEM_DATA_DIR=/path/to/oxpinyin-data
export OXPINYIN_USER_DATA_DIR=$(mktemp -d)
```

Without the environment overrides the shell falls back to the compiled-in
`${CMAKE_INSTALL_FULL_DATADIR}/oxpinyin` and then to fcitx's
`StandardPaths` (`PkgData` + `oxpinyin`).

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
