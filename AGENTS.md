# AGENTS.md — working rules for fcitx5-oxpinyin

## What this repository is

A fcitx5 input-method addon (C++20 shell) whose engine is
[oxpinyin](https://github.com/shenghaoc/oxpinyin) via its C ABI
(`libpinyin_capi`, header `pinyin.h`). The Rust stays in oxpinyin; this repo is
the thin shell only — key events, candidate list, preedit, configuration,
fcitx5 addon plumbing. Structural template: fcitx5-cskk (C++ shell over a Rust
engine's C API). Do not move engine logic into C++; do not reach for engine
internals.

## Hard safety rule (verbatim, non-negotiable)

> this addon is NEVER registered as the live session input method on a
> development machine; all testing goes through the TestFrontend harness; the
> rare visual check uses a nested compositor (weston/cage) or a VM; document
> the TTY `pkill fcitx5` escape.

Background: a mis-registered input method on a live session can leave the
machine without a usable keyboard (the RHEL ibus lesson). If an experiment ever
does leave the session input-dead, switch to a TTY (Ctrl+Alt+F3) and run
`pkill fcitx5`. Every test in this repo runs headless via
`Fcitx5::Module::TestFrontend`; if a proposed test needs real session IM
registration, that is a STOP condition — redesign the test, not the rule.

## Engine API contract

Call ONLY the high-level exported surface of `pinyin.h`:
`pinyin_init/fini/save`, `pinyin_alloc_instance/free_instance/reset`,
`pinyin_parse_more_full_pinyins/_double_pinyins/_chewings`,
`pinyin_get_parsed_input_length`, `pinyin_guess_sentence/get_sentence`,
`pinyin_guess_candidates`, `pinyin_get_n_candidate/get_candidate/
get_candidate_string`, `pinyin_choose_candidate`,
`pinyin_choose_predicted_candidate` (Phase 4+, predicted path only),
`pinyin_clear_constraint` (Phase 4+), `pinyin_train`,
`pinyin_remember_user_input`,
`pinyin_get_full_pinyin_auxiliary_text` (+ `_double_pinyin_`/`_chewing_`
variants), `pinyin_set_options`,
`pinyin_set_double_pinyin_scheme/set_zhuyin_scheme`.

Do NOT use the per-key accessors (`pinyin_get_pinyin_key*`,
`pinyin_get_pinyin_string(s)`, `pinyin_get_zhuyin_string`,
`pinyin_get_raw_full_pinyin`): the aux-text getters replace the hand-rolled
preedit walk those imply. Do NOT use `oxpinyin_init_for_fixtures` — it is
outside the exported `pinyin.h` surface.

- Constraint/partial-choice behaviour is NEVER reimplemented client-side. The
  shell only calls `pinyin_choose_candidate` (sets the pin),
  `pinyin_clear_constraint` (removes it), and re-runs `guess_*`. Correctness is
  pinned by oxpinyin's constraint differentials, not by this repo.
- A needed symbol missing from oxpinyin's exports → **STOP and report**; it is
  implemented in oxpinyin's capi, never shimmed in C++.

## Data resolution

`pinyin_init(systemdir, userdir)` fails closed: it needs the exported `.redb`
tables (`pinyin_index`, `phrase_index`, `bigram`) and a parsable
`interpolation2.text` in `systemdir`, else returns NULL. `cargo cinstall`
ships none of that data. The shell resolves at runtime: env
`OXPINYIN_SYSTEM_DATA_DIR` / `OXPINYIN_USER_DATA_DIR` first (this seam keeps
the harness and CI off any real session state), then the compiled-in
`${CMAKE_INSTALL_FULL_DATADIR}/oxpinyin`, then fcitx `StandardPaths`
(`PkgData` + `oxpinyin`).

## Phases and STOP gates

Work is phased; every phase ends with a STOP: report gate results, ctest names
+ status, sanitizer status, head SHA, and wait for go/no-go. Current phase
status lives in `.kiro/specs/foundation/`.

## Build, test, gates

Requirements: CMake ≥ 3.21, C++20, fcitx5 ≥ 5.1.13 dev files,
`extra-cmake-modules`, and oxpinyin discoverable via
`pkg-config --cflags --libs oxpinyin` (build it from the pinned oxpinyin
checkout with `cargo cinstall -p oxpinyin-capi --prefix=<p> --libdir=lib`).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
OXPINYIN_SYSTEM_DATA_DIR=<data dir> OXPINYIN_USER_DATA_DIR=$(mktemp -d) \
  ctest --test-dir build --output-on-failure
```

Gates before any STOP:
1. `cmake` configure + build clean on **gcc AND clang**.
2. `clang-format` clean (`clang-format --dry-run -Werror` over `src/` `test/`).
3. `ctest` green.
4. CI: build+ctest matrix {gcc, clang} on archlinux:latest plus one
   fedora:latest job; one job under `-fsanitize=address,undefined` — any
   sanitizer finding is fixed before proceeding (this C++ shell is the one
   memory-unsafe layer in the stack).

### Engine pin policy

The CI sibling oxpinyin checkout is pinned to a **fixed main SHA** (see
`OXPINYIN_SHA` in `.github/workflows/ci.yml`), bumped deliberately the same
way the oracle is pinned at `0c5e80e` — never floating on `main` HEAD. An
addon building against a moving engine main is non-reproducible and breaks
bisection across both repos.

### Pin discipline (the fabricated-SHA lesson, 2026-08-23)

- A full-SHA pin is captured from the source of truth at pin time —
  `git rev-parse origin/main` in the engine checkout, or the GitHub API —
  and pasted verbatim. A remembered short SHA is **never** expanded by
  hand: the missing 33 hex digits would be invented, and CI pays for it
  with "not our ref" days later.
- A diagnosis that posits remote-state change (force-push, rename,
  deletion, garbage collection) is verified against the remote before it
  is acted on — and before it is written into a commit message. "My ref
  doesn't resolve" is, first and always, a candidate bug in the ref
  itself.

## Commit identity

Every commit: author AND committer `Shenghao Chen <shenghaoc@outlook.com>`,
plus an `Assisted-by:` trailer in the house form `Agent:Model` with nothing
after the model — e.g. `Assisted-by: Z.ai:GLM-5.3`. Set up with:

```sh
git config user.name "Shenghao Chen"
git config user.email "shenghaoc@outlook.com"
git config core.hooksPath .githooks   # activate the commit-msg hook
```

The commit-message linter (`.github/scripts/lint-commits.sh`) enforces this
at commit time via `.githooks/commit-msg` (R1–R2) and in CI on every pushed
range and PR commit (R1, R2, R4); the rule logic is self-tested by
`test/lint-commits.test.sh`:

- **R1** — no AI agent identity in `Co-authored-by:` (email match, never name
  match). AI attribution goes in `Assisted-by:` only.
- **R2** — `Assisted-by:` house form: `AGENT:MODEL` shape with nothing after
  the model; the `MODEL` token must contain at least one ASCII letter; no
  placeholder text; no duplicate lines.
- **R4** — no AI agent identity as git author or committer (CI-only; the
  hook runs before the commit exists).

## Explain-back

Before writing code on tagged work (engine loop, preedit, constraints, CI,
packaging), explain the plan back first and get a go. Code without a
green-lit explain-back on such work is a rework risk the schedule does not
have.

## Layout

```
src/            oxpinyin.{h,cpp} — engine + per-context state; conf templates
test/           headless TestFrontend harness (testoxpinyin)
po/             gettext
.kiro/          steering docs + specs (phase status)
.github/        CI
```
