# Structure & process steering — fcitx5-oxpinyin

## Layout

```
src/    oxpinyin.{h,cpp}; oxpinyin-addon.conf.in.in; oxpinyin.conf.in
test/   testdir.h.in, testoxpinyin.cpp, addon/ + inputmethod/ (copy targets)
po/     gettext (fcitx5_install_translation)
.kiro/  this steering + specs/foundation (phase status)
.github/workflows/ci.yml
```

## Process

- Phased work with a STOP gate after every phase: report gate results, ctest
  names + status, sanitizer status, head SHA; wait for go/no-go.
- Explain-back before code on tagged work (engine loop, preedit,
  constraints, CI, packaging).
- Commit identity: author AND committer `Shenghao Chen <shenghaoc@outlook.com>`
  + `Assisted-by:` trailer naming vendor and model.

## Gates

1. cmake configure + build clean on gcc AND clang.
2. `clang-format --dry-run -Werror` clean over `src/` and `test/`.
3. ctest green (headless TestFrontend harness; env-seamed data dirs).
4. CI matrix {gcc, clang} × archlinux:latest, one fedora:latest job, one
   ASan/UBSan job; sanitizer findings block; clang-tidy advisory.

## STOP conditions (any phase)

- A needed symbol missing from oxpinyin's exports (report; capi-side work,
  never a C++ shim).
- Any client-side constraint/partial-choice reimplementation outside the
  choose/clear_constraint calls.
- ASan/UBSan findings (fix before proceeding).
- Any test needing real session IM registration.

## Safety

The hard safety rule in AGENTS.md governs all testing. The harness env-seams
(`OXPINYIN_SYSTEM_DATA_DIR` / `OXPINYIN_USER_DATA_DIR`) exist so tests never
touch live session state.
