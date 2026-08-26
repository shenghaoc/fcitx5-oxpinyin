# fcitx5-oxpinyin

A [fcitx5](https://fcitx-im.org) input-method addon for Simplified-Chinese
pinyin — full pinyin, double pinyin, and Zhuyin — driven through the
libpinyin-compatible C ABI (`pinyin.h`). It can be built against stock
[libpinyin](https://github.com/libpinyin/libpinyin) or against
[oxpinyin](https://github.com/shenghaoc/oxpinyin), a Rust reimplementation
that exports the same C ABI.

**Status:** active development toward version 0.1.0. The frontend feature set
described below is implemented and covered by a headless regression suite;
desktop-integration validation and packaging are still outstanding, so there
is **no released version yet**. See [RELEASE.md](RELEASE.md) for the current
release status and the release checklist.

## Architecture

The project follows the [fcitx5-cskk](https://github.com/fcitx/fcitx5-cskk)
pattern: a C++20 fcitx5 addon over an input-method engine's C API. This
repository is intentionally the **thin shell**; the intelligence lives
engine-side and reaches the shell only across the exported C ABI.

| Layer               | Lives in                     | Responsible for                                                                                                                                             |
| ------------------- | ---------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Fcitx5 addon        | this repo (`src/`)           | key-event handling, preedit and auxiliary text, candidate presentation and selection, commit, configuration, status-bar actions, optional module integrations |
| Input-method engine | oxpinyin (Rust) or libpinyin | syllable parsing, decoding, candidate generation and ranking, prediction, the user model, persistence                                                        |
| Engine model data   | installed separately         | read-only system tables (`*.redb`, `interpolation2.text`) and a writable per-user directory                                                                  |

The shell calls only the high-level exported surface of `pinyin.h`
(`pinyin_init`, `pinyin_parse_more_*`, `pinyin_guess_sentence` /
`pinyin_guess_candidates`, `pinyin_choose_candidate`,
`pinyin_choose_predicted_candidate`, `pinyin_clear_constraint`,
`pinyin_train`, `pinyin_remember_user_input`, the auxiliary-text getters,
and the option/scheme setters). In particular, partial-selection constraint
machinery is **never** reimplemented in C++: the shell sets and clears
constraints and re-runs generation, exactly as the engine defines them.
A needed engine symbol that is not exported is reported upstream — it is
implemented in the engine's C API, never shimmed here.

## Features

All of the following exist in the implementation and are exercised by the
headless test suite ([TESTING.md](TESTING.md)); "on"/"off" refers to shipped
defaults.

| Feature                                                                                                    | Default                    | Notes                                                                                        |
| ---------------------------------------------------------------------------------------------------------- | -------------------------- | -------------------------------------------------------------------------------------------- |
| Full pinyin                                                                                                | on                         | core input path                                                                              |
| Double pinyin (six schemes: Natural Code/ZRM, Microsoft, Ziguang, Intelligent ABC, Pinyin Jiajia, Xiaohe)   | selected via *Input scheme* | parsed engine-side                                                                          |
| Zhuyin (nine layouts incl. Standard, Hsu, IBM, Gin-Yieh, Eten/Eten26, Dachen CP26)                          | selected via *Input scheme* | parsed engine-side (chewing)                                                                |
| Incomplete pinyin                                                                                          | on                         | type initial consonants only                                                                |
| Fuzzy pinyin (10 pairs) and auto-correction (6 rules)                                                      | off                        | configuration flags passed to the engine                                                    |
| Candidate list: digit selection, paging, Space selects first                                               | on                         | page size 5 (configurable 1–10)                                                             |
| Partial (constrained) selection                                                                            | on                         | a mid-sentence choice pins it engine-side; editing unpins; composition continues            |
| Client-side preedit + auxiliary text                                                                       | on                         | falls back to server-side panel display when the input context lacks preedit capability      |
| Next-word prediction                                                                                       | off                        | prediction row after a commit; chains; dismissed by typing or punctuation                   |
| English candidates via fcitx5's Spell module                                                               | on                         | inline suggestion row; silently disabled when the Spell module is unavailable                |
| Chinese punctuation via fcitx5-chinese-addons' punctuation module                                          | on                         | **hard dependency** — the addon does not load without it                                     |
| Simplified↔Traditional toggle                                                                              | passthrough                | performed by chinese-addons' chttrans module; this addon surfaces its status-area toggle     |
| Full-width mode                                                                                            | passthrough                | performed by chinese-addons' fullwidth module; surfaced the same way                        |
| Cloud Pinyin                                                                                               | off                        | build-time option `ENABLE_CLOUDPINYIN`; needs the cloudpinyin module at runtime; hotkey-toggled, suspended in password fields |
| Lua-driven candidates (date/time demo extension)                                                           | off                        | build-time option `ENABLE_LUA`; needs fcitx5-lua at runtime                                 |
| Status-bar toggles                                                                                         | on                         | prediction/spell switches owned here and persisted with the config; module-provided toggles appear next to them |

Known behavioural limitations worth knowing before relying on the addon:

- Delegated punctuation is stateless — pair-alternating smart-quote
  behaviour from some other engines is intentionally not reproduced here.
- Candidate rows contributed by Spell, Cloud Pinyin, or Lua bypass the
  engine's user model on purpose: selecting one commits directly and does
  not train the engine.
- Double-pinyin composition has wiring and scheme-switch coverage but no
  dedicated end-to-end composition test yet.
- There are no translation catalogs yet (`po/LINGUAS` is empty).

## Engine backends

CMake selects the engine with `-DENGINE=` (`libpinyin` — the default — or
`oxpinyin`). The switch changes only which pkg-config module is linked,
where system model data is expected by default, and packaging dependency
metadata; the addon source is identical either way. The point is to exercise
**the same fcitx5 frontend behaviour against either backend**, so frontend
regressions can be told apart from engine differences.

Behavioural parity between the two backends is *not* claimed. Differences do
surface (they have before, e.g. apostrophe handling during parsing); parity
validation is an explicit release-criterion item in [RELEASE.md](RELEASE.md),
owned per-backend by the engine projects.

## Engine model data

The engine library alone does nothing without its language-model data:

- **System data (read-only)** — what it must contain depends on the
  selected backend:
  - **libpinyin:** the files its distribution package installs under its
    data directory — `table.conf`, `pinyin_index.bin`, `phrase_index.bin`,
    `bigram.db`, among others. Distro packages ship these, so nothing
    extra is needed on a normal installation.
  - **oxpinyin:** the exported `.redb` tables (`pinyin_index`,
    `phrase_index`, `bigram`) plus `interpolation2.text`. These are **not**
    installed by building the engine library itself; produce them with the
    engine's data export procedure and make them discoverable via the
    resolution order below.
- **User data (writable):** trained user-model files (kept per backend in
  the user directory).

At startup the shell resolves the two directories in this order:

1. environment override — `OXPINYIN_SYSTEM_DATA_DIR` /
   `OXPINYIN_USER_DATA_DIR`;
2. the compiled-in location (`${CMAKE_INSTALL_FULL_DATADIR}/oxpinyin` for
   the oxpinyin backend; libpinyin's own installed data directory
   otherwise), used only if it exists on disk;
3. fcitx's `StandardPaths` data lookup (`PkgData` + `oxpinyin`).

The **user** directory is created automatically when missing. If the
*system* data cannot be found or is incomplete, initialization fails closed:
the error log names the required files and the addon does not load. So a
successful build that refuses to load almost always means *system model data
is missing* — check the fcitx5 diagnostic log, or set
`OXPINYIN_SYSTEM_DATA_DIR` to a valid data directory to confirm.

Note that installing an engine development package makes the build succeed
but does not necessarily ship model data; data provision belongs to the
backend engine project (for oxpinyin, see its repository).

## Building from source

Requirements: CMake ≥ 3.21, a C++20 compiler (GCC and Clang are both
exercised in CI), Ninja or Make, pkg-config, gettext, `extra-cmake-modules`,
fcitx5 ≥ 5.1.13 development files, fcitx5-chinese-addons development files
(the punctuation module is a hard dependency), and one engine visible to
pkg-config — the distribution `libpinyin`, or oxpinyin built from source
([DEVELOPMENT.md](DEVELOPMENT.md) covers the developer workflow). Optionally,
fcitx5-lua for `ENABLE_LUA`. All configuration options are listed in
[DEVELOPMENT.md](DEVELOPMENT.md).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build    # installs into the usual prefixes
```

## Running the tests

```sh
ctest --test-dir build --output-on-failure
```

All tests run headless through fcitx5's TestFrontend — nothing here touches
your session input method. On a system where the engine's packages normally
provide the model data (e.g. distro libpinyin), the suite resolves everything
automatically. [TESTING.md](TESTING.md) documents the whole architecture:
each runner, sanitizer builds, backend selection, and how to point tests at
explicit model-data directories with `-DOXPINYIN_SYSTEM_DATA_DIR=` /
`-DOXPINYIN_USER_DATA_DIR=`.

## Installation and packaging status

There is currently **no released artifact of any kind** (no tarball release,
no DEB/RPM, no AUR entry). What exists today:

- a working install tree from `cmake --install`: the addon library, the
  input-method and addon config files, and AppStream metainfo;
- CPack scaffolding: `cpack -G "DEB;RPM"` generates raw packages straight
  off the install rules. These are **not** distro-quality packages and have
  not been validated.

Packaging work — proper debian/ and .spec packaging, install-tree and
runtime-dependency validation — is planned as a separate effort; see
[RELEASE.md](RELEASE.md).

Runtime requirements once installed: fcitx5 ≥ 5.1.13,
fcitx5-chinese-addons (the punctuation module), the selected engine library
and its model data, and — depending on build-time options — the cloudpinyin
module or fcitx5-lua.

## Safety notice for developers

This addon is **never** registered as the live session input method on a
development machine; all automated testing goes through the headless
TestFrontend harness. Visual checks belong in a nested compositor (weston or
cage) or a VM. If an experiment ever leaves your session without a usable
keyboard, switch to a TTY (Ctrl+Alt+F3) and run `pkill fcitx5` to restore
the previous input stack. See [DEVELOPMENT.md](DEVELOPMENT.md) for the full
guidance.

## Documentation map

- [DEVELOPMENT.md](DEVELOPMENT.md) — prerequisites, configure options, backend workflow, house commit rules
- [TESTING.md](TESTING.md) — the test architecture and every ctest runner explained
- [RELEASE.md](RELEASE.md) — current release status, known limitations, release checklist
- [CONTRIBUTING.md](CONTRIBUTING.md) — contribution process and commit conventions
- [AGENTS.md](AGENTS.md) — binding working rules for AI coding agents

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
