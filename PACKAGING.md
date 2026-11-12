# Packaging validation and boundaries

Four distinct things are described here; do not conflate them:

1. **CMake install** — `cmake --install` (DESTDIR or `--prefix=`), the base
   every package is built from. Checked by `install-check.py`.
2. **CPack RPM/DEB/TXZ** — convenience/validation packages generated from the
   install rules (the sections below). Supported and validated. The DEB and
   TXZ built on Debian testing are release assets starting with 0.1.1; the
   CPack RPM is never published.
3. **Fedora-native `.spec`** — `packaging/fedora/fcitx5-oxpinyin.spec`, see
   [Fedora-native packaging](#fedora-native-packaging). Reference material for
   upstream; it packages the addon only.
4. **Fedora repository submission** — **has not happened.** No Fedora or COPR
   package exists, none has been reviewed, and nothing here claims official
   Fedora availability.

CPack and the spec are independent: the spec does not replace or alter CPack.

## What a GitHub Release carries

`.github/workflows/release.yml` builds, validates and attaches, per tag:

- `fcitx5-oxpinyin-<ver>-<rel>.fc44.x86_64.rpm` and `.src.rpm`: the Fedora-native
  spec, built in a Fedora 44 container from GitHub's archive of the tag;
- starting with 0.1.1, `fcitx5-oxpinyin-<ver>-debian-testing-amd64.deb` and
  `fcitx5-oxpinyin-<ver>-debian-testing-x86_64.tar.xz`: the CPack DEB and TXZ,
  built in a Debian testing container by `.github/scripts/debian-build.sh`
  (the recipe `artifacts.yml` runs on every pull request), then installed in
  a fresh Debian testing container. The TXZ unpacks to
  `fcitx5-oxpinyin-<ver>-Linux/`, CPack's own name;
- one `SHA256SUMS` over all of them.

v0.1.0 carries only the RPM and SRPM: its tree predates the DEB license and
changelog fixes, and it is the only tag the workflow releases without Debian
assets. Any other tree must contain `packaging/debian/changelog` and
`.github/scripts/debian-build.sh`, or the run fails before building.

These are upstream prebuilt packages. No distribution repository entry
exists, and nothing is claimed for Debian stable, Ubuntu or any other
release: the DEB depends on the library versions of Debian testing at build
time. It depends on stock `libpinyin15`, never on oxpinyin.

## Validating locally

Run `.github/scripts/install-check.py BUILD` and
`.github/scripts/artifact-check.py BUILD` after a `/usr`-prefix build.
The latter requires rpm-build/rpm2cpio, cpio, dpkg-deb and appstreamcli; it
extracts RPM/DEB/TXZ, checks all required files, validates addon/inputmethod
relationships and hard/optional dependencies, runs AppStream validation and
`.github/scripts/i18n-check.py payload` on every extracted payload.
Evidence is saved in the build directory, never committed.

## Licensing per format

No `install()` rule ships a license file, so a plain `cmake --install` has
none; `install-check.py` fails if one appears. Each package adds its own:

- **CPack DEB**: `/usr/share/doc/fcitx5-oxpinyin/copyright`, the DEP-5 file
  `packaging/debian/copyright` (GPL-3.0-or-later; CC0-1.0 for the metainfo),
  which refers to `/usr/share/common-licenses` instead of carrying the texts.
- **CPack TXZ**: the full `LICENSE` at `share/doc/fcitx5-oxpinyin/LICENSE`.
- **CPack RPM**: the full `LICENSE` at
  `/usr/share/licenses/fcitx5-oxpinyin/LICENSE`, tagged `%license`.
- **Fedora-native RPM**: the spec's `%license LICENSE`, independent of CPack.

The CPack files are staged by `packaging/cpack/license-files.cmake`, which
runs through `CPACK_INSTALL_SCRIPTS`: only CPack reads that variable, so the
files exist in CPack's staging tree alone. `artifact-check.py` fails if a
package lacks its file, carries another format's, or the DEB `Section` is not
`utils`.

## CPack DEB contents

Beyond the license, the DEB is shaped to pass `lintian` without errors or
warnings; `debian-build.sh` runs `lintian --fail-on error,warning`, so both
pull requests and releases fail on one:

- The addon is stripped (`CPACK_STRIP_FILES`, which applies to every CPack
  package and to CPack's staging install only).
- The control file has an extended description
  (`CPACK_DEBIAN_PACKAGE_DESCRIPTION`).
- `/usr/share/doc/fcitx5-oxpinyin/changelog.gz` is `packaging/debian/changelog`,
  staged by the same CPack-only script. **Add an entry there for every
  release**: `artifact-check.py` fails unless the newest entry is the packaged
  version, the addon is stripped and the extended description is non-empty.
- The Debian build links with `-Wl,-z,now`, passed on the command line by
  `debian-build.sh` (lintian's `hardening-no-bindnow`). It is not set in
  `CMakeLists.txt`, so other builds keep their distribution's link flags.
  The pull-request check and the release build share that script, so the
  published DEB is configured exactly as the validated one.

## Translations

Catalogs are exactly the languages in `po/LINGUAS` (`zh_CN`, `zh_TW`); the
build fails if `po/*.po` and `LINGUAS` disagree. Each is compiled with
`msgfmt --check` and installed as
`<localedir>/<lang>/LC_MESSAGES/fcitx5-oxpinyin.mo` under the relocatable
GNUInstallDirs locale directory (`share/locale`). Fcitx's own
`fcitx5_install_translation()` is deliberately not used: it installs to an
absolute path that escapes `cmake --install --prefix=`. The addon manifest
`Comment[...]` and AppStream `<name>`/`<summary>` translations are generated
from the same catalogs. `i18n-check.py payload` verifies, for the DESTDIR,
direct-prefix, RPM, DEB and TXZ trees, that the `.mo` files sit in that
hierarchy (the configured `CMAKE_INSTALL_LOCALEDIR`), load, cover every template message and translate each message exactly as its source `.po` says.
The addon registers the compiled-in `FCITX_INSTALL_LOCALEDIR` at runtime, as
other Fcitx addons do, so a catalog is found at the configure-time prefix;
a payload moved with `--prefix=` after configuration is installed
relocatably but not re-pointed.

## Fedora-native packaging

`packaging/fedora/fcitx5-oxpinyin.spec` is a conventional Fedora spec
(`%autosetup`, `%cmake`/`%cmake_build`, `%cmake_install`, `%find_lang`,
`%check` running ctest and `appstreamcli validate`). Scope boundary: it
packages **only the addon** and builds against Fedora's ordinary
`libpinyin-devel`. The oxpinyin engine, and any Provides/Obsoletes/Conflicts
strategy for replacing `libpinyin.so.15`, are owned by oxpinyin/downstream
packaging, not this repository.

Requirements were audited against Fedora 44 package names:

- Build: `cmake`, `gcc-c++`, `gettext`, `extra-cmake-modules`, `appstream`,
  `cmake(Fcitx5Core|Utils|ModuleSpell|ModuleTestFrontend|ModulePunctuation)`
  (the last from `fcitx5-chinese-addons-devel`), `pkgconfig(libpinyin)`; for
  `%check` additionally `fcitx5`, `fcitx5-chinese-addons`, `libpinyin-data`.
- Runtime: `fcitx5 >= 5.1.13`, `fcitx5-chinese-addons` (the only Fedora
  provider of the dlopen()ed punctuation module; Fedora ships no smaller
  subpackage for it, so its Qt WebEngine and Lua dependencies come along
  transitively), and the automatic
  `libpinyin.so.15` soname dependency, which brings `libpinyin-data`. Spell,
  Cloud Pinyin and fcitx5-lua are **not** required (Cloud/Lua are build
  options left OFF). Translations are in the payload via `%find_lang`.
- License tag `GPL-3.0-or-later AND CC0-1.0`: every source carries
  GPL-3.0-or-later; the installed metainfo declares CC0-1.0.

Build and inspect (a release tag does not exist, so the tarball is produced
from the committed tree with `git archive`):

```sh
packaging/fedora/build-srpm.sh build-srpm            # tarball + SRPM
mock -r fedora-44-x86_64 --rebuild build-srpm/SRPMS/*.src.rpm --resultdir out
packaging/fedora/validate-rpm.py out/fcitx5-oxpinyin-0.1.1-*.x86_64.rpm \
    out/*.src.rpm
```

`validate-rpm.py` (needs rpm, rpmlint with `glibc-langpack-en`, binutils,
appstreamcli) checks rpmlint (distribution configuration, zero findings),
the exact file manifest, root ownership and modes, license tag and source SPDX
identifiers, dependencies, absence of RPATH/RUNPATH, no build-tree paths in the
payload, AppStream validation and the translation payload. The installed RPM
is then exercised in a disposable Fedora 44 install root with the existing
TestFrontend smoke:
`OXPINYIN_RPM="$(ls out/fcitx5-oxpinyin-0.1.1-*.x86_64.rpm)" bash .github/scripts/minimal-runtime.sh BUILD`
(the CI `fedora-native` job does all of this with `dnf builddep` +
`rpmbuild --rebuild`; the local reference is the clean `mock` build). This
does not replace real KDE acceptance.

Raw `rpmlint` without the distribution configuration reports, and the
classification is: `spelling-error` for the proper nouns *libpinyin* and
*addon* (false positives); `no-signature`, `no-packager-tag`, `no-group-tag`,
`no-buildroot-tag` (Fedora builds sign packages and forbids/ignores those
tags); and `invalid-license` for valid SPDX identifiers that the default
config's license list lacks (the Fedora SPDX list accepts both). None needs a
spec change; none is filtered in the repository.

## Runtime and distro requirements

The addon requires Fcitx5 core, the chinese-addons punctuation module, an
ABI-compatible `libpinyin.so.15`, and complete matching engine system data.
Spell, conversion, Cloud and Lua features do not turn into hard dependencies
in the baseline package. Headless failure-path tests separately cover absent
punctuation, absent Spell and missing engine data; these do not by themselves
prove a clean package-manager installation resolves every dependency.

CPack's package-name declarations must be mapped to the target distro.
Fedora's `libpinyin` name is not Debian's versioned runtime library name.
The DEB declaration uses `libpinyin15` (which itself depends on its exact
`libpinyin-data` version, so the data package is not named) and the standalone
`fcitx5-module-punctuation`, avoiding the broad chinese-addons metapackage.
Reference: [Debian libpinyin15](https://packages.debian.org/en/libpinyin15)
and [Chinese addon packages](https://packages.debian.org/search?keywords=fcitx5-chinese-addons&searchon=sourcenames).
A Fedora-built DEB is only an inspected payload. Native Debian validation
runs on Debian testing: it builds with `-DCPACK_DEBIAN_PACKAGE_SHLIBDEPS=ON`
and installs that DEB in a fresh Debian testing container. The automated
artifact workflow exercises native
Fedora RPM and Debian DEB dependency resolution, installed-addon discovery,
composition/commit, absent punctuation and missing model data. Fedora also
checks a missing engine SONAME. Explicitly copied TestFrontend/testim modules
are harness instrumentation, not dependencies shipped by the addon.

On 2026-10-01 these checks passed locally on Fedora 44 and Debian unstable,
including AppStream validation and all five baseline tests in both native
build environments. Package-root/container changes never touch the live IM.
A Fedora-native `.spec` now exists (above); `debian/` policy packaging remains
a later project, separate from CPack content validation. Packages are
published only by the release workflow, which the maintainer starts by pushing
a tag; do not publish artifacts from any other build.

## Oxpinyin replacement data contract

The engine's supported `tools/packaging/install.sh` installs the ABI library,
headers and complete pkg-config metadata; it does not install a usable model.
An oxpinyin package replacing libpinyin must also install/export backend-
compatible system tables at **`<libpinyin pkgdatadir>/data`**, with matching
`table.conf` and backend metadata. For the pinned Tkrzw build this is the
libpinyin-compatible exported `.bin`/`.db` layout, not an unrelated developer
cache. Read the installed `libpinyin.pc`; upstream's data install uses
`$(libdir)/libpinyin/data`, which can differ with distro multilib policy.

The package must arrange model dependencies, ABI/SONAME provider conflicts
and per-user writable data independently of this adapter. Consumers other
than OXPinyin do not know `OXPINYIN_SYSTEM_DATA_DIR`; an adapter environment
override therefore cannot establish that replacement packaging is correct.
Model provenance, checksum and backend must agree with the packaged library.
