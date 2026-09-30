# Testing guide

Testing an input method has unusual hazard potential — a misbehaving IM can
leave a desktop without a usable keyboard — so this project's testing is
deliberately layered. Automation stays isolated from the live input method;
explicitly authorized manual acceptance follows the safeguards in
[AGENTS.md](AGENTS.md) and [DEVELOPMENT.md](DEVELOPMENT.md).

## The layers

1. **Automated headless tests** (`ctest`) — the entire automated layer,
   run in-process against fcitx5's TestFrontend.
2. **Sanitizer builds** — the same suite compiled with ASan+UBSan
   (`-DENABLE_SANITIZER=ON`).
3. **Static analysis / fuzz smoke tests** — clang-tidy over the engine
   sources and a libFuzzer smoke of the input seam run in CI
   (`static-analysis`, `fuzz-smoke`; a bounded campaign runs on the
   nightly schedule); status tracked in [RELEASE.md](RELEASE.md).
4. **Real desktop integration testing** — explicitly *not* covered by this
   repository's automation. A manual Fedora 44 KDE Wayland pass completed on
   2026-09-30; X11, GTK and GNOME remain unvalidated (see
   [RELEASE.md](RELEASE.md)). Scope and methods are described below.

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
| `testoxpinyin-noengine`   | always             | The system data dir points at an empty directory, so `pinyin_init` fails closed and no addon backs the IM: the name still resolves, keys pass through unfiltered, the panel stays empty; the init-failure path leaks `pinyin_init`'s partial context (known upstream libpinyin bug), so this one runner swaps in `test/lsan-suppressions-noengine.txt` — every other runner keeps `pinyin_init` frames visible in its leak gate |
| `testoxpinyin-conv`         | always             | With chttrans + fullwidth enabled, their toggles join the status area exactly once; the whole normal-composition suite passes unchanged                                            |
| `testoxpinyin-cloudabsent`  | `ENABLE_CLOUDPINYIN` | Cloud code built but the cloudpinyin module not loaded: the guard skips injection, the toggle persists, composition stays intact                                                 |
| `testoxpinyin-cloudstub`    | `ENABLE_CLOUDPINYIN` | A hermetic in-tree stub cloudpinyin addon fills synchronously, exercising the complete row → select → commit path with **no network**                                             |
| `testoxpinyin-luaabsent`    | `ENABLE_LUA`       | imeapi absent: no lua rows injected, composition intact                                                                                                                           |

Honesty about the network: when the *real* cloudpinyin module is exercised,
the tests assert only what is deterministic without connectivity (the empty
placeholder row's position, the toggle hotkey, disable behaviour). Anything
requiring an actual server response goes through the synchronous stub.

Coverage gaps that exist today (also recorded in [RELEASE.md](RELEASE.md)):
double pinyin now has a dedicated ZRM composition/commit regression (not
every scheme/model combination); simplified/traditional and full-width conversion *correctness* belongs to the
chinese-addons modules and is only pinned here as far as their status-area
wiring; training effects of the engine's user model are asserted indirectly
only.

## Running against an oxpinyin engine

By default the suite runs against the distribution libpinyin and finds its
model data automatically. For *development* against the oxpinyin Rust
engine, shadow libpinyin in pkg-config with oxpinyin's exported
`libpinyin.pc` ([DEVELOPMENT.md](DEVELOPMENT.md) has the recipe) and give
the tests explicit data directories (see below).

Shadowing is a development convenience, **not** a controlled parity method:
it can configure a different addon binary, with different headers and data
paths. The release gate must build one identical addon binary and substitute
only `libpinyin.so.15`, with compatible API, equivalent model/user data and
options. The reproducible gate now lives in `tools/engine-substitution/`; representative
comparisons alone do not close broader engine parity. See
[RELEASE.md](RELEASE.md).

## Engine data requirements for tests

Every engine-loading runner needs valid system model data, and what that
means depends on the installed engine:

- `libpinyin` (default): the data directory shipped by its distribution
  package (`table.conf`, `pinyin_index.bin`, `phrase_index.bin`,
  `bigram.db`, …) — located automatically on any normal distro setup.
- `oxpinyin` (shadowed development build): an exported data directory
  holding the system tables
  (`pinyin_index`, `phrase_index`, `bigram` — named with the extension of
  the storage backend compiled into that engine build, `.tkt` for the
  default tkrzw build) plus `interpolation2.text`, which installing the
  engine library alone does not provide.

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

### Automated desktop testing

There is no automated desktop-integration harness or desktop CI coverage.
TestFrontend cannot establish visible candidate-window behaviour, real
application preedit/commits or Wayland/X11 focus integration. Routine visual
checks should use a nested compositor with its own Fcitx, a disposable VM or
another isolated desktop session.

Live-session manual acceptance requires explicit maintainer authorization for
a concrete goal and all [AGENTS.md](AGENTS.md) safeguards: configuration
backup, reversible changes, a known-good input method, runtime binary
verification and incremental smoke gates. The human performs GUI interaction
when automation cannot safely and controllably do so. Recovery must be
recorded before testing: Ctrl+Alt+F3, log in, then `pkill fcitx5`.

### Completed manual acceptance — 2026-09-30

An explicitly maintainer-authorized real-desktop pass completed on Fedora 44,
KDE Plasma 6.7.5, KDE Frameworks/KCoreAddons 6.30.0, Qt 6.11.2 and Fcitx5
5.1.22 in a Wayland session. Shell preparation/diagnostics accompanied human
GUI interaction; no autonomous Linux Computer Use drove the desktop.
Configuration was backed up, changes were reversible, a known-good input
method remained available, and smoke tests preceded broader testing.

Process maps and hashes verified the real Fcitx daemon loaded the intended
development addon and Rust engine. The accepted production implementation
was merged by [PR #18](https://github.com/shenghaoc/fcitx5-oxpinyin/pull/18).
The engine was oxpinyin `e1d915d0ac2532d4ac496404269005439a524759`, using
Tkrzw. The behavioural reference was pinned libpinyin 2.11.92
`074a2219c90feaf962d0d24f034514033ece5f99`, also Tkrzw, with checksum-verified
model20 and equivalent exported engine data. Fedora libpinyin 2.11.91 was
not the oracle.

The pass established activation, client preedit, candidate display, Space
and numeric selection, Page Up/Page Down, Escape cancellation, Backspace
editing, input-method switching and clean composition after reset. Real
application coverage included KWrite, Konsole's Find field and basic Chrome
browser input. After a clean Plasma/Fcitx logout/login restart, separate
`nihao` and `woaizhongguo` compositions committed `你好我爱中国` into KWrite
with the verified addon and Rust engine.

The investigation found two adapter notification bugs: clearing client
preedit omitted a client refresh, and candidate paging omitted an InputPanel
refresh. PR #18 fixed both; affected manual KDE retests passed.

Pinned representative engine comparisons agreed on parsing and candidate
ordering for the tested cases. Differences between fresh and live candidate
alternatives were reproduced exactly by pinned libpinyin using a copy of the
same persistent learned state, including `user.bin` and `user_bigram.db`.
They required no adapter ranking change. Traditional output was explained by
Fcitx's Traditional conversion toggle, not an OXPinyin defect.

This is manual acceptance evidence, not CI coverage or full engine parity.
X11, GTK and GNOME remain unvalidated; Chrome coverage does not validate
other browsers. The broader desktop matrix and same-binary engine
substitution coverage is tracked separately in [RELEASE.md](RELEASE.md).

## Same-binary engine substitution gate

`bash tools/engine-substitution/bootstrap.sh "$PWD/build-substitution"`
provisions isolated libraries using the pinned oxpinyin checkout's canonical
oracle/model bootstrap and supported packaging installer. All components use
Tkrzw; model20 is checksum verified. `pins.env` pins the engine revision;
that revision owns the oracle/model pins. Build the addon once with the
oracle prefix first on `PKG_CONFIG_PATH`, then run:

```sh
python3 tools/engine-substitution/run.py --build build \
  --oracle build-substitution/oracle --engine build-substitution/engine \
  --output build-substitution/evidence
```

The runner executes every configured headless test twice, overrides CTest's
data/user settings with isolated per-run directories, and substitutes only
the library search path. Linux loader audit logs prove actual mapped paths;
`identity.json` records canonical libraries and hashes plus the unchanged
addon hash. Missing-punctuation tests intentionally map neither addon nor
engine. Complete fresh-state parsing/candidate captures for nine inputs are
retained and compared without normalization. A difference fails the gate
and must be classified; it is never hidden by changing adapter ranking.
This bounded differential does not claim global engine parity. CI runs GCC
and Clang and retains evidence even on failure.
