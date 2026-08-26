# Development guide

This is the developer-facing companion to the README: prerequisites, every
CMake option, the two engine-backend workflows, sanitizer builds, and the
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
- One engine visible to pkg-config:
  - the distribution `libpinyin` (its `.pc` file plus model data — what
    distro packages normally ship), or
  - oxpinyin built from source (see below; requires Rust, Cargo, and
    cargo-c for `cargo cinstall`)
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
| `-DENGINE=`                | `libpinyin` | `libpinyin` or `oxpinyin`: selects the pkg-config engine module, the compiled-in system-data directory, and packaging dependency metadata. The addon source is identical for both. |
| `-DENABLE_TEST=`           | `ON`        | Builds the headless test harness ([TESTING.md](TESTING.md))                                                     |
| `-DENABLE_CLOUDPINYIN=`    | `OFF`       | Compiles the optional Cloud Pinyin integration; adds cloudpinyin as a manifest optional-dependency              |
| `-DENABLE_LUA=`            | `OFF`       | Compiles the optional lua-driven candidates and installs `src/oxpinyin.lua` (date/time demo extension)          |
| `-DENABLE_SANITIZER=`      | `OFF`       | Adds ASan+UBSan instrumentation to the tests                                                                    |
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

The default `ENGINE=libpinyin` needs nothing beyond distro packages. To
develop against the oxpinyin Rust engine instead:

```sh
git clone https://github.com/shenghaoc/oxpinyin ../oxpinyin
cd ../oxpinyin
cargo cinstall -p oxpinyin-capi --prefix=$HOME/.local/oxpinyin --libdir=lib
cd ../fcitx5-oxpinyin
export PKG_CONFIG_PATH=$HOME/.local/oxpinyin/lib/pkgconfig:$PKG_CONFIG_PATH
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENGINE=oxpinyin \
      -DOXPINYIN_SYSTEM_DATA_DIR=/path/to/exported/engine/data \
      -DOXPINYIN_USER_DATA_DIR=$(mktemp -d)
cmake --build build && ctest --test-dir build --output-on-failure
```

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

> This addon is NEVER registered as the live session input method on a
> development machine; all testing goes through the TestFrontend harness;
> the rare visual check uses a nested compositor (weston/cage) or a VM;
> document the TTY `pkill fcitx5` escape.

Concretely:

- Automated coverage comes exclusively from the headless TestFrontend suite
  (`ctest`) — it loads the addon inside a sandboxed in-process fcitx5 and
  never registers anything session-wide.
- For anything visual, prefer a nested compositor session (weston or cage)
  or a disposable VM; an isolated container also keeps your desktop IM
  untouched.
- If an experiment ever does leave the session input-dead: switch to a TTY
  (Ctrl+Alt+F3), log in, run `pkill fcitx5`, return to the graphical
  session.
