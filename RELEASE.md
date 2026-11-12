# Release status & checklist

This file tracks two things: where the project honestly stands on the road
to its first public release, and what has to be true before one happens.
Status statements here are evidence-based — checked against the source tree,
the test suite, and real runs — and dated so staleness is detectable.

## Current release status — 0.1.0 released on 2026-11-12

**0.1.0 is released on 2026-11-12 for the declared scope.**

Every gate below passed on 2026-10-01 on the then top-of-stack tree, and the
maintainer completed the package-installed Fedora KDE acceptance on 2026-10-04
(Fedora-native RPM, [TESTING.md](TESTING.md)). Relative to that accepted tree,
the release commit changes only documentation and adds the AppStream
`<releases>` entry to the metainfo; the addon binary, configuration files and
catalogs are built from unchanged sources. Maintainer disposition: the release
entry is catalog metadata that fcitx5 does not read at runtime, so the KDE
acceptance was not repeated for the release payload.

Release metadata: **2026-11-12**, version `0.1.0` (from `project()`).
The gate results below are historical; the release commit's gate matrix is recorded on pull request #30.

| Area                    | Status                                                                                                                                                                                                                             |
| ----------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Frontend implementation | The full feature set described in the [README](README.md) — schemes (full/double pinyin/Zhuyin), candidate flow with constrained selection, preedit/aux text, prediction, English candidates, punctuation delegation, s2t/full-width toggle surfacing, optional Cloud Pinyin and lua candidates, status-bar toggles — is implemented and covered by the headless suite |
| Automated testing       | Final gate matrix, 2026-10-01, clean export of the top-of-stack commit in Fedora 44 containers with warnings-as-errors: GCC and Clang Release baseline 5/5; Cloud 7/7 and Lua 6/6 on both compilers; Clang ASan+UBSan 5/5; fuzz-enabled suite 6/6; clang-tidy 0 findings; clang-format clean; commit lint and linter self-test 31/31. CI additionally runs the pinned same-binary substitution gate on every pull-request update and by manual dispatch (it does not run on direct pushes to `main`). See [TESTING.md](TESTING.md) |
| Engine compatibility    | **Frontend scope complete.** The addon uses only the libpinyin-compatible C ABI. The reproducible same-binary `libpinyin.so.15` substitution gate (`tools/engine-substitution/`) runs one unchanged addon binary under pinned libpinyin 2.11.92 and Rust oxpinyin (Tkrzw/model20) and compares bounded captures as integration evidence. Behavioural parity with libpinyin is **not** claimed and is not a frontend release criterion; engine defects belong in the oxpinyin repository. See [TESTING.md](TESTING.md) |
| Desktop integration     | **Declared 0.1.0 scope accepted.** Fedora 44 KDE Plasma Wayland, Qt/KDE applications, basic Chrome input and clean Plasma/Fcitx restart validated by manual acceptance on 2026-09-30 using verified addon/engine binaries. Fedora 44 KDE Wayland acceptance was repeated on 2026-10-04 with the Fedora-native RPM installed. No automated desktop harness exists. Other environments are unvalidated (listed below), not broken. See [TESTING.md](TESTING.md) |
| Packaging               | **Validated CPack artifacts and a Fedora-native `.spec`.** CPack RPM/DEB/TXZ contents, dependency resolution and installed-addon TestFrontend validation (Fedora 44 RPM, Debian unstable DEB) as of 2026-10-01. `packaging/fedora/fcitx5-oxpinyin.spec` clean-builds in `mock` for Fedora 44 (tests, AppStream and rpmlint clean, disposable install + headless smoke). It is reference material: **no Fedora/COPR submission has happened and no official availability is claimed.** A prebuilt Fedora 44 RPM and SRPM (built upstream; no distribution repository entry exists) are attached to the GitHub Release; starting with 0.1.1 the release workflow also attaches a Debian testing DEB and TXZ (v0.1.0 carries only the RPM and SRPM; nothing is claimed for Debian stable, Ubuntu or other releases). Debian-policy `debian/` packaging is a later project. See [PACKAGING.md](PACKAGING.md) |
| Internationalization    | Initial `zh_CN` and `zh_TW` catalogs ship through the project gettext pipeline; `.mo` placement, loading and manifest/metainfo translations are verified for DESTDIR, direct-prefix, RPM, DEB and TXZ payloads. Other locales are untranslated English |

The release tree keeps `project()` at `0.1.0` and has a matching AppStream
release entry and dated [RELEASE-NOTES.md](RELEASE-NOTES.md).

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
the package-installed Fedora KDE acceptance and final hardening are done. The maintainer authorized the 0.1.0 tag and GitHub Release on 2026-11-12; a prebuilt Fedora 44 RPM and SRPM (built upstream; no distribution repository entry exists) are attached to the GitHub Release. v0.1.0 carries only those; starting with 0.1.1 the release workflow also attaches a Debian testing DEB and TXZ.

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
2. Version metadata, together in one commit, all naming the same `X.Y.Z`:
   - `project(... VERSION X.Y.Z)` in `CMakeLists.txt`;
   - `Version:` and a dated `%changelog` entry in
     `packaging/fedora/fcitx5-oxpinyin.spec`;
   - a new `<release version="X.Y.Z" date="YYYY-MM-DD"/>` first in `<releases>`
     in `org.fcitx.Fcitx5.Addon.Oxpinyin.metainfo.xml.in`;
   - the title and date of `RELEASE-NOTES.md`, which becomes the release body;
   - a new first entry in `packaging/debian/changelog`.

   Rerun `install-check.py`, `artifact-check.py` (AppStream validates the
   release entry; the DEB check fails unless the changelog's newest entry is
   the packaged version) and `i18n-check.py source`. Merge it.
3. Run the full gate matrix on that release commit: both compilers (baseline,
   Cloud, Lua), clang-format, ASan+UBSan, clang-tidy, fuzz smoke, commit lint,
   same-binary substitution gate, install/package checks and the
   Fedora-native `mock` build with `validate-rpm.py`. Rerun the manual KDE
   acceptance only if the release commit changes the packaged payload.
4. Rehearse on the merged commit before any tag exists: dispatch
   `.github/workflows/release.yml` with `dry_run: true` and `ref: main`. It
   runs every build and validation step of a release and never starts the job
   that can write to one.
5. Push the `vX.Y.Z` tag (the spec's `Source0` expects that tag archive).
   Pushing the tag is the publication step; the workflow does the rest:
   - checks that the tag, `project()`, the spec and the newest
     `packaging/debian/changelog` entry name the same version;
   - builds the Fedora 44 RPM and SRPM from GitHub's archive of the tag, and
     runs `validate-rpm.py`, the stock-libpinyin dependency check and the
     disposable-install smoke;
   - builds the Debian testing DEB and TXZ with `debian-build.sh` (tests,
     `artifact-check.py`, lintian with no error or warning), checks the DEB's
     version and stock-`libpinyin15` dependency, and installs it in a fresh
     Debian testing container;
   - writes one `SHA256SUMS` over the four assets, creates the GitHub Release
     from `RELEASE-NOTES.md` **as a draft**, uploads everything, downloads it
     back, verifies the exact asset set and every hash, and only then
     publishes. On any failure the draft stays unpublished and the run fails;
     a new run empties and reuses that draft.

   Only then update distro/packaging claims anywhere they appear.

### Rebuilding the assets of an existing tag

Dispatch the workflow with a `tag` input. A release that is already published
is refused unless `replace: true` is also set. Replacing saves the current
assets as the run artifact `previous-release-assets`, logs a content
comparison (file lists, per-file digests, dependencies; rebuilds are not
byte-reproducible, so differences are reported, not failed on), uploads over
them and verifies as above. There is no automatic rollback: if it fails
part-way, restore with

```sh
gh run download <run-id> -n previous-release-assets -D previous
rm previous/previous-assets.json
gh release upload <tag> previous/* --clobber
```

`dry_run: true` with a `tag` shows what a real run would do, including that
comparison, without writing. A tag whose tree has no
`packaging/debian/changelog` (v0.1.0) gets Fedora assets only.
