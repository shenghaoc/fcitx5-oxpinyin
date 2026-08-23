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

## Phase 2 — engine loop (IN PROGRESS — local gates green, CI pending)

- [x] keyEvent wiring: filterAndAccept for handled keys; releases and
      modifier combos pass through; normalized key().
- [x] Printable pinyin -> raw buffer -> parse_more_full_pinyins; first-key
      guard via get_parsed_input_length (un-swallowed passthrough).
- [x] guess_candidates(0) -> get_n_candidate/get_candidate/get_candidate_
      string into CommonCandidateList (page size 5, digit selection keys,
      Page_Up/Page_Down paging).
- [x] Space = candidate 0; Enter commits sentence(0) + unparsed remainder
      (raw buffer fallback); Backspace edits + re-parses; Escape resets
      (pinyin_reset).
- [x] Selection: choose_candidate(0, cand); whole-buffer -> sentence +
      train(0) + remember_user_input; partial -> commit candidate text
      (Phase 4 branch documented in-code). pinyin_save on deactivate.
- [x] Harness: nihao preedit+candidates+commit expectation; backspace;
      escape; Ctrl-combo passthrough mid-composition; digit selection;
      paging. Ownership: candidate strings borrowed-copy, sentence
      malloc'd -> free().
- [ ] Gates: local gcc+clang+format+ctest+ASan green; commit; push; CI
      green; report + STOP.

## Phase 3 — preedit/aux + config (pending GO)

Aux-text preedit, client-vs-panel, FCITX_CONFIGURATION (page size, scheme,
fuzzy) → set_options/scheme setters; harness tests for scheme switch +
zhuyin case.

## Phase 4 — partial-choice / constrained re-decode (pending GO)

choose_candidate pins; clear_constraint on backspace-into-pin; re-decode;
whole-buffer choose commits; predicted path optional. Engine-side
correctness only.
