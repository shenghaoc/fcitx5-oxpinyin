# Foundation spec — requirements

## 1. Overview

fcitx5-oxpinyin is a fcitx5 addon (C++20 shell) providing Chinese pinyin
input, delegating all language processing to the oxpinyin engine's C ABI.
This spec covers the foundation: skeleton, harness, governance, CI.

## 2. Requirements

### REQ-1: Engine lifecycle
The addon SHALL create exactly one `pinyin_context_t` via `pinyin_init` at
engine construction and release it with `pinyin_fini` at destruction; addon
loading SHALL fail (factory returns nullptr) if `pinyin_init` fails (it fails
closed on missing data).

### REQ-2: Data resolution
The engine data directories SHALL be resolved as: env
`OXPINYIN_SYSTEM_DATA_DIR`/`OXPINYIN_USER_DATA_DIR` first, then the
compiled-in `${CMAKE_INSTALL_FULL_DATADIR}/oxpinyin`, then fcitx
`StandardPaths` (`PkgData` + `oxpinyin`). Tests and CI SHALL use the env seam
exclusively, never live session state.

### REQ-3: Per-context state
Each fcitx input context SHALL own a `pinyin_instance_t` obtained from
`pinyin_alloc_instance` and released with `pinyin_free_instance`, held as
`std::unique_ptr<pinyin_instance_t, decltype(&pinyin_free_instance)>`.

### REQ-4: API discipline
The shell SHALL call only the high-level exported surface of `pinyin.h`
(listed in AGENTS.md). The per-key accessors and `oxpinyin_init_for_fixtures`
SHALL NOT be used. Constraint behaviour SHALL NOT be reimplemented in C++.

### REQ-5: Headless verification
All functional verification SHALL go through the fcitx5 TestFrontend harness;
the addon SHALL NOT be registered as a live session input method on
development machines.

### REQ-6: Governance
The repository SHALL carry GPL-3.0-or-later, a pre-alpha README with the
scope note, AppStream metainfo, AGENTS.md + CLAUDE.md with the safety rule
and identity rule, and this spec tree.

### REQ-7: Gates
Every phase SHALL pass: gcc AND clang builds, clang-format, ctest; CI SHALL
run the archlinux matrix, one fedora job, and one ASan/UBSan job, with the
engine pinned to a fixed main SHA bumped deliberately.

### REQ-8: Phased delivery with STOP gates
Work SHALL proceed in phases (0 explain-back; 1 skeleton; 2 engine loop;
3 preedit/aux + config; 4 partial-choice/constrained re-decode), each ending
in a STOP with reported gate results, ctest names + status, sanitizer status,
and head SHA.

## 3. Success criteria

Phase 1: skeleton builds and loads in the harness; the group switch to
`keyboard-us` + `oxpinyin` works; keys pass through unfiltered (logging only);
all gates green; CI workflow and CPack DEB/RPM generation wired.
