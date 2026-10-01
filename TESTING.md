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
- `oxpinyin` (shadowed development build): a complete backend-compatible
  exported data directory. The pinned Tkrzw bootstrap produces the
  libpinyin-compatible `.bin`/`.db` tables and `table.conf`; installing the
  shared library alone does not provide a model. Use the supported bootstrap
  and consult PACKAGING.md rather than historical backend-extension recipes.

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

## Package-installed acceptance (Fedora-native RPM)

**Status: executed and passed on 2026-10-04 (see Result).** The earlier 2026-09-30 pass used a
development prefix. This run repeats the important KDE checks with the
Fedora-native RPM from `packaging/fedora/` in a normal installation shape: no
`LD_LIBRARY_PATH`, no development-prefix addon search path, no build-tree model
override. It is maintainer-authorized under the live-session safeguards in
[AGENTS.md](AGENTS.md); the human performs every GUI step and the shell side is
automated by `tools/acceptance/packaged-kde.sh` (it never runs sudo, never
restarts Fcitx5 and keeps evidence outside the repository in
`~/oxpinyin-packaged-acceptance/`).

Artifact under test (recorded by `record-rpm`):

| Item | Value |
| --- | --- |
| NEVRA | `fcitx5-oxpinyin-0.1.0-1.fc44.x86_64` |
| RPM sha256 | `858c15649384a8545eb7a5c25cf33dbd4786dbb63a9042a2ac6c208a72d4cdc6` |
| addon sha256 (`/usr/lib64/fcitx5/oxpinyin.so`) | `528ded6c5ba2e81b6c5c80cf775317307822e22d40a4b0b307d6f5f532217f0e` |
| source commit | `a3615f4cf8e378fea9a0f852801e3e6ef0edee83` (head of `packaging/fedora-spec` when built; later review-fix rewrites changed the commit id but not the shipped addon, hash above) |
| build | clean `mock -r fedora-44-x86_64`, `%check` 5/5, rpmlint clean |
| expected engine | Fedora `libpinyin-2.11.91-2.fc44` (`/usr/lib64/libpinyin.so.15.0.0`) |

The engine is Fedora's ordinary libpinyin, because that is what this package
builds against; oxpinyin substitution is a separate packaging concern covered
by the same-binary gate above, not by this run.

Non-live preflight already performed: the addon extracted from this RPM,
with the distro libpinyin and a **copy** of the current learned user state,
composed `nihao` and committed through the headless probe.

A development setup currently shadows the package: the running daemon maps
`build-acceptance/.../oxpinyin.so` and the Rust engine, selected by
`~/.local/share/fcitx5/{addon,inputmethod}/oxpinyin.conf`. Those two
descriptors must be moved aside or the user-level descriptor wins over
`/usr/share/fcitx5/addon/oxpinyin.conf`.

### Maintainer runbook

Recovery first (record it): if the keyboard becomes unusable, press
Ctrl+Alt+F3, log in, run `pkill fcitx5`. `keyboard-us` and chinese-addons
`pinyin` remain in the profile as known-good input methods.

```sh
tools/acceptance/packaged-kde.sh preflight         # read-only audit (done)
tools/acceptance/packaged-kde.sh backup            # done; repeat if unsure
tools/acceptance/packaged-kde.sh shadow-aside      # move dev descriptors aside
tools/acceptance/packaged-kde.sh install-cmds      # prints the sudo commands
sudo dnf install ~/oxpinyin-packaged-acceptance/rpm/fcitx5-oxpinyin-0.1.0-1.fc44.x86_64.rpm
rpm -V fcitx5-oxpinyin                             # must print nothing
```

Gate A (optional smoke): restart only Fcitx5 and type `nihao` in KWrite.
Gate B (the real run): log out and back in so the daemon is a fresh child of the
Plasma session, then:

```sh
tools/acceptance/packaged-kde.sh log-on            # optional oxpinyin=5 logging
tools/acceptance/packaged-kde.sh verify post-login # fails unless the package is what runs
```

`verify` asserts that exactly `/usr/lib64/fcitx5/oxpinyin.so` (owned by
`fcitx5-oxpinyin`, `rpm -V` clean) and `/usr/lib64/libpinyin.so.15.0.0` (owned
by `libpinyin`) are mapped by the daemon, that no build/home/tmp path is
mapped, no development descriptors or `LD_LIBRARY_PATH`/`OXPINYIN_*`
overrides exist, the daemon started after the package install and the
installed NEVRA equals the recorded one. It stores the D-Bus identity,
journal excerpt and `fcitx5-diagnose` in the evidence directory.

GUI checklist (KWrite unless stated; record pass/fail per line):

1. select packaged OXPinyin
2. `nihao` shows composition and candidates
3. Space commits `你好`
4. Escape cancels composition
5. candidate paging visibly works
6. page-local numeric selection works
7. `woaizhongguo` commits the text expected from the current learned state
8. switch away and back
9. log out and in
10. repeat a simple composition and commit

Also: a Konsole text field or Find, and basic Chrome input. GNOME, GTK, X11 and
other browsers are out of scope.

Rollback: `tools/acceptance/packaged-kde.sh rollback-cmds` restores the moved
descriptors and prints `sudo dnf remove fcitx5-oxpinyin`; the full config
backup restore steps are in the backup's `RESTORE.txt`.

### Result

Executed by the maintainer on 2026-10-04 (Fedora 44, KDE Plasma Wayland) with
the artifact above installed from the local RPM; `rpm -V fcitx5-oxpinyin` was
clean. After a fresh login `verify post-login` passed: Fcitx5 (PID 2038,
started after the install) mapped exactly `/usr/lib64/fcitx5/oxpinyin.so`
(sha256 `528ded6c…17f0e`, equal to the recorded payload digest, owned by
`fcitx5-oxpinyin-0.1.0-1.fc44`) and `/usr/lib64/libpinyin.so.15.0.0` (owned by
`libpinyin-2.11.91-2.fc44`, `rpm -V` clean) plus the system model data under
`/usr/lib64/libpinyin/data/`; no development descriptor or build-acceptance
path shadowed the package.

GUI results reported by the maintainer, all PASS: in KWrite, activating
OXPinyin, `nihao` → `你好`, Escape cancellation, Page Down/Page Up, page-2
numeric selection, Backspace/editing, switching away and back, and
`woaizhongguo` commit; Konsole Find; basic Chrome input. No errors or
anomalies. Checklist items 9 and 10 also PASS: after a clean logout/login with
the packaged RPM loaded (evidenced by the post-login verification), the
maintainer repeated a composition and committed it in KWrite. Evidence (runtime map, journal, `fcitx5-diagnose`) is kept locally in
`~/oxpinyin-packaged-acceptance/evidence/post-login/` and is not committed.

Fedora 44 KDE Wayland acceptance has also been completed using the
Fedora-native packaged installation. This is stronger than the earlier
development-prefix acceptance, but it is manual evidence for the declared
scope only: it does not claim official Fedora repository support, other
desktops or browsers, or oxpinyin-engine behaviour (the engine here was
Fedora's libpinyin).
