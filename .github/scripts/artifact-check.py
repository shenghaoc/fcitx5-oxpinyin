#!/usr/bin/env python3
"""Inspect generated artifacts; generation success alone is insufficient."""
import configparser
import json
import re
import shutil
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import xml.etree.ElementTree as ET

def dependency_names(text):
    """Exact capability names from `Depends:`/`rpm --requires` text.

    Alternatives split on `|`; version constraints and `()(64bit)` qualifiers
    are dropped, so `libpinyin.so.15()(64bit)` never satisfies `libpinyin` and
    `fcitx5-module-punctuation` never satisfies `fcitx5`.
    """
    names = set()
    for entry in re.split(r'[,\n]', text):
        for alternative in entry.split('|'):
            alternative = alternative.strip()
            if alternative:
                names.add(re.split(r'[\s(<>=]', alternative, maxsplit=1)[0])
    return names


build = Path(sys.argv[1]).resolve()
required = ['fcitx5/oxpinyin.so', 'fcitx5/addon/oxpinyin.conf',
            'fcitx5/inputmethod/oxpinyin.conf',
            'metainfo/org.fcitx.Fcitx5.Addon.Oxpinyin.metainfo.xml']
records = []
def check_tree(root):
    files = [p for p in root.rglob('*') if p.is_file()]
    for suffix in required:
        matches = [p for p in files if str(p).endswith('/' + suffix)]
        assert len(matches) == 1, (suffix, matches)
    def conf(suffix):
        p = next(p for p in files if str(p).endswith(suffix))
        c = configparser.ConfigParser(interpolation=None, strict=False)
        c.read(p)
        return c
    addon = conf('/addon/oxpinyin.conf')
    deps = list(addon['Addon/Dependencies'].values())
    assert any(x.startswith('core:') for x in deps) and 'punctuation' in deps
    assert addon['Addon']['library'] == 'oxpinyin'
    assert conf('/inputmethod/oxpinyin.conf')['InputMethod']['addon'] == 'oxpinyin'
    meta = next(p for p in files if '/metainfo/' in str(p))
    assert ET.parse(meta).getroot().findtext('id') == 'org.fcitx.Fcitx5.Addon.Oxpinyin'
    subprocess.run(['appstreamcli', 'validate', '--no-net', str(meta)], check=True)
    return [str(p.relative_to(root)) for p in files]
for generator, suffix in [('TXZ', '.tar.xz'), ('RPM', '.rpm'), ('DEB', '.deb')]:
    subprocess.run(['cpack', '-G', generator], cwd=build, check=True)
    artifacts = list(build.glob('fcitx5-oxpinyin-*' + suffix))
    assert len(artifacts) == 1, artifacts
    artifact = artifacts[0]
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        dependencies = ''
        if generator == 'TXZ':
            with tarfile.open(artifact) as archive:
                archive.extractall(root, filter='data')
        elif generator == 'DEB':
            subprocess.run(['dpkg-deb', '-x', str(artifact), str(root)], check=True)
            dependencies = subprocess.check_output(['dpkg-deb', '-f', str(artifact), 'Depends'], text=True)
        else:
            dependencies = subprocess.check_output(['rpm', '-qp', '--requires', str(artifact)], text=True)
            # Read the complete archive first: cpio may stop at its trailer
            # before rpm2cpio has written padding, causing a pipe SIGPIPE.
            payload = subprocess.check_output(['rpm2cpio', str(artifact)])
            subprocess.run(['cpio', '-id', '--quiet'], input=payload, cwd=root, check=True)
        if dependencies:
            required_deps = ['fcitx5', 'fcitx5-module-punctuation', 'libpinyin15', 'libpinyin-data'] if generator == 'DEB' else ['fcitx5', 'fcitx5-chinese-addons', 'libpinyin']
            names = dependency_names(dependencies)
            for dependency in required_deps:
                assert dependency in names, (generator, dependency, sorted(names))
            assert re.search(r'(^|[,\n])\s*fcitx5\s*\(?>=\s*5\.1\.13', dependencies), \
                (generator, 'versioned fcitx5 requirement', dependencies)
            for optional in ['cloudpinyin', 'fcitx5-lua', 'quickphrase']:
                assert not any(optional in name for name in names), (generator, optional)
        if generator == 'RPM':
            prefix = root / 'usr'
            env = dict(__import__('os').environ)
            with tempfile.TemporaryDirectory() as user:
                env['OXPINYIN_USER_DATA_DIR'] = user
                probe = build / 'test/package-runtime'
                for mode in ['working', 'no-punctuation']:
                    subprocess.run([str(probe), str(prefix), mode], env=env, check=True)
                env['OXPINYIN_SYSTEM_DATA_DIR'] = user
                subprocess.run([str(probe), str(prefix), 'no-data'], env=env, check=True)
        records.append({'generator': generator, 'artifact': str(artifact),
                        'dependencies': dependencies, 'files': check_tree(root)})
(build / 'artifact-evidence.json').write_text(json.dumps(records, indent=2) + '\n')
print('RPM/DEB/TXZ contents, dependencies and metadata: PASS')

# Prepare separately attributed TestFrontend instrumentation for a fresh
# runtime container. This is never shipped in the package payload.
cache = (build / 'CMakeCache.txt').read_text().splitlines()
libdir = next(x.split('=', 1)[1] for x in cache if x.startswith('CMAKE_INSTALL_LIBDIR:'))
for module in ['testfrontend', 'testim']:
    for relative in [f'{libdir}/fcitx5/lib{module}.so', f'share/fcitx5/testing/addon/{module}.conf']:
        destination = build / 'package-harness/usr' / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(Path('/usr') / relative, destination)
