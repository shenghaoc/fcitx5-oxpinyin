# Product steering — fcitx5-oxpinyin

## Purpose

A fcitx5 input-method addon for Chinese pinyin, powered by the oxpinyin engine
(Rust, exposed via its drop-in C ABI `libpinyin.so.15` / `pinyin.h`). This repository
contains only the C++20 shell: fcitx5 addon registration, key-event handling,
candidate presentation, preedit, and configuration. Decoding, ranking, and
user-model persistence belong to the engine and stay in the engine repo.

## Verification scope

- Verified here, headless: engine call sequencing, fcitx5 wiring, preedit
  composition, configuration application — all through the TestFrontend
  harness in `test/`.
- Engine correctness belongs upstream in oxpinyin's oracle differentials. This
  repository owns using the libpinyin-compatible ABI correctly, not
  bug-for-bug parity: the same-addon-binary substitution gate and its bounded
  Tkrzw/model20 comparison are integration evidence, and parity is not a
  frontend release criterion (see RELEASE.md).
- Manual desktop evidence: the declared 0.1.0 scope is Fedora 44 KDE Plasma
  Wayland / Qt / basic Chrome, accepted 2026-09-30 including a clean session
  restart (see TESTING.md). No automated desktop harness exists; X11, GTK,
  GNOME and Debian/Kubuntu desktops are unvalidated, not blockers.
- Packaging scope: CPack RPM/DEB/TXZ are validated; a Fedora-native `.spec` is
  reference material; Debian-policy `debian/` packaging is a later project.

## Non-goals

- No engine logic in C++ (no candidate ranking, no segmentation, no
  constraint bookkeeping).
- No live-session registration/manipulation by ordinary development or
  automation. Explicitly maintainer-authorized manual acceptance is the only
  exception, under the safeguards in AGENTS.md.
- No C++ shims for missing engine exports — missing symbols are engine-side
  work (STOP and report).
