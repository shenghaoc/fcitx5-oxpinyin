# Testing guide

Testing an input method has unusual hazard potential — a misbehaving IM can
leave a desktop without a usable keyboard — so this project's testing is
deliberately layered, with **nothing** ever registered as a live session
input method (see [DEVELOPMENT.md](DEVELOPMENT.md)).

## The layers

1. **Automated headless tests** (`ctest`) — the entire automated layer,
   run in-process against fcitx5's TestFrontend.
2. **Sanitizer builds** — the same suite compiled with ASan+UBSan
   (`-DENABLE_SANITIZER=ON`).
3. **Static analysis / fuzz smoke tests** — not set up yet; tracked as an
   open item in [RELEASE.md](RELEASE.md).
4. **Real desktop integration testing** — explicitly *not* covered by this
   repository's automation; it is a separate, currently pending effort (see
   [RELEASE.md](RELEASE.md)). Manual methods are described at the bottom.

## How the headless harness works

The suite links fcitx5's `TestFrontend` module: each test binary boots a
complete in-process fcitx5 (addon manager included), enables exactly the
addons passed to it, creates test input contexts, dispatches synthetic key
events, and asserts on preedit, auxiliary text, candidate lists, commits,
and status-area actions with a small custom assertion harness (no external
test framework). The addon configuration and input-method files are copied
into a private build directory for discovery, and the engine's *user* model
directory is pointed at throwaway locations, so tests neither read nor
write real user data.

Each interesting environment difference gets its own process (its own ctest
entry), because "an addon is disabled" must mean the module was truly
never loaded, not merely switched off:

| ctest name                  | Built when         | What it pins                                                                                                                                                                    |
| --------------------------- | ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `testoxpinyin`              | always             | The main body: load/passthrough, typing → candidates → commit, backspace/escape, paging, Space-selection, aux-text and client/server preedit reflection, live config round-trips, Zhuyin scheme switching, partial (constrained) selection including unpin-on-backspace, prediction chaining, English-candidate placement and selection, delegated punctuation (expectations read back from the chinese-addons punctuation module itself, never hardcoded), status-toggle action lifecycle |
| `testoxpinyin-nospell`      | always             | With the Spell module absent from the process, ordinary pinyin is unaffected and uppercase keys stay client keys                                                                  |
| `testoxpinyin-punctabsent`  | always             | Without the punctuation module the addon **fails to load** (hard-dependency enforcement); keystrokes then pass through untouched                                                  |
| `testoxpinyin-conv`         | always             | With chttrans + fullwidth enabled, their toggles join the status area exactly once; the whole normal-composition suite passes unchanged                                            |
| `testoxpinyin-cloudabsent`  | `ENABLE_CLOUDPINYIN` | Cloud code built but the cloudpinyin module not loaded: the guard skips injection, the toggle persists, composition stays intact                                                 |
| `testoxpinyin-cloudstub`    | `ENABLE_CLOUDPINYIN` | A hermetic in-tree stub cloudpinyin addon fills synchronously, exercising the complete row → select → commit path with **no network**                                             |
| `testoxpinyin-luaabsent`    | `ENABLE_LUA`       | imeapi absent: no lua rows injected, composition intact                                                                                                                           |

Honesty about the network: when the *real* cloudpinyin module is exercised,
the tests assert only what is deterministic without connectivity (the empty
placeholder row's position, the toggle hotkey, disable behaviour). Anything
requiring an actual server response goes through the synchronous stub.

Coverage gaps that exist today (also recorded in [RELEASE.md](RELEASE.md)):
double pinyin has no dedicated end-to-end composition test; simplified/
traditional and full-width conversion *correctness* belongs to the
chinese-addons modules and is only pinned here as far as their status-area
wiring; training effects of the engine's user model are asserted indirectly
only.

## Engine backend selection

The identical suite runs against either backend: configure with
`-DENGINE=libpinyin` (default; uses the distribution libpinyin and finds its
model data automatically) or `-DENGINE=oxpinyin` (needs oxpinyin installed
for pkg-config plus explicit data directories, see below). Comparing
results between the two is how frontend-vs-engine regressions get told
apart; full parity between engines is a release criterion, not a claim.

## Engine data requirements for tests

Every engine-loading runner needs valid system model data, and what that
means depends on `-DENGINE=`:

- `libpinyin` (default): the data directory shipped by its distribution
  package (`table.conf`, `pinyin_index.bin`, `phrase_index.bin`,
  `bigram.db`, …) — located automatically on any normal distro setup.
- `oxpinyin`: an exported data directory holding the `.redb` tables
  (`pinyin_index`, `phrase_index`, `bigram`) plus `interpolation2.text`,
  which installing the engine library alone does not provide.

ctest resolves either case automatically (compiled-in path or the
`OXPINYIN_SYSTEM_DATA_DIR` CMake variable wins); for manual control:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DOXPINYIN_SYSTEM_DATA_DIR=/path/to/engine-data \
      -DOXPINYIN_USER_DATA_DIR=$(mktemp -d)
```

If the system data is missing or incomplete the engine initializes
fail-closed, the addon refuses to load, and every test fails loudly — check
the configured directory first.

## Sanitizer builds

```sh
cmake -B build-asan -DCMAKE_BUILD_TYPE=Release -DENABLE_SANITIZER=ON
cmake --build build-asan && ctest --test-dir build-asan --output-on-failure
```

ASan+UBSan instrument the whole test (addon module plus harness).
`test/lsan-suppressions.txt` suppresses exactly one known upstream libpinyin
leak, scoped as narrowly as the stripped distro library allows; any other
leak still fails the run. Rationale for the scoping lives inside the file.

## Manual and desktop testing

The headless harness cannot see panels, candidate windows, client-app
preedit behaviour, Wayland/X11 specifics, or multi-application focus flows.
For occasional visual checks use one of:

- a **nested compositor** (weston or cage) running its own fcitx5 instance;
- a **disposable VM**, where registration as session IM is acceptable;
- an **isolated container/desktop session**.

Never swap your daily-driver session input method to test this addon. If a
session ever ends up without a working keyboard: switch to a TTY
(Ctrl+Alt+F3) and run `pkill fcitx5`. Systematic desktop-integration
validation across Wayland/X11/GTK/Qt/browsers/KDE/GNOME remains an open
release item — see the checklist in [RELEASE.md](RELEASE.md).
