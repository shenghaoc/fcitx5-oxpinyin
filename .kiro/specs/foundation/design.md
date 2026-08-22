# Foundation spec — design

Captures the settled design for the shell. Structural templates: fcitx5-cskk
(shell-over-Rust-C-API), fcitx5-chewing (eim shape, harness, CI), quwei
tutorial (progression), fcitx-libpinyin (pinyin_* call sequence only).

## 1. Component model

```
fcitx5 ── keyEvent ──> OxpinyinEngine (InputMethodEngineV3)
                         │ one pinyin_context_t (pinyin_init/fini)
                         ├─ FactoryFor<OxpinyinState> per InputContext
                         │    └─ unique_ptr<pinyin_instance_t, &pinyin_free_instance>
                         └─ (Phase 3) Configuration -> pinyin_set_options / scheme setters
libpinyin_capi.so (oxpinyin, pinned main SHA) ── all decoding/ranking/state
```

- `OxpinyinEngine` ctor: resolve data dirs (env → compiled-in → StandardPaths),
  create user dir, `pinyin_init`; on NULL, log and mark not-ready; the addon
  factory then returns nullptr (addon fails to load, honestly).
- `keyEvent` delegates to `OxpinyinState` (cskk seam). Phase 1: log only.

## 2. Engine call sequencing (from fcitx-libpinyin, adapted)

- Printable key → append to client-side raw buffer → re-parse whole buffer:
  `pinyin_parse_more_full_pinyins` (or `_double_pinyins`/`_chewings`).
  First-key guard: `pinyin_get_parsed_input_length == 0` on a 1-char buffer →
  reset and pass the key through.
- Candidate build: `pinyin_guess_sentence` → `pinyin_get_sentence(0)`;
  `pinyin_guess_candidates(inst, offset, sort)` → `pinyin_get_n_candidate` →
  `pinyin_get_candidate` + `pinyin_get_candidate_string` → CommonCandidateList.
  Offset stays 0 in v1 (Phases 2–3).
- Selection/commit: `pinyin_choose_candidate`; whole buffer consumed →
  `guess_sentence`/`get_sentence` → `commitString` → `pinyin_train(0)` +
  `pinyin_remember_user_input(phrase, -1)`. `pinyin_save` on
  deactivate/focus-out.
- Enter: commit sentence (plus unparsed remainder) or raw buffer.
- Escape/reset: clear local state + `pinyin_reset`.
- Phase 4 (unblocked: §3 merged on engine main, PRs #145/#146, merge commit
  f801cda): partial choice pins the prefix via `pinyin_choose_candidate`;
  backspace into a pinned run calls `pinyin_clear_constraint(offset)` and
  re-decodes; `pinyin_choose_predicted_candidate` only if prediction is
  surfaced. Never client-side constraint logic.

## 3. Preedit (Phase 3)

Aux/preedit text from `pinyin_get_full_pinyin_auxiliary_text` (+ variants)
and `guess_sentence`+`get_sentence` only. Client preedit when
`capabilityFlags` allow — cursor pinned at 0 with highlight marking the true
position (wiki candidate-window-jitter guidance) — panel preedit fallback.

## 4. Harness

Chewing-verbatim: `test/testdir.h.in` (TESTING_*_DIR), `test/addon/` +
`test/inputmethod/` copy targets putting the built confs on the test lookup
path, `testoxpinyin.cpp` with `setupTestingEnvironment`,
`Instance{--disable=all --enable=testim,testfrontend,oxpinyin}`, group =
`keyboard-us` + `oxpinyin`, `ITestFrontend::sendKeyEvent` (bool = handled →
passthrough asserts), `pushCommitExpectation` for commits. Env-seamed data
dirs; CTest `ENVIRONMENT` passes configure-time
`OXPINYIN_SYSTEM_DATA_DIR`/`OXPINYIN_USER_DATA_DIR` through when set.

## 5. CI

- `clang-format` job (archlinux container, fcitx/github-actions@clang-format).
- Build jobs: matrix {gcc, clang} × archlinux:latest, plus one fedora:latest
  job; each builds a minimal fcitx5 first, then the addon against the pinned
  engine (`OXPINYIN_SHA`, fixed main SHA, deliberate bumps), then ctest with
  env-seamed data.
- Data: dedicated job restores an `actions/cache` keyed by engine SHA; on
  miss runs the pinned oracle pipeline (`tools/oracle/build-oracle.sh` +
  `cargo run -p oxpinyin-migrate --features oracle-ffi -- export`) and
  `tools/model/fetch-model.sh`, assembling `oxpinyin-data` (3 `.redb` +
  `interpolation2.text`), published to build jobs via artifact.
- One ASan/UBSan ctest job; clang-tidy advisory non-blocking.

## 6. Packaging

CPack DEB + RPM generated off the `install()` rules. Archive-grade `debian/`
and `.spec` packaging is a later, separate effort (noted in README).
