#!/usr/bin/env python3
"""Run one built adapter under two implementations; never rebuild between runs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--build', required=True, type=Path)
p.add_argument('--oracle', required=True, type=Path)
p.add_argument('--engine', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parent
build, oracle, engine, output = (x.resolve() for x in (a.build, a.oracle, a.engine, a.output))
output.mkdir(parents=True, exist_ok=True)
addon = build / 'src/oxpinyin.so'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
addon_hash = digest(addon)
env = os.environ.copy()
env['PKG_CONFIG_PATH'] = str(oracle / 'lib/pkgconfig')
flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'libpinyin'], env=env, text=True).split()
subprocess.run(['cc', '-shared', '-fPIC', str(root / 'loader-audit.c'), '-o', str(output / 'audit.so')], check=True)
subprocess.run(['cc', str(root / 'candidates.c'), '-o', str(output / 'candidates'), *flags], check=True)
tests = json.loads(subprocess.check_output(['ctest', '--test-dir', str(build), '--show-only=json-v1'], text=True))['tests']
if not tests:
    raise SystemExit('engine substitution requires configured headless tests')
inputs = ['nihao', 'zhongguo', 'beijing', 'shanghai', 'woaini', 'woaizhongguo', "xi'an", 'niha', 'shi']
failures = []
def capture(command, run_env):
    try:
        result = subprocess.run(command, env=run_env, cwd=build / 'test',
                                capture_output=True, check=False, timeout=180)
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired as error:
        return 'timeout', error.stdout or b'', error.stderr or b''

identity = {'addon': str(addon), 'addon_sha256': addon_hash, 'runs': {}}
for name, prefix in [('libpinyin', oracle), ('oxpinyin', engine)]:
    library = (prefix / 'lib/libpinyin.so.15').resolve(strict=True)
    data = prefix / 'lib/libpinyin/data'
    identity['runs'][name] = {'library': str(library), 'sha256': digest(library)}
    for test in tests:
        with tempfile.TemporaryDirectory() as user:
            run_env = env.copy()
            for prop in test.get('properties', []):
                if prop['name'] == 'ENVIRONMENT':
                    for item in prop['value']:
                        key, value = item.split('=', 1)
                        run_env[key] = value
            run_env.update(LD_LIBRARY_PATH=str(prefix / 'lib'), LD_AUDIT=str(output / 'audit.so'),
                           OXPINYIN_SYSTEM_DATA_DIR=str(data), OXPINYIN_USER_DATA_DIR=user)
            code, stdout, stderr = capture(test['command'], run_env)
            log = (stdout + stderr).decode('utf-8', errors='replace')
            (output / f'{name}-{test["name"]}.log').write_bytes(stdout + stderr)
            mapped = [Path(line.split(' ', 1)[1]).resolve() for line in log.splitlines() if line.startswith('SUBSTITUTION_MAP ')]
            # Missing punctuation intentionally prevents loading the addon/engine.
            expected_loaded = test['name'] != 'testoxpinyin-punctabsent'
            if code or (expected_loaded and (library not in mapped or addon.resolve() not in mapped)) or (not expected_loaded and mapped):
                failures.append(f'{name}/{test["name"]}: rc={code}, maps={mapped}')
            print(name, test['name'], 'PASS' if not code else 'FAIL', flush=True)
    records = []
    for text in inputs:
        with tempfile.TemporaryDirectory() as user:
            run_env = env | {'LD_LIBRARY_PATH': str(prefix / 'lib'), 'LD_AUDIT': str(output / 'audit.so')}
            code, stdout, stderr = capture([str(output / 'candidates'), str(data), user, text], run_env)
            index = len(records)
            (output / f'{name}-candidate-{index}.stderr').write_bytes(stderr)
            records.append(stdout)
            if code or str(prefix / 'lib/libpinyin.so.15') not in stderr.decode('utf-8', errors='replace'):
                failures.append(f'{name}/candidates/{text}: rc={code}, loader identity absent or capture failed')
    (output / f'{name}-candidates.txt').write_bytes(b''.join(records))
assert digest(addon) == addon_hash, 'addon changed between runs'
(output / 'identity.json').write_text(json.dumps(identity, indent=2) + '\n')
if (output / 'libpinyin-candidates.txt').read_bytes() != (output / 'oxpinyin-candidates.txt').read_bytes():
    failures.append('candidate differential: inspect complete retained ordering; do not normalize')
(output / 'failures.txt').write_text('\n'.join(failures) + '\n')
if failures:
    raise SystemExit('\n'.join(failures))
