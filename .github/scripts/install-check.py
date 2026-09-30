#!/usr/bin/env python3
"""Verify both install staging forms and every installed path, without live writes."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

build = Path(sys.argv[1]).resolve()
cache = (build / 'CMakeCache.txt').read_text().splitlines()
def variable(name):
    return next(line.split('=', 1)[1] for line in cache if line.startswith(name + ':'))
libdir = variable('CMAKE_INSTALL_LIBDIR')
# GNUInstallDirs leaves DATADIR empty in cache when it inherits DATAROOTDIR.
datadir = variable('CMAKE_INSTALL_DATADIR') or variable('CMAKE_INSTALL_DATAROOTDIR')
assert not Path(libdir).is_absolute() and not Path(datadir).is_absolute()
required = [f'{libdir}/fcitx5/oxpinyin.so', f'{datadir}/fcitx5/addon/oxpinyin.conf',
            f'{datadir}/fcitx5/inputmethod/oxpinyin.conf',
            f'{datadir}/metainfo/org.fcitx.Fcitx5.Addon.Oxpinyin.metainfo.xml']
with tempfile.TemporaryDirectory() as temp:
    guard = Path(temp)
    for mode in ['destdir', 'prefix']:
        # DESTDIR guard protects against a regression to absolute destinations,
        # including the --prefix case, without allowing writes into /usr.
        prefix = '/usr' if mode == 'destdir' else '/relocated'
        env = os.environ | {'DESTDIR': str(guard / mode)}
        subprocess.run(['cmake', '--install', str(build), '--prefix', prefix], env=env, check=True)
        stage = guard / mode / prefix.lstrip('/')
        paths = (build / 'install_manifest.txt').read_text().splitlines()
        for path in paths:
            full = guard / mode / path.lstrip('/')
            assert full.is_relative_to(stage), f'escaped {mode} prefix: {path}'
            assert full.is_file(), full
        for path in required:
            assert (stage / path).is_file(), (mode, path)
        print(json.dumps({'mode': mode, 'required': required, 'all_paths': paths}))
    # Also exercise literal --prefix with no DESTDIR after guarded checks pass.
    stage = guard / 'direct'
    env = os.environ.copy()
    env.pop('DESTDIR', None)
    subprocess.run(['cmake', '--install', str(build), '--prefix', str(stage)], env=env, check=True)
    for path in (build / 'install_manifest.txt').read_text().splitlines():
        assert Path(path).is_relative_to(stage), path
    for path in required:
        assert (stage / path).is_file(), path
print('DESTDIR and direct --prefix staging: PASS')
