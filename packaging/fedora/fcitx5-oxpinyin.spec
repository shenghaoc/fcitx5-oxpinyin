# Fedora-native packaging for fcitx5-oxpinyin, kept upstream as reference
# material. This is NOT a Fedora package submission: nothing here implies the
# package has been reviewed, accepted or built in any Fedora repository.
#
# Scope: this spec packages ONLY the fcitx5 addon (the C++ frontend). It
# builds against Fedora's ordinary libpinyin development package. The
# oxpinyin engine, and any Provides/Obsoletes/Conflicts strategy for
# replacing libpinyin.so.15, belong to oxpinyin / downstream packaging.
#
# Local build from a git checkout (until a release tag exists):
#   packaging/fedora/build-srpm.sh        # source tarball + SRPM
#   packaging/fedora/validate-rpm.sh ...  # rpmlint + payload inspection

# The addon is dlopen()ed by fcitx5; it is not a library other packages link.
%global __provides_exclude_from ^%{_libdir}/fcitx5/.*\\.so$

Name:           fcitx5-oxpinyin
Version:        0.1.0
# Explicit release/changelog keep this in-tree reference spec reproducible
# outside dist-git; a Fedora submission would use %%autorelease/%%autochangelog.
Release:        1%{?dist}
Summary:        Pinyin input method for Fcitx 5 using the libpinyin C ABI
# GPL-3.0-or-later: all sources, translations and manifests (SPDX headers).
# CC0-1.0: the installed AppStream metainfo declares <metadata_license>CC0-1.0.
License:        GPL-3.0-or-later AND CC0-1.0
URL:            https://github.com/shenghaoc/fcitx5-oxpinyin
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.21
BuildRequires:  gcc-c++
BuildRequires:  gettext
BuildRequires:  extra-cmake-modules
BuildRequires:  appstream
BuildRequires:  cmake(Fcitx5Core) >= 5.1.13
BuildRequires:  cmake(Fcitx5Utils)
BuildRequires:  cmake(Fcitx5ModuleSpell)
BuildRequires:  cmake(Fcitx5ModuleTestFrontend)
# punctuation_public.h: punctuation is delegated to chinese-addons' module.
BuildRequires:  cmake(Fcitx5ModulePunctuation)
BuildRequires:  pkgconfig(libpinyin)
# %%check runs the headless TestFrontend suite, which loads the real fcitx5
# TestFrontend/Spell modules, chinese-addons' punctuation/chttrans/fullwidth
# modules and the libpinyin model data (pulled in by libpinyin itself).
BuildRequires:  fcitx5 >= 5.1.13
BuildRequires:  fcitx5-chinese-addons
BuildRequires:  libpinyin-data

# The addon needs the fcitx5 daemon (manifest: core >= 5.1.13). Shared
# libraries (fcitx5-libs, libpinyin) come from the automatic soname
# dependencies, and Fedora's libpinyin already requires libpinyin-data.
Requires:       fcitx5 >= 5.1.13
# [Addon/Dependencies] punctuation: the module ships in the main
# fcitx5-chinese-addons package. It is dlopen()ed, so rpm cannot see it.
Requires:       fcitx5-chinese-addons%{?_isa}
# Deliberately NOT required: Spell dictionaries, Cloud Pinyin and fcitx5-lua.
# Spell, chttrans and fullwidth are optional runtime integrations (the addon
# degrades without them); this build leaves ENABLE_CLOUDPINYIN and ENABLE_LUA
# at their default OFF, so neither module is referenced by the package.

%description
fcitx5-oxpinyin is a thin Fcitx 5 input-method addon for full pinyin, double
pinyin and Zhuyin. It talks to its engine only through the libpinyin C ABI
(libpinyin.so.15, pinyin.h), so the engine is whatever libpinyin-compatible
library is installed; this package ships no engine and no language model.

%prep
%autosetup

%build
%cmake
%cmake_build

%install
%cmake_install
%find_lang %{name}

%check
# Per-user engine state must not touch the build user's real home.
export OXPINYIN_USER_DATA_DIR="$(mktemp -d)"
%ctest
appstreamcli validate --no-net %{buildroot}%{_metainfodir}/org.fcitx.Fcitx5.Addon.Oxpinyin.metainfo.xml

%files -f %{name}.lang
%license LICENSE
%doc README.md RELEASE-NOTES.md
%{_libdir}/fcitx5/oxpinyin.so
%{_datadir}/fcitx5/addon/oxpinyin.conf
%{_datadir}/fcitx5/inputmethod/oxpinyin.conf
%{_metainfodir}/org.fcitx.Fcitx5.Addon.Oxpinyin.metainfo.xml

%changelog
* Thu Oct 01 2026 Shenghao Chen <shenghaoc@outlook.com> - 0.1.0-1
- Reference spec for the unreleased 0.1.0 tree (not a Fedora submission)
