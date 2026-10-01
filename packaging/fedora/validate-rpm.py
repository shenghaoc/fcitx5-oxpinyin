#!/usr/bin/env python3
"""Inspect a Fedora-built fcitx5-oxpinyin binary RPM (and optionally its SRPM).

    validate-rpm.py BINARY.rpm [SOURCE.src.rpm]

Run in a Fedora environment with rpm, rpmlint, binutils and appstreamcli (for
example the same container used for `mock`). Nothing is installed or written
outside a temporary extraction directory. Checks:

  rpmlint (distro configuration) clean; installed-file manifest; ownership and
  permissions; license tag and source SPDX headers; dependencies; RPATH/RUNPATH;
  no build-tree paths in the payload; AppStream validation; translation payload.

These checks do not make the package a Fedora package: nothing here has been
submitted to, reviewed by or built in any Fedora repository.
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
LICENSE_TAG = 'GPL-3.0-or-later AND CC0-1.0'
EXPECTED_FILES = {
    '/usr/lib64/fcitx5/oxpinyin.so': '0755',
    '/usr/share/fcitx5/addon/oxpinyin.conf': '0644',
    '/usr/share/fcitx5/inputmethod/oxpinyin.conf': '0644',
    '/usr/share/metainfo/org.fcitx.Fcitx5.Addon.Oxpinyin.metainfo.xml': '0644',
    '/usr/share/locale/zh_CN/LC_MESSAGES/fcitx5-oxpinyin.mo': '0644',
    '/usr/share/locale/zh_TW/LC_MESSAGES/fcitx5-oxpinyin.mo': '0644',
    '/usr/share/licenses/fcitx5-oxpinyin/LICENSE': '0644',
}
# Documentation and the build-id symlink farm are expected extras.
ALLOWED_PREFIXES = ('/usr/share/doc/fcitx5-oxpinyin', '/usr/lib/.build-id',
                    '/usr/share/licenses/fcitx5-oxpinyin/')
OPTIONAL_DEP_WORDS = ('lua', 'cloudpinyin', 'spell', 'aspell', 'enchant', 'curl')


def run(*cmd, **kw):
    return subprocess.run(cmd, check=True, text=True, capture_output=True, **kw).stdout


def rpm_query(rpm, fmt):
    return run('rpm', '-qp', '--qf', fmt, str(rpm))


def main(binary, srpm=None):
    binary = Path(binary).resolve()
    results = []

    def ok(msg):
        results.append(msg)
        print('PASS', msg)

    # 1. rpmlint with the distribution configuration: nothing may remain.
    # rpmlint forces LC_ALL=en_US.UTF-8 for its rpm subprocesses; without that
    # locale installed (glibc-langpack-en) it reports a spurious setlocale
    # warning, so the environment must provide it rather than this check hide it.
    lint = subprocess.run(['rpmlint', str(binary)] + ([str(srpm)] if srpm else []),
                          text=True, capture_output=True)
    findings = [l for l in lint.stdout.splitlines() if re.search(r': [EW]: ', l)]
    assert lint.returncode == 0 and not findings, lint.stdout + lint.stderr
    ok('rpmlint (Fedora configuration): 0 errors, 0 warnings')

    # 2. Manifest: exactly the expected files, no owned directories, no extras.
    listing = rpm_query(binary, r'[%{FILEMODES:perms}|%{FILEUSERNAME}|%{FILEGROUPNAME}|%{FILENAMES}\n]')
    entries = [l.split('|', 3) for l in listing.splitlines()]
    seen = {}
    for perms, user, group, name in entries:
        assert user == 'root' and group == 'root', (name, user, group)
        if perms.startswith('d'):
            # Only the package's own doc dir and the build-id tree (owned by
            # every package that ships build-ids) may appear as directories.
            assert name.startswith(ALLOWED_PREFIXES) or name == '/usr/share/licenses/fcitx5-oxpinyin', \
                f'package owns a shared directory: {name}'
            continue
        if name.startswith(ALLOWED_PREFIXES) and name not in EXPECTED_FILES:
            continue
        seen[name] = perms
    def octal(perms):
        """-rwxr-xr-x -> 0755 (setuid/sticky bits are rejected separately)."""
        return '0' + ''.join(
            str(4 * (t[0] == 'r') + 2 * (t[1] == 'w') + (t[2] in 'xs'))
            for t in (perms[1:4], perms[4:7], perms[7:10]))
    assert set(seen) == set(EXPECTED_FILES), (set(seen) ^ set(EXPECTED_FILES))
    for name, perms in seen.items():
        assert octal(perms) == EXPECTED_FILES[name], (name, perms)
        assert 's' not in perms and 't' not in perms and perms[8] != 'w', (name, perms)
    ok(f'manifest: {len(seen)} files, all root:root, expected modes, no owned shared directories')

    # 3. License tag and source SPDX headers.
    tag = rpm_query(binary, '%{LICENSE}')
    assert tag == LICENSE_TAG, tag
    ids = set()
    for f in run('git', '-C', str(REPO), 'ls-files').splitlines():
        p = REPO / f
        if p.suffix in ('.cpp', '.h', '.in', '.lua', '.sh', '.py', '.txt', '.po', '.pot'):
            ids.update(re.findall(r'SPDX-License-Identifier:\s*([A-Za-z0-9.+-]+)', p.read_text(errors='ignore')))
    assert ids <= {'GPL-3.0-or-later'}, ids
    assert (REPO / 'LICENSE').read_text().lstrip().startswith('GNU GENERAL PUBLIC LICENSE')
    ok(f'license: tag "{tag}"; source SPDX identifiers {sorted(ids)}; LICENSE shipped')

    # 4. Dependencies.
    requires = rpm_query(binary, '[%{REQUIRENAME} %{REQUIREFLAGS:depflags} %{REQUIREVERSION}\n]').splitlines()
    names = {r.split()[0] for r in requires}
    for needed in ('fcitx5', 'fcitx5-chinese-addons(x86-64)', 'libpinyin.so.15()(64bit)',
                   'libFcitx5Core.so.7()(64bit)'):
        assert needed in names, (needed, sorted(names))
    assert any(r.startswith('fcitx5 ') and '5.1.13' in r for r in requires), requires
    # No optional integration may be hard-required; check package names only.
    for n in names:
        if not n.startswith(('lib', 'rpmlib', 'rtld')):
            assert not any(w in n for w in OPTIONAL_DEP_WORDS), n
    ok('dependencies: fcitx5 >= 5.1.13, fcitx5-chinese-addons, sonames; no Spell/Cloud/Lua packages')

    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        payload = subprocess.run(['rpm2cpio', str(binary)], check=True, capture_output=True).stdout
        subprocess.run(['cpio', '-id', '--quiet'], input=payload, cwd=root, check=True)
        so = root / 'usr/lib64/fcitx5/oxpinyin.so'

        # 5. RPATH / RUNPATH.
        dynamic = run('readelf', '-d', str(so))
        assert not re.search(r'\((RPATH|RUNPATH)\)', dynamic), dynamic
        ok('addon has no RPATH/RUNPATH')

        # 6. No build-tree paths anywhere in the payload.
        bad = re.compile(rb'/builddir|BUILDROOT|redhat-linux-build|/home/|/tmp/')
        for p in root.rglob('*'):
            if p.is_file() and not p.is_symlink() and '.build-id' not in p.parts:
                hit = bad.search(p.read_bytes())
                assert not hit, (p.relative_to(root), hit.group())
        ok('no build-tree or user paths in any payload file')

        # 7. AppStream.
        meta = root / 'usr/share/metainfo/org.fcitx.Fcitx5.Addon.Oxpinyin.metainfo.xml'
        run('appstreamcli', 'validate', '--no-net', str(meta))
        ok('AppStream metainfo validates')

        # 8. Translation payload (loaded catalogs, manifest and metainfo).
        run(sys.executable, str(REPO / '.github/scripts/i18n-check.py'), 'payload', str(root))
        ok('translation payload: zh_CN/zh_TW catalogs load and cover the template')

    if srpm:
        files = run('rpm', '-qpl', str(srpm)).split()
        assert 'fcitx5-oxpinyin.spec' in files and any(f.endswith('.tar.gz') for f in files), files
        ok(f'SRPM contents: {files}')

    print(f'{len(results)} checks passed')


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    main(*sys.argv[1:])
