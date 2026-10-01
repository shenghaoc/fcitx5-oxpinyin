# Packaging validation and boundaries

Four distinct things are described here; do not conflate them:

1. **CMake install** — `cmake --install` (DESTDIR or `--prefix=`), the base
   every package is built from. Checked by `install-check.py`.
2. **CPack RPM/DEB/TXZ** — convenience/validation packages generated from the
   install rules (the sections below). Supported and validated, never
   published.
3. **Fedora-native `.spec`** — `packaging/fedora/fcitx5-oxpinyin.spec`, see
   [Fedora-native packaging](#fedora-native-packaging). Reference material for
   upstream; it packages the addon only.
4. **Fedora repository submission** — **has not happened.** No Fedora or COPR
   package exists, none has been reviewed, and nothing here claims official
   Fedora availability.

CPack and the spec are independent: the spec does not replace or alter CPack.

CPack outputs are local validation artifacts, not published distro packages.
Run `.github/scripts/install-check.py BUILD` and
`.github/scripts/artifact-check.py BUILD` after a `/usr`-prefix build.
The latter requires rpm-build/rpm2cpio, cpio, dpkg-deb and appstreamcli; it
extracts RPM/DEB/TXZ, checks all required files, validates addon/inputmethod
relationships and hard/optional dependencies, runs AppStream validation and
`.github/scripts/i18n-check.py payload` on every extracted payload.
Evidence is saved in the build directory, never committed.

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
packaging/fedora/validate-rpm.py out/fcitx5-oxpinyin-0.1.0-*.x86_64.rpm \
    out/*.src.rpm
```

`validate-rpm.py` (needs rpm, rpmlint with `glibc-langpack-en`, binutils,
appstreamcli) checks rpmlint (distribution configuration, zero findings),
the exact file manifest, root ownership and modes, license tag and source SPDX
identifiers, dependencies, absence of RPATH/RUNPATH, no build-tree paths in the
payload, AppStream validation and the translation payload. The installed RPM
is then exercised in a disposable Fedora 44 install root with the existing
TestFrontend smoke:
`OXPINYIN_RPM="$(ls out/fcitx5-oxpinyin-0.1.0-*.x86_64.rpm)" bash .github/scripts/minimal-runtime.sh BUILD`
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
The DEB declaration now uses `libpinyin15`/`libpinyin-data` and the standalone
`fcitx5-module-punctuation`, avoiding the broad chinese-addons metapackage.
Reference: [Debian libpinyin15](https://packages.debian.org/en/libpinyin15)
and [Chinese addon packages](https://packages.debian.org/search?keywords=fcitx5-chinese-addons&searchon=sourcenames).
A Fedora-built DEB is only an inspected payload. Native Debian validation
builds with `-DCPACK_DEBIAN_PACKAGE_SHLIBDEPS=ON` and installs that DEB in a
fresh Debian container. The automated artifact workflow exercises native
Fedora RPM and Debian DEB dependency resolution, installed-addon discovery,
composition/commit, absent punctuation and missing model data. Fedora also
checks a missing engine SONAME. Explicitly copied TestFrontend/testim modules
are harness instrumentation, not dependencies shipped by the addon.

On 2026-10-01 these checks passed locally on Fedora 44 and Debian unstable,
including AppStream validation and all five baseline tests in both native
build environments. Package-root/container changes never touch the live IM.
A Fedora-native `.spec` now exists (above); `debian/` policy packaging remains
a later project, separate from CPack content validation. Do not publish these
artifacts without explicit maintainer authorization.

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
