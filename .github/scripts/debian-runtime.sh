#!/usr/bin/env bash
# Invoke in a fresh Debian container, not the build container or live host.
set -euo pipefail
[[ -f /run/.containerenv || -f /.dockerenv ]] || exit 2
BUILD=$(realpath "${1:?build directory required}")
DEB=$(find "$BUILD" -maxdepth 1 -name 'fcitx5-oxpinyin-*.deb' -print -quit)
apt-get update
apt-get install -y --no-install-recommends "$DEB"
dpkg-query -W > "$BUILD/debian-runtime-packages.txt"
# Explicit harness instrumentation prepared by the native build environment.
cp -a "$BUILD/package-harness/usr/." /usr/
mkdir -p /tmp/oxpinyin-user /tmp/oxpinyin-empty
for mode in working no-punctuation; do
    env OXPINYIN_USER_DATA_DIR=/tmp/oxpinyin-user \
        "$BUILD/test/package-runtime" /usr "$mode"
done
env OXPINYIN_USER_DATA_DIR=/tmp/oxpinyin-user \
    OXPINYIN_SYSTEM_DATA_DIR=/tmp/oxpinyin-empty \
    "$BUILD/test/package-runtime" /usr no-data
printf '%s\n' 'Native Debian runtime dependency/discovery/commit gates: PASS' \
    | tee "$BUILD/debian-runtime-result.txt"
