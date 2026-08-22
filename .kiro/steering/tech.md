# Technology steering — fcitx5-oxpinyin

Settled decisions; do not relitigate without an explicit design-change STOP.

## Language and build

- C++20 (fcitx5's floor is C++17; C++20 is complete on every target incl.
  Ubuntu 24.04 GCC 13; C++23 is not required). CMake ≥ 3.21.
- `find_package(Fcitx5Core Fcitx5Utils)` (floor Fcitx5 ≥ 5.1.13, as chewing);
  include `Fcitx5CompilerSettings`, then set C++20.
- Engine linkage: `pkg_check_modules(OXPINYIN REQUIRED oxpinyin)`. The .pc
  module is `oxpinyin`; the link flag resolves to `-lpinyin_capi` (SONAME
  `libpinyin_capi.so.0.1`); header installed as `pinyin.h`.
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
→ compiled-in `${CMAKE_INSTALL_FULL_DATADIR}/oxpinyin` → fcitx
`StandardPaths` (`PkgData` + `oxpinyin`). CI assembles the data dir from
`tools/model/fetch-model.sh` (SHA-pinned model20) + `oxpinyin-migrate export`
(pinned oracle pipeline), cached by engine SHA.

## Engine pin

CI builds the sibling oxpinyin checkout at a **fixed main SHA**
(`OXPINYIN_SHA` in `.github/workflows/ci.yml`; currently `78b22ee`, which
contains the §3 constraint merges #145/#146, merge commit `f801cda`). Bumps
are deliberate, like the oracle pin at `0c5e80e`. Never float on main HEAD.

## Templates

- fcitx5-cskk: shell-over-Rust-C-API structure, per-context FactoryFor seam.
- fcitx5-chewing: thin `eim.{h,cpp}` shape, `add_fcitx5_addon`, TestFrontend
  harness, CI shape.
- fcitx5 wiki quwei tutorial: skeleton progression and key-handling rules.
- fcitx-libpinyin: the `pinyin_*` call sequence only (its fcitx4 framework
  code is dead).

## Unit-test framework policy

None in the scaffold. If pure-logic helpers accrete, adopt Catch2 v3 via
FetchContent (designated choice; GoogleTest acceptable if mocking ever
matters). Do not add it speculatively.
