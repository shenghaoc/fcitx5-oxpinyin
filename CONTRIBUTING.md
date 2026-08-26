# Contributing to fcitx5-oxpinyin

Thanks for your interest. A few things make contributing here different from
a typical C++ repo, so please read the short ground rules first.

## Ground rules

1. **This repository stays a thin frontend shell.** Parsing, candidate
   generation/ranking, prediction, and the user model belong to the engine
   (upstream [oxpinyin](https://github.com/shenghaoc/oxpinyin) or libpinyin),
   reached through its C ABI. If something is missing there, it gets fixed
   upstream and exported through the API — never reimplemented or shimmed in
   this shell.
2. **Never register the addon as your live session input method while
   developing or testing.** Automated work uses the headless TestFrontend
   suite; visual checks use a nested compositor or VM. Recovery guidance if
   anything goes wrong: [DEVELOPMENT.md](DEVELOPMENT.md).
3. **Binding working rules** (engine API contract, phased workflow with
   STOP reports, pin discipline) live in [AGENTS.md](AGENTS.md); they apply
   equally to human contributors and coding agents. For tagged areas of the
   code (the engine call loop, preedit, constraint handling, CI,
   packaging), changes are preceded by an explain-back round before code is
   written.

## Getting started

Build and test setup: [DEVELOPMENT.md](DEVELOPMENT.md).
What the test suite covers and how to run it: [TESTING.md](TESTING.md).

Every change should keep this full matrix green locally before you open a
PR — clean configure, build, and tests once per compiler, then formatting:

```sh
cmake -B build-gcc -DCMAKE_BUILD_TYPE=Release
cmake --build build-gcc && ctest --test-dir build-gcc --output-on-failure

CC=clang CXX=clang++ cmake -B build-clang -DCMAKE_BUILD_TYPE=Release
cmake --build build-clang && ctest --test-dir build-clang --output-on-failure

clang-format --dry-run -Werror src/*.h src/*.cpp test/*.cpp
```

CI builds with both GCC and Clang on Arch Linux containers against the
distro libpinyin (plus optional-feature variants); those jobs must pass.
[DEVELOPMENT.md](DEVELOPMENT.md) is the authoritative list of options and
gates.

## Commit conventions

Commits follow the conventional style used throughout the history
(`feat:`, `fix:`, `test:`, `docs:`, `chore:` — lowercase imperative
summary). Author and committer identity rules plus an `Assisted-by:`
trailer are enforced by a commit-msg hook locally and re-checked in CI;
see [DEVELOPMENT.md](DEVELOPMENT.md) ("Commit rules") for the exact form
and one-time setup commands.

AI-assisted contributions are welcome under the same rules: agent identity
belongs in the `Assisted-by:` trailer only — never `Co-authored-by:`, never
as author/committer.

## Reporting issues

Include: the addon version/commit, which engine backend and version
(`ENGINE=` setting), the fcitx5 version, how the engine's model data is
provisioned (relevant environment variables), and the fcitx5 diagnostic log
around the failure — initialization problems almost always name the missing
piece there. See the README's "Engine model data" section for what the
engine requires.

## License

By contributing you agree that your contributions are licensed under
GPL-3.0-or-later, matching [LICENSE](LICENSE).
