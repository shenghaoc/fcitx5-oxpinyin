# Release status & checklist

This file tracks two things: where the project honestly stands on the road
to its first public release, and what has to be true before one happens.
Status statements here are evidence-based — checked against the source tree,
the test suite, and real runs — and dated so staleness is detectable.

## Current release status

Snapshot: **2026-08-27**, version `0.1.0` (from `project()`), no git tag and
no published artifact exists yet.

| Area                    | Status                                                                                                                                                                                                                             |
| ----------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Frontend implementation | The full feature set described in the [README](README.md) — schemes (full/double pinyin/Zhuyin), candidate flow with constrained selection, preedit/aux text, prediction, English candidates, punctuation delegation, s2t/full-width toggle surfacing, optional Cloud Pinyin and lua candidates, status-bar toggles — is implemented and covered by the headless suite |
| Automated testing       | Headless ctest runners pass across the baseline and optional-feature configurations; verified 2026-08-27: baseline 4/4, cloud 6/6, lua 5/5 in a CI-equivalent Arch container. Sanitizer coverage available locally via `-DENABLE_SANITIZER=ON`. No fuzz smoke tests exist yet |
| Engine parity           | **Open.** Behavioural parity between the libpinyin and oxpinyin backends is neither proven nor claimed; differences are known to exist (e.g. apostrophe parse handling). Same-suite runs against both backends are the standing method to compare |
| Desktop integration     | **Not started.** Wayland/X11 panels, GTK/Qt client behaviour, browsers, KDE/GNOME specifics have no automated coverage and no completed manual validation pass. A dedicated desktop-integration effort is planned                          |
| Packaging               | **Scaffold only.** Install rules work (`cmake --install` into the live filesystem prefix) and CPack metadata exists, but generated DEB/RPM output is unvalidated, the staged-install bug below is unfixed, and there are no distro packages |
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
3. Double pinyin lacks a dedicated end-to-end composition regression test
   (wiring and scheme-switching are covered).
4. Conversion correctness for simplified/traditional and full-width belongs
   to the chinese-addons modules; this addon pins only their status-area
   wiring.
5. The engine initializes fail-closed: without complete system model data
   the addon does not load (documented debugging path in the README).
6. Cloud Pinyin's network path cannot be asserted end-to-end by automation;
   tests cover the deterministic surface plus a synchronous stub.
7. The addon manifest declares optional dependencies (`quickphrase`,
   `notifications`, `pinyinhelper`) that no shell code references directly;
   their intent should be revisited before release.

## Release checklist

Only check items with concrete, reproducible evidence.

### Functionality

- [ ] Frontend feature set declared complete by the maintainer
- [ ] libpinyin backend validated against the full suite
- [ ] oxpinyin backend validated against the full suite
- [ ] libpinyin/oxpinyin behavioural parity investigated and differences dispositioned
- [ ] Supported oxpinyin database/model backends validated where applicable

### Testing

- [x] TestFrontend suite passes (baseline + optional variants; verified 2026-08-27)
- [x] Regression suite passes (same run)
- [ ] Sanitizer suite passes on the current tree (`-DENABLE_SANITIZER=ON`; rerun at release time)
- [ ] Static analysis passes (not wired up yet)
- [ ] Fuzz smoke tests pass (not written yet)
- [ ] Real desktop testing completed (pending dedicated effort)

### Desktop

Desktop items lack any validation infrastructure today; each needs at least
one recorded manual pass in a controlled environment once the desktop-
integration effort lands, ideally automated afterwards.

- [ ] Wayland
- [ ] X11
- [ ] GTK applications
- [ ] Qt applications
- [ ] Browser compatibility (client-side preedit quirks)
- [ ] KDE Plasma
- [ ] GNOME

### Packaging

- [ ] Install-tree validated in a clean staging environment (see blocker B1)
- [ ] Runtime dependencies validated on a minimal installation
- [ ] Package metadata validated (`metainfo`, addons confs)
- [x] Model data installation documented (README "Engine model data")
- [ ] DEB/RPM packaging validated (CPack output is raw scaffolding today)
- [ ] Distro packaging requirements documented (debian/, .spec, dependencies incl. engine *data* packages)

### Documentation

- [x] README current (this change set; includes accurate status + architecture)
- [x] Build instructions current (verified against the tree 2026-08-27)
- [x] Testing instructions current ([TESTING.md](TESTING.md))
- [x] Known limitations documented (above and README)
- [ ] Release notes prepared

## Release blockers found during documentation audit

- **B1 — staged installs lose the addon config.**
  `src/CMakeLists.txt` installs `oxpinyin-addon.conf` (renamed
  `addon/oxpinyin.conf`) to `"${FCITX_INSTALL_PKGDATADIR}/addon"`, which
  fcitx5 defines as an **absolute** path (`/usr/share/fcitx5`). With
  `cmake --install build --prefix=<staging>` (or DESTDIR/component-based
  packaging) that file escapes the staging tree — observed 2026-08-27: the
  staged install contained the `.so`, the input-method conf, and the
  metainfo, but not the addon conf. Fix direction: use a relocatable
  destination (e.g. `${CMAKE_INSTALL_DATADIR}/fcitx5/addon`). Owned by the
  packaging effort; intentionally not touched by documentation changes.
- **B2 — version/release entry mismatch.** Metainfo claims release
  `0.1.0`/2026-08-23 while no tag or published release exists. Sync when
  tagging (or strip the entry until then).
- **B3 — phase specs lag history.** `.kiro/specs/foundation/tasks.md`
  stops mid-project while later phases landed on main; refresh or archive
  before pointing external readers at it.

## Items tracked separately

Documentation must not duplicate or preempt active parallel efforts, so
these are named here without detail:

- **CI hardening** — static analysis, sanitizer jobs in CI, token
  permissions and similar security tightening is being advanced as its own
  workstream.
- **Desktop-integration validation and packaging** — real-desktop test
  passes, packaging quality work, and distro distribution belong to another
  workstream; results will supersede the provisional wording above.

## Making a release (sketch)

1. Empty every unchecked box above (with evidence) or consciously
   disposition it in a release-notes paragraph.
2. Set the version in `project()` and the metainfo `<release>` entry
   together; fix B1 first so install-staging checks exercise reality.
3. Run the full gate matrix (both compilers, clang-format, ctest, sanitizer
   build) on the release commit.
4. Tag, publish sources + notes, and only then update distro/packaging
   claims anywhere they appear.
