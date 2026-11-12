# 0.1.1 release notes — 2026-11-12

A packaging and release-automation release. No engine or input behaviour
changes: nothing under `src/` or `po/` differs from 0.1.0, so the addon is
built from the same sources. The installed addon configuration and AppStream
metainfo differ only in the version they declare.

Packages depend on stock libpinyin (`libpinyin.so.15`, pkg-config `libpinyin`),
not on oxpinyin. oxpinyin is an optional install-time substitute exporting
libpinyin's SONAME.

See [RELEASE.md](RELEASE.md) for the declared scope, and the
[0.1.0 release](https://github.com/shenghaoc/fcitx5-oxpinyin/releases/tag/v0.1.0)
for what the frontend does.

## Changes since 0.1.0

- **Debian testing DEB and TXZ release assets.** The GitHub Release now also
  carries `fcitx5-oxpinyin-0.1.1-debian-testing-amd64.deb` and
  `fcitx5-oxpinyin-0.1.1-debian-testing-x86_64.tar.xz`, next to the Fedora 44
  RPM and SRPM, with one `SHA256SUMS` over all four. They are built and
  installed on Debian testing only; nothing is claimed for Debian stable,
  Ubuntu or any other release, and no distribution repository entry exists.
- **License files in the CPack packages.** The DEB ships a DEP-5
  `/usr/share/doc/fcitx5-oxpinyin/copyright`; the TXZ ships the full `LICENSE`
  at `share/doc/fcitx5-oxpinyin/LICENSE`; the CPack RPM (a validation
  artifact, not published) ships it as `%license`. A plain `cmake --install`
  and the Fedora-native RPM are unchanged.
- **Debian packaging metadata.** `Section: utils` instead of CPack's default
  `devel`, an extended description, a `changelog.gz`, a stripped addon, and
  immediate binding (`-Wl,-z,now`) in the Debian build. The DEB passes
  `lintian` with no error or warning on Debian testing, and pull requests now
  fail on one.
- **Simpler DEB dependencies.** The DEB no longer names `libpinyin-data`:
  `libpinyin15` already depends on its exact data version.
- **Automated release publication.** Pushing a `vX.Y.Z` tag builds and
  validates every asset, creates the release as a draft, uploads, downloads
  everything back, verifies the exact asset set and every hash, and only then
  publishes. A published release is never modified without an explicit
  `replace` dispatch, and a dry run builds and validates without writing.
- **Smoke-test assertion fix.** The disposable-install smoke test now fails
  when `ldd` reports an unresolved library; the check was previously printed
  and ignored.

Declared desktop scope is unchanged: Fedora 44 KDE Plasma Wayland. X11, GTK,
GNOME, Kubuntu/Debian desktops, other browsers, exhaustive input
schemes/backends/models, an automated desktop harness and Debian-policy
packaging are unvalidated. The Debian assets are validated by a headless
install in a container, not on a Debian desktop. No general platform support
is inferred from the tested scope.
