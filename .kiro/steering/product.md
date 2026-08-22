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
- Verified upstream, not here: engine output correctness (pinned by
  oxpinyin's oracle differentials). Do not re-litigate parity in this repo.
- Out of scope: full-desktop/KDE integration testing, distribution packaging
  polish beyond CPack DEB/RPM generation.

## Non-goals

- No engine logic in C++ (no candidate ranking, no segmentation, no
  constraint bookkeeping).
- No live-session registration on development machines (see the hard safety
  rule in AGENTS.md).
- No C++ shims for missing engine exports — missing symbols are engine-side
  work (STOP and report).
