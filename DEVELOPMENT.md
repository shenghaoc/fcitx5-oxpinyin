# Development guide

This is the developer-facing companion to the README: prerequisites, every
CMake option, the oxpinyin workflow, sanitizer builds, and the
house rules that keep commits consistent. For binding project rules (engine
API contract, safety policy, gates), [AGENTS.md](AGENTS.md) is authoritative;
this file summarizes.

## Prerequisites

A Linux environment with:

- CMake ≥ 3.21, Ninja or Make, pkg-config, gettext, `extra-cmake-modules`
- A C++20 compiler — GCC and Clang are both exercised by CI; neither may
  regress
- fcitx5 ≥ 5.1.13 development files (core, utils, and module CMake configs)
- fcitx5-chinese-addons development files — the punctuation module is a
  **hard** dependency of the addon, at build time (public header +
  `Fcitx5Module` Punctuation component) and at runtime
- `libpinyin` visible to pkg-config — the distribution package (its `.pc`
  file plus model data — what distro packages normally ship), or an
  oxpinyin build exporting the same `libpinyin.pc` (see below; requires
  Rust, Cargo, and cargo-c for `cargo cinstall`)
- Optional additions for feature builds:
  - `ENABLE_CLOUDPINYIN`: chinese-addons' cloudpinyin module (development
    files for the build, the module at test time)
  - `ENABLE_LUA`: fcitx5-lua (`LuaAddonLoader`/imeapi modules), build *and*
    test time

On an Arch-like system this is roughly:

```sh
pacman -S --needed base-devel clang cmake ninja git extra-cmake-modules \
  fmt libuv boost libpinyin fcitx5 fcitx5-chinese-addons
# Only for the oxpinyin-from-source engine (the rust package ships cargo):
# pacman -S --needed rust cargo-c
```

This repository's own continuous integration runs exactly such a container,
so any distro providing equivalent packages works.

## Configure options

| Option                     | Default     | Effect                                                                                                          |
| -------------------------- | ----------- | --------------------------------------------------------------------------------------------------------------- |
| `-DENABLE_TEST=`           | `ON`        | Builds the headless test harness ([TESTING.md](TESTING.md))                                                     |
| `-DENABLE_CLOUDPINYIN=`    | `OFF`       | Compiles the optional Cloud Pinyin integration; adds cloudpinyin as a manifest optional-dependency              |
| `-DENABLE_LUA=`            | `OFF`       | Compiles the optional lua-driven candidates and installs `src/oxpinyin.lua` (date/time demo extension)          |
| `-DENABLE_SANITIZER=`      | `OFF`       | Adds ASan+UBSan instrumentation to the tests                                                                    |
| `-DENABLE_FUZZ=`           | `OFF`       | Builds the libFuzzer harnesses over the shell's pure input-handling seam (requires clang; PR-gate smoke, nightly campaign)                                                     |
| `-DLIBPINYIN_SYSTEM_DATA_DIR=` | *from pkg-config* | The **compiled-in** system-data fallback baked into the addon, normally `<libpinyin pkgdatadir>/data`. Set it only when the resolved `libpinyin.pc` defines no `pkgdatadir` (configure fails with that instruction) |
| `-DOXPINYIN_SYSTEM_DATA_DIR=` | *unset*  | Passed through to the test environment so the addon resolves engine **system** model data there (beats the compiled-in path) |
| `-DOXPINYIN_USER_DATA_DIR=`   | *unset*  | Same, for the writable **user** model directory                                                                 |

## Build and test

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Sanitizer build (ASan + UBSan):

```sh
cmake -B build-asan -DCMAKE_BUILD_TYPE=Release -DENABLE_SANITIZER=ON
cmake --build build-asan && ctest --test-dir build-asan --output-on-failure
```

Leak checking uses `test/lsan-suppressions.txt`, deliberately scoped to one
known upstream libpinyin leak so any other leak still fails the run.

Before considering work finished, the gates are: configure/build clean on
**both** GCC and Clang, `clang-format --dry-run -Werror` over `src/` and
`test/`, ctest green, and CI green. Phased work then ends in a STOP report
and waits for go/no-go ([AGENTS.md](AGENTS.md)).

## Working against oxpinyin from source

The default build needs distro packages. For isolated oxpinyin development,
use the supported engine packaging installer; plain `cargo cinstall` lacks
complete pkg-config metadata and upstream ELF symbol versioning. The
repository's pinned bootstrap also provisions checksum-verified model data:

```sh
bash tools/engine-substitution/bootstrap.sh "$PWD/build-substitution"
export PKG_CONFIG_PATH="$PWD/build-substitution/engine/lib/pkgconfig:$PKG_CONFIG_PATH"
cmake -B build-oxpinyin -DCMAKE_BUILD_TYPE=Release \
  -DOXPINYIN_SYSTEM_DATA_DIR="$PWD/build-substitution/engine/lib/libpinyin/data" \
  -DOXPINYIN_USER_DATA_DIR="$(mktemp -d)"
cmake --build build-oxpinyin
ctest --test-dir build-oxpinyin --output-on-failure
```

For parity, compile once against the **oracle** prefix instead and invoke the
same-binary runner in TESTING.md; separate development builds are not a parity
method. Keep the same pkg-config/data environment on subsequent configure
runs so cached linkage cannot be paired with a newly discovered distro data
path. The engine checkout's local rust-toolchain selects its required version;
do not change the user's global Rust default.

Two rules matter here:

1. **Model data is not installed by the engine's library install.** The
   addon initializes the engine fail-closed; without the exported tables it
   refuses to load (see README, "Engine model data"). Point
   `OXPINYIN_SYSTEM_DATA_DIR` at a directory produced by the engine's data
   export procedure.
2. **Pin discipline.** When a checkout must be pinned, capture the full SHA
   from the source of truth (`git rev-parse origin/main`, or the API) and
   paste it verbatim. Never expand a remembered short SHA by hand; verify
   any remote-state-change diagnosis against the actual remote before
   acting on it or writing it anywhere. The full policy is in
   [AGENTS.md](AGENTS.md).

If a needed engine capability has no exported symbol in `pinyin.h`, stop and
report upstream: it gets implemented in the engine's C ABI, never shimmed in
this shell.

## Commit rules

Every commit must have author **and** committer set to
`Shenghao Chen <shenghaoc@outlook.com>` plus one trailer in the house form:

```text
Assisted-by: Z.ai:GLM-5.3
```

i.e. `AGENT:MODEL`, nothing after the model. A commit-msg hook enforces this
locally; the same checks run in CI. Activate with:

```sh
git config user.name "Shenghao Chen"
git config user.email "shenghaoc@outlook.com"
git config core.hooksPath .githooks
```

AI-agent identity belongs in `Assisted-by:` only — never in
`Co-authored-by:` and never as commit author/committer. The linter lives at
`.github/scripts/lint-commits.sh` (self-tested by `test/lint-commits.test.sh`).

## Safety when testing an input method

Automated tests, CI, agents and ordinary development automation must not
register or manipulate the live desktop input method. The TestFrontend suite
runs headless with isolated user data; routine visual checks should use a
nested compositor (weston/cage), VM or isolated desktop session.

Only an explicitly maintainer-authorized manual acceptance run for a concrete
goal may use the live session, under the binding [AGENTS.md](AGENTS.md)
policy. Back up configuration, make changes reversible, retain a known-good
input method, verify loaded addon/engine identity, and start with incremental
smoke gates. Shell results alone do not establish desktop acceptance. The
human performs graphical interactions when automation cannot safely and
controllably do so.

Document recovery before the run: switch to a TTY (Ctrl+Alt+F3), log in and
run `pkill fcitx5`. The completed authorized KDE pass and its limited scope
are recorded in [TESTING.md](TESTING.md).
