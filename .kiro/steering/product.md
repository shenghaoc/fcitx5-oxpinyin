# Product steering — fcitx5-oxpinyin

## Purpose

A fcitx5 input-method addon for Chinese pinyin, powered by the oxpinyin engine
(Rust, exposed via its C ABI `libpinyin_capi` / `pinyin.h`). This repository
contains only the C++20 shell: fcitx5 addon registration, key-event handling,
candidate presentation, preedit, and configuration. Decoding, ranking, and
user-model persistence belong to the engine and stay in the engine repo.

## Verification scope

- Verified here, headless: engine call sequencing, fcitx5 wiring, preedit
  composition, configuration application — all through the TestFrontend
  harness in `test/`.
- Engine correctness belongs upstream in oxpinyin's oracle differentials.
  Representative pinned comparisons accompanied frontend acceptance; full
  parity and the same-binary substitution gate remain open in RELEASE.md.
- Manual desktop evidence: Fedora 44 KDE Wayland / Qt / basic Chrome passed
  2026-09-30, including a clean session restart (see TESTING.md). No automated
  desktop harness exists; X11/GTK/GNOME remain unvalidated.
- Distribution packaging polish remains a separate workstream.

## Non-goals

- No engine logic in C++ (no candidate ranking, no segmentation, no
  constraint bookkeeping).
- No live-session registration/manipulation by ordinary development or
  automation. Explicitly maintainer-authorized manual acceptance is the only
  exception, under the safeguards in AGENTS.md.
- No C++ shims for missing engine exports — missing symbols are engine-side
  work (STOP and report).
