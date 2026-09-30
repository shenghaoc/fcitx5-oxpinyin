# Packaging validation and boundaries

CPack outputs are local validation artifacts, not published distro packages.
Run `.github/scripts/install-check.py BUILD` and
`.github/scripts/artifact-check.py BUILD` after a `/usr`-prefix build.
The latter requires rpm-build/rpm2cpio, cpio, dpkg-deb and appstreamcli; it
extracts RPM/DEB/TXZ, checks all required files, validates addon/inputmethod
relationships and hard/optional dependencies, and runs AppStream validation.
Evidence is saved in the build directory, never committed.

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
Full `.spec` and `debian/` policy integration
remain separate from CPack content validation. Do not publish these artifacts
until those target-distro checks are complete.

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
