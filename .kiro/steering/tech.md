# Technology steering — fcitx5-oxpinyin

Settled decisions; do not relitigate without an explicit design-change STOP.

## Language and build

- C++20 (fcitx5's floor is C++17; C++20 is complete on every target incl.
  Ubuntu 24.04 GCC 13; C++23 is not required). CMake ≥ 3.21.
- `find_package(Fcitx5Core Fcitx5Utils)` (floor Fcitx5 ≥ 5.1.13, as chewing);
  include `Fcitx5CompilerSettings`, then set C++20.
- Engine linkage: `pkg_check_modules(LIBPINYIN REQUIRED IMPORTED_TARGET libpinyin)`.
  Both implementations supply `libpinyin.so.15` and `pinyin.h`; engine
  substitution is runtime/install-time, not a frontend build option.
- Addon library: `add_fcitx5_addon` + `PREFIX ""` → `oxpinyin.so` in
  `${CMAKE_INSTALL_LIBDIR}/fcitx5`.
- Formatting: fcitx5 `.clang-format` (copied from fcitx5-chewing).

## Engine class shape

- `OxpinyinEngine final : public fcitx::InputMethodEngineV3` (V3 = V2 +
  `invokeActionImpl`; do not use V4). Registered with
  `FCITX_ADDON_FACTORY_V2`.
- Engine ctor does `pinyin_init` once; the factory yields `nullptr` on init
  failure; dtor `pinyin_fini`.
- Per-context state: `FactoryFor<OxpinyinState>` (an `InputContextProperty`),
  each owning
  `std::unique_ptr<pinyin_instance_t, decltype(&pinyin_free_instance)>`
  allocated via `pinyin_alloc_instance`.

## Engine API surface (allowed/forbidden)

See AGENTS.md "Engine API contract" for the full list. Highlights:
aux-text getters replace the per-key preedit walk; `oxpinyin_init_for_fixtures`
is forbidden; constraint behaviour is engine-side only
(`choose_candidate` / `clear_constraint` + re-run `guess_*`).

## Data resolution

`pinyin_init(systemdir, userdir)` fails closed on missing tables/model.
Resolution order: env `OXPINYIN_SYSTEM_DATA_DIR` / `OXPINYIN_USER_DATA_DIR`
→ compiled-in `<libpinyin pkgdatadir>/data` → fcitx StandardPaths
(`PkgData` + `oxpinyin`). Supported engine export/install tooling provides
complete matching model tables; the library installer alone does not.
See PACKAGING.md for the consumer data contract.

## Engine pins and CI

Ordinary CI builds against distro libpinyin. The separate same-binary gate
uses `tools/engine-substitution/pins.env` to pin oxpinyin; that source revision's
`tools/oracle/oracle-pin.txt` owns the authoritative libpinyin/model20 pins.
The bootstrap explicitly selects Tkrzw for oracle, engine and data export.
Run both libraries under one built addon, verify loader paths and hashes,
and retain unnormalized candidate differences. Never float a pin or equate
SONAME equality with binary identity.

## Templates

- fcitx5-cskk: shell-over-Rust-C-API structure, per-context FactoryFor seam.
- fcitx5-chewing: thin `eim.{h,cpp}` shape, `add_fcitx5_addon`, TestFrontend
  harness, CI shape.
- fcitx5 wiki quwei tutorial: skeleton progression and key-handling rules.
- fcitx-libpinyin: the `pinyin_*` call sequence only (its fcitx4 framework
  code is dead).

## Unit-test framework policy

The suite uses Fcitx TestFrontend and FCITX_ASSERT, with no external framework. If pure-logic helpers accrete, adopt Catch2 v3 via
FetchContent (designated choice; GoogleTest acceptable if mocking ever
matters). Do not add it speculatively.
