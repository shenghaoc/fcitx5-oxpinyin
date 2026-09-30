# Release status & checklist

This file tracks two things: where the project honestly stands on the road
to its first public release, and what has to be true before one happens.
Status statements here are evidence-based — checked against the source tree,
the test suite, and real runs — and dated so staleness is detectable.

## Current release status

Snapshot: **2026-09-30**, version `0.1.0` (from `project()`), no git tag and
no published artifact exists yet.

| Area                    | Status                                                                                                                                                                                                                             |
| ----------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Frontend implementation | The full feature set described in the [README](README.md) — schemes (full/double pinyin/Zhuyin), candidate flow with constrained selection, preedit/aux text, prediction, English candidates, punctuation delegation, s2t/full-width toggle surfacing, optional Cloud Pinyin and lua candidates, status-bar toggles — is implemented and covered by the headless suite |
| Automated testing       | Headless ctest runners pass across the baseline and optional-feature configurations; verified 2026-09-07 at 02750dd: baseline 5/5, cloud 7/7, lua 6/6 in a CI-equivalent Arch container. Sanitizer suite clean under ASan+UBSan on the same date (clang, RelWithDebInfo) and run in CI as `check-sanitizer`. Fuzz smoke harnesses exist (`test/fuzz`: libFuzzer over the shell's input-handling seam; deterministic smoke on PRs, bounded campaign nightly) |
| Engine parity           | **Open.** The 2026-09-30 KDE investigation compared representative inputs against pinned libpinyin 2.11.92 and oxpinyin (Tkrzw/model20), finding identical parsing/candidate ordering for tested cases and reproducing learned-state differences under identical persistent state. This is not full parity. The reproducible same-binary substitution gate is implemented in `tools/engine-substitution/`; its bounded differential is not a global parity claim. See [TESTING.md](TESTING.md) for reference revisions and scope |
| Desktop integration     | **Partial / in progress.** A real Fedora 44 KDE Plasma Wayland manual acceptance pass completed 2026-09-30: Qt/KDE application behaviour, basic Chrome input and clean Plasma/Fcitx restart validated using verified addon/engine binaries. No automated desktop-integration harness exists; X11, GTK and GNOME remain unvalidated. See [TESTING.md](TESTING.md) |
| Packaging               | **Validated CPack artifacts; distro policy work remains.** Native Fedora 44 RPM and Debian unstable DEB contents, dependency resolution and installed-addon TestFrontend composition/commit validated 2026-10-01; TXZ contents and AppStream/config metadata checked. Both install staging forms pass. No artifacts published; full `.spec`/`debian/` integration remains separate. See [PACKAGING.md](PACKAGING.md) |
| Internationalization    | Translation catalogs are absent (`po/LINGUAS` empty); gettext scaffolding only                                                                                                                                                    |

The AppStream metainfo currently advertises `0.1.0` (2026-08-23); there is
no matching tag or release yet — the `<releases>` entry must be kept in sync
when the first tag is cut.

## Known limitations

1. Delegated punctuation is stateless: repeated smart quotes do not
   alternate open/close as some other engines do. Intentional trade-off of
   delegating to chinese-addons' shared punctuation module.
2. Spell/Cloud Pinyin/lua candidate rows commit directly and deliberately do
   not feed the engine's user model.
3. Double pinyin has a dedicated ZRM TestFrontend composition/commit
   regression; exhaustive scheme/model validation is not claimed.
4. Conversion correctness for simplified/traditional and full-width belongs
   to the chinese-addons modules; this addon pins only their status-area
   wiring.
5. The engine initializes fail-closed: without complete system model data
   the addon does not load (documented debugging path in the README).
6. Cloud Pinyin's network path cannot be asserted end-to-end by automation;
   tests cover the deterministic surface plus a synchronous stub.

## Release checklist

Only check items with concrete, reproducible evidence.

### Functionality

- [ ] Frontend feature set declared complete by the maintainer
- [x] Manifest optional dependencies audited (unused quickphrase/notifications/pinyinhelper edges removed; used conversion/Spell/Cloud/Lua entries retained, 2026-10-01)
- [x] libpinyin.so.15 substitution run validated against the configured baseline full suite (5/5 under each pinned engine, GCC and Clang, 2026-09-30; unchanged addon hashes and loader mappings retained by the gate)
- [ ] libpinyin/oxpinyin behavioural parity investigated and differences dispositioned
- [ ] Supported oxpinyin database/model backends validated where applicable

### Testing

- [x] TestFrontend suite passes (baseline + optional variants; verified 2026-09-07 at 02750dd: baseline 5/5, cloud 7/7, lua 6/6)
- [x] Regression suite passes (same runs)
- [x] Dedicated double-pinyin composition/commit regression (ZRM through TestFrontend; expected candidate read from the same engine full-pinyin path, 2026-10-01)
- [x] Sanitizer suite passes on the current tree (verified 2026-09-07 at 02750dd: 5/5 under ASan+UBSan, clang/RelWithDebInfo via `.github/scripts/build-test.sh`; also a CI job, `check-sanitizer`, since fd32b32; rerun at release time)
- [x] Static analysis passes (clang-tidy over `src/oxpinyin.cpp` + `src/englishness.cpp` — zero findings, 2026-09-07 at 02750dd; a CI job, `static-analysis`, since fd32b32)
- [x] Fuzz smoke tests pass (`fuzz-englishness-smoke` — libFuzzer replays the checked-in seed corpus plus 2000 deterministic runs — green 2026-09-07 at 02750dd; PR gate `fuzz-smoke` in CI, bounded campaign on the nightly schedule)
- [ ] Real desktop testing completed (overall matrix open; KDE/Wayland/Qt and basic Chrome scope passed 2026-09-30; X11/GTK/GNOME remain)

### Desktop

Checked items record the 2026-09-30 manual Fedora 44 acceptance scope,
including a clean Plasma/Fcitx restart. No automated desktop harness exists;
the remaining matrix still requires evidence.

- [x] Wayland (Fedora 44 KDE session)
- [ ] X11
- [ ] GTK applications
- [x] Qt applications (KWrite and Konsole Find)
- [x] Browser compatibility (Chrome basic preedit/selection/commit only; other browsers unvalidated)
- [x] KDE Plasma (6.7.5; clean logout/login restart validated)
- [ ] GNOME

### Packaging

- [x] Install-tree validated in isolated staging (DESTDIR and direct `--prefix=`, with all manifest paths checked; B1 resolved 2026-09-30)
- [x] Runtime dependencies validated on isolated native Fedora RPM and Debian DEB runtime installations (2026-10-01; TestFrontend instrumentation supplied separately)
- [x] Package metadata validated (AppStream schema plus addon/input-method linkage, dependency declarations and installed payloads; 2026-10-01; release/tag synchronization remains B2)
- [x] Model data installation documented (README "Engine model data")
- [x] CPack DEB/RPM packaging validated (native contents, dependency resolution and installed-addon headless smoke, 2026-10-01; full distro policy integration is not claimed)
- [ ] Distro packaging requirements documented (debian/, .spec, dependencies incl. engine *data* packages)

### Documentation

- [x] README current (reviewed 2026-09-30; includes accurate status + architecture)
- [x] Build instructions current (verified against the tree 2026-09-15)
- [x] Testing instructions current ([TESTING.md](TESTING.md); reviewed 2026-09-30)
- [x] Known limitations documented (above and README)
- [ ] Release notes prepared

## Release blockers found during documentation audit

- **B1 — resolved: relocatable addon configuration.** The absolute addon
  config destination escaped `--prefix=` (reproduced under a disposable
  DESTDIR guard on 2026-09-30). Addon config and optional Lua extension now
  use relative GNUInstallDirs destinations. `install-check.py` asserts all
  manifest paths and required files for DESTDIR and direct `--prefix=`;
  package-check runs this regression. No live installation is touched.
- **B2 — version/release entry mismatch.** Metainfo claims release
  `0.1.0`/2026-08-23 while no tag or published release exists. Sync when
  tagging (or strip the entry until then).
- **B3 — phase specs lag history.** `.kiro/specs/foundation/tasks.md`
  stops mid-project while later phases landed on main; refresh or archive
  before pointing external readers at it.

## Items tracked separately

Documentation must not duplicate or preempt active parallel efforts, so
these are named here without detail:

- **CI hardening** — the static-analysis, check-sanitizer, fuzz-smoke and
  package-check jobs landed on main in fd32b32 (2026-08-27); further
  tightening (workflow permissions are already `contents: read`) continues
  as its own workstream.
- **Remaining desktop integration and packaging** — X11/GTK/GNOME,
  additional browser coverage and an automated desktop harness remain open
  beyond the recorded KDE Wayland pass. Packaging quality and distro
  distribution remain separate workstreams.

## Making a release (sketch)

1. Empty every unchecked box above (with evidence) or consciously
   disposition it in a release-notes paragraph.
2. Set the version in `project()` and the metainfo `<release>` entry
   together; rerun install-staging checks on the release tree.
3. Run the full gate matrix (both compilers, clang-format, ctest, sanitizer
   build) on the release commit.
4. Tag, publish sources + notes, and only then update distro/packaging
   claims anywhere they appear.
