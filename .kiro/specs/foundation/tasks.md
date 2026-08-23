# Foundation spec — tasks

## Phase 0 — explain-back (DONE 2026-08-22/23)

- [x] Read cskk cskk.{h,cpp}; chewing eim.{h,cpp} + testchewing.cpp + test
      wiring + check.yml; quwei tutorial; fcitx-libpinyin eim.cpp.
- [x] Report (a) engine-class shape (b) pinyin_* sequence (c) harness
      mechanics (d) CI data plan. GO received.
- [x] Correction folded in: §3 constraint machinery IS on engine main
      (78b22ee; #145/#146 merged, f801cda) — Phase 4 unblocked; CI pins a
      fixed main SHA, not feature SHAs.

## Phase 1 — governance + skeleton (IN PROGRESS)

- [x] LICENSE (GPL-3.0-or-later), README (pre-alpha + scope note + safety),
      AppStream metainfo, .clang-format (fcitx5's).
- [x] .kiro steering (product/tech/structure) + this spec tree.
- [x] AGENTS.md + CLAUDE.md (safety rule verbatim, identity rule, gates,
      engine contract, pin policy).
- [x] src/oxpinyin.{h,cpp}: V3 engine, pinyin_init/fini, per-context
      instances, logging-only keyEvent; conf templates; add_fcitx5_addon;
      PREFIX ""; install .so + confs.
- [x] po/ skeleton.
- [x] test harness (testdir.h.in, copy-addon/copy-im, testoxpinyin.cpp):
      addon loads; group switch; passthrough asserts.
- [x] CI workflow: clang-format gate; arch {gcc,clang} + fedora + ASan/UBSan;
      pinned OXPINYIN_SHA; data job (oracle export + fetch-model, cached).
- [x] Gates: gcc+clang configure/build, clang-format, ctest (podman
      archlinux:latest, mirrors CI), ASan/UBSan ctest; commit; report.
- [x] Post-review: commit-msg hook + trailer lint ported (canonical
      `Assisted-by: Z.ai:GLM-5.3` house form; `.githooks/commit-msg` delegates
      to `.github/scripts/lint-commits.sh`; CI trailer-lint + trailer-test);
      history rewritten to canonical trailers from the root.
- [x] Post-review 2: the engine pin's full SHA had been fabricated from the
      short prefix — corrected to the true tip `78b22eefa28…` (engine main was
      never rewritten; reflog-confirmed); pin-capture + remote-verification
      rules encoded in AGENTS.md; the false narrative retracted from history.

## Phase 2 — engine loop (pending GO)

Buffer/parse/guess/select/commit/reset wiring per design §2; harness tests:
nihao preedit + candidates + commit expectation; paging; backspace; escape;
Ctrl-combo passthrough.

## Phase 3 — preedit/aux + config (pending GO)

Aux-text preedit, client-vs-panel, FCITX_CONFIGURATION (page size, scheme,
fuzzy) → set_options/scheme setters; harness tests for scheme switch +
zhuyin case.

## Phase 4 — partial-choice / constrained re-decode (pending GO)

choose_candidate pins; clear_constraint on backspace-into-pin; re-decode;
whole-buffer choose commits; predicted path optional. Engine-side
correctness only.
