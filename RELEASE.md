# Release status & checklist

This file tracks two things: where the project honestly stands on the road
to its first public release, and what has to be true before one happens.
Status statements here are evidence-based — checked against the source tree,
the test suite, and real runs — and dated so staleness is detectable.

## Current release status

**Technically release-ready for the declared 0.1.0 scope.** No tag or artifact has been published. Final publication requires explicit maintainer authorization.

Every gate below passed on 2026-10-01 on the top-of-stack tree, and the
maintainer completed the package-installed Fedora KDE acceptance on 2026-10-04
(Fedora-native RPM, [TESTING.md](TESTING.md)). The later changes after the
matrix are documentation only. The shipped addon binary, configuration files,
catalogs and metainfo of the Fedora-native RPM are byte-identical (per-file
digests) to the accepted package; only the packaged documentation
(`README.md`, `RELEASE-NOTES.md`) differs.

Snapshot: **2026-10-01**, version `0.1.0` (from `project()`), no git tag and
no published artifact exists yet.

| Area                    | Status                                                                                                                                                                                                                             |
| ----------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Frontend implementation | The full feature set described in the [README](README.md) — schemes (full/double pinyin/Zhuyin), candidate flow with constrained selection, preedit/aux text, prediction, English candidates, punctuation delegation, s2t/full-width toggle surfacing, optional Cloud Pinyin and lua candidates, status-bar toggles — is implemented and covered by the headless suite |
| Automated testing       | Final gate matrix, 2026-10-01, clean export of the top-of-stack commit in Fedora 44 containers with warnings-as-errors: GCC and Clang Release baseline 5/5; Cloud 7/7 and Lua 6/6 on both compilers; Clang ASan+UBSan 5/5; fuzz-enabled suite 6/6; clang-tidy 0 findings; clang-format clean; commit lint and linter self-test 31/31. CI additionally runs the pinned same-binary substitution gate on every pull-request update and by manual dispatch (it does not run on direct pushes to `main`). See [TESTING.md](TESTING.md) |
| Engine compatibility    | **Frontend scope complete.** The addon uses only the libpinyin-compatible C ABI. The reproducible same-binary `libpinyin.so.15` substitution gate (`tools/engine-substitution/`) runs one unchanged addon binary under pinned libpinyin 2.11.92 and Rust oxpinyin (Tkrzw/model20) and compares bounded captures as integration evidence. Behavioural parity with libpinyin is **not** claimed and is not a frontend release criterion; engine defects belong in the oxpinyin repository. See [TESTING.md](TESTING.md) |
| Desktop integration     | **Declared 0.1.0 scope accepted.** Fedora 44 KDE Plasma Wayland, Qt/KDE applications, basic Chrome input and clean Plasma/Fcitx restart validated by manual acceptance on 2026-09-30 using verified addon/engine binaries. Fedora 44 KDE Wayland acceptance was repeated on 2026-10-04 with the Fedora-native RPM installed. No automated desktop harness exists. Other environments are unvalidated (listed below), not broken. See [TESTING.md](TESTING.md) |
| Packaging               | **Validated CPack artifacts and a Fedora-native `.spec`.** CPack RPM/DEB/TXZ contents, dependency resolution and installed-addon TestFrontend validation (Fedora 44 RPM, Debian unstable DEB) as of 2026-10-01. `packaging/fedora/fcitx5-oxpinyin.spec` clean-builds in `mock` for Fedora 44 (tests, AppStream and rpmlint clean, disposable install + headless smoke). It is reference material: **no Fedora/COPR submission has happened and no official availability is claimed.** No artifacts published; Debian-policy `debian/` packaging is a later project. See [PACKAGING.md](PACKAGING.md) |
| Internationalization    | Initial `zh_CN` and `zh_TW` catalogs ship through the project gettext pipeline; `.mo` placement, loading and manifest/metainfo translations are verified for DESTDIR, direct-prefix, RPM, DEB and TXZ payloads. Other locales are untranslated English |

The unreleased tree keeps `project()` at `0.1.0` but has no AppStream
release entry. Add the actual version/date together with the maintainer's
explicit tag/publish decision. [RELEASE-NOTES.md](RELEASE-NOTES.md) is a draft,
not evidence of a published release.

## 0.1.0 scope decisions

Settled by the maintainer:

- **Engine compatibility.** fcitx5-oxpinyin is responsible for using the
  libpinyin-compatible ABI correctly, not for tracking oxpinyin's progress
  toward bug-for-bug libpinyin semantics. The same-addon-binary substitution
  gate and its bounded Tkrzw/model20 comparison stay as integration
  evidence; no global parity is claimed. Full behavioural parity and
  per-backend/model coverage are not frontend release blockers. Engine
  defects are filed in the oxpinyin repository.
- **Declared desktop scope.** Fedora 44, KDE Plasma, Wayland, Fcitx5
  explicitly installed/configured, Qt/KDE applications, basic Chrome input
  and clean Plasma/Fcitx restart.
- **Packaging scope.** CPack RPM/DEB/TXZ validation stays supported. The
  next packaging step is a Fedora-native `.spec` as upstream/reference
  material; it is not evidence of Fedora acceptance. Debian-policy
  `debian/` packaging is a later project.

No substantive pre-0.1.0 work remains: translations, the Fedora-native spec,
the package-installed Fedora KDE acceptance and final hardening are done. No tag,
GitHub Release or package publication has been authorized.

Unvalidated environments (not blockers, not claimed to work or fail): GNOME,
GTK-specific testing, X11, Kubuntu/Ubuntu and other Debian desktops,
browsers other than basic Chrome, an automated GUI harness, Debian-native
`debian/` packaging and official Fedora repository inclusion.

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

- [x] Frontend feature set declared complete by the maintainer for the 0.1.0 scope (scope decisions above)
- [x] Manifest optional dependencies audited (unused quickphrase/notifications/pinyinhelper edges removed; used conversion/Spell/Cloud/Lua entries retained, 2026-10-01)
- [x] Same-addon-binary libpinyin.so.15 substitution gate validated against the configured baseline full suite (bounded integration evidence, not parity) (5/5 under each pinned engine, GCC and Clang, 2026-10-01; unchanged addon hashes and loader mappings retained by the gate)

### Testing

- [x] TestFrontend suite passes (baseline + optional variants; GCC and Clang, verified 2026-10-01: baseline 5/5, cloud 7/7, lua 6/6)
- [x] Regression suite passes (same runs)
- [x] Dedicated double-pinyin composition/commit regression (ZRM through TestFrontend; expected candidate read from the same engine full-pinyin path, 2026-10-01)
- [x] Sanitizer suite passes (Clang RelWithDebInfo ASan+UBSan 5/5, 2026-10-01)
- [x] Static analysis passes (clang-tidy on `src/oxpinyin.cpp` and `src/englishness.cpp`, zero findings, 2026-10-01)
- [x] Fuzz smoke tests pass (fuzz-enabled suite 6/6 including deterministic libFuzzer corpus/2000-run smoke, 2026-10-01; nightly campaign is separate)
- [x] Declared 0.1.0 desktop acceptance scope completed: Fedora 44 KDE Plasma Wayland, Qt/KDE applications, basic Chrome input, clean Fcitx restart (2026-09-30)

### Desktop

Declared tested release scope (manual acceptance, 2026-09-30; no automated
desktop harness exists):

- [x] Fedora 44 KDE Plasma 6.7.5, Wayland
- [x] Qt/KDE applications (KWrite and Konsole Find)
- [x] Basic Chrome input (preedit/selection/commit only)
- [x] Clean logout/login Plasma/Fcitx restart

Unvalidated environments (not 0.1.0 blockers): X11, GTK applications,
GNOME, Kubuntu/Ubuntu/Debian desktops, other browsers.

### Packaging

- [x] Install-tree validated in isolated staging (DESTDIR and direct `--prefix=`, with all manifest paths checked; B1 resolved 2026-09-30)
- [x] Runtime dependencies validated on isolated native Fedora RPM and Debian DEB runtime installations (2026-10-01; TestFrontend instrumentation supplied separately)
- [x] Package metadata validated (AppStream schema plus addon/input-method linkage, dependency declarations and installed payloads; 2026-10-01; unreleased metainfo does not claim a tag)
- [x] Model data installation documented (README "Engine model data")
- [x] CPack DEB/RPM packaging validated (native contents, dependency resolution and installed-addon headless smoke, 2026-10-01; full distro policy integration is not claimed)
- [x] Fedora-native `.spec` added and clean-built: mock Fedora 44 SRPM/RPM, `%check` 5/5, rpmlint clean, manifest/ownership/dependency/RPATH/build-path/AppStream/translation checks and a disposable-install headless smoke (2026-10-01; reference material, not a Fedora submission; `debian/` is a later, non-blocking project)

- [x] Initial zh_CN/zh_TW translations built, installed and payload-verified (`msgfmt --check`, template freshness, `.mo` locale hierarchy and loaded strings in DESTDIR/prefix/RPM/DEB/TXZ payloads, 2026-10-01)

- [x] Final package matrix on the top-of-stack tree (2026-10-01): `install-check.py` (DESTDIR, `--prefix=`, direct), `artifact-check.py` (CPack RPM/DEB/TXZ payloads, dependencies, AppStream, translations), CPack RPM disposable Fedora 44 install + headless smoke, Fedora-native `mock` SRPM/RPM clean build (`%check` 5/5, rpmlint clean, `validate-rpm.py` 9/9). The Fedora-native RPM's addon binary, configs, catalogs and metainfo are byte-identical (per-file digests) to the accepted package (only packaged docs differ)
- [x] Package-installed Fedora KDE acceptance (2026-10-04, Fedora 44 KDE Wayland with the Fedora-native RPM: runtime identity verified, KWrite, Konsole Find and basic Chrome passed; see [TESTING.md](TESTING.md); no official Fedora support claimed)

### Documentation

- [x] README current (reviewed 2026-10-01; includes accurate status + architecture)
- [x] Build instructions current (supported bootstrap reviewed 2026-10-01)
- [x] Testing instructions current ([TESTING.md](TESTING.md); reviewed 2026-10-01)
- [x] Known limitations documented (above and README)
- [x] Draft 0.1.0 release notes prepared ([RELEASE-NOTES.md](RELEASE-NOTES.md), 2026-10-01; no publication)

## Release blockers found during documentation audit

- **B1 — resolved: relocatable addon configuration.** The absolute addon
  config destination escaped `--prefix=` (reproduced under a disposable
  DESTDIR guard on 2026-09-30). Addon config and optional Lua extension now
  use relative GNUInstallDirs destinations. `install-check.py` asserts all
  manifest paths and required files for DESTDIR and direct `--prefix=`;
  package-check runs this regression. No live installation is touched.
- **B2 — resolved for an unreleased tree.** Removed the premature
  `0.1.0`/2026-08-23 AppStream release claim. Tag-time version/date
  synchronization remains an explicit publication step.
- **B3 — resolved: historical phase material archived.** Foundation specs
  retain their original content under an archive notice; current steering,
  build/data guidance and agent policy point to the current release gates.

## Items tracked separately

Documentation must not duplicate or preempt active parallel efforts, so
these are named here without detail:

- **CI hardening** — the static-analysis, check-sanitizer, fuzz-smoke and
  package-check jobs landed on main in fd32b32 (2026-08-27); further
  tightening (workflow permissions are already `contents: read`) continues
  as its own workstream.
- **Desktop breadth and distro distribution** — X11/GTK/GNOME, other
  browsers, an automated desktop harness, `debian/` packaging and Fedora
  repository submission are optional later work, not 0.1.0 blockers.

## Making a release

Nothing below is done by preparation work; each step needs the maintainer's
explicit authorization.

1. Empty every unchecked box above (with evidence) or consciously
   disposition it in a release-notes paragraph.
2. Tag-time metadata, together in one commit: add
   `<releases><release version="0.1.0" date="YYYY-MM-DD"/></releases>` to
   `org.fcitx.Fcitx5.Addon.Oxpinyin.metainfo.xml.in` (after `<project_group>`),
   keep `project(... VERSION 0.1.0)`, retitle `RELEASE-NOTES.md` from "draft,
   unreleased" and date it, and add a dated `%changelog` entry for the same
   version in `packaging/fedora/fcitx5-oxpinyin.spec`. Rerun `install-check.py`,
   `artifact-check.py` (AppStream validates the release entry) and
   `i18n-check.py source`.
3. Run the full gate matrix on that release commit: both compilers (baseline,
   Cloud, Lua), clang-format, ASan+UBSan, clang-tidy, fuzz smoke, commit lint,
   same-binary substitution gate, install/package checks and the
   Fedora-native `mock` build with `validate-rpm.py`. Rerun the manual KDE
   acceptance only if the release commit changes the packaged payload.
4. Tag `v0.1.0` (the spec's `Source0` expects that tag archive), publish
   sources + notes, and only then update distro/packaging claims anywhere
   they appear.
