#!/usr/bin/env bash
# Run only inside a disposable rootful/container environment, never the host.
set -euo pipefail
BUILD=${1:?Usage: minimal-runtime.sh build-directory}
[[ -f /run/.containerenv || -f /.dockerenv ]] || {
    echo 'Refusing package-root changes outside a container' >&2; exit 2;
}
BUILD=$(realpath "$BUILD")
ROOT=${2:-$(mktemp -d /tmp/oxpinyin-runtime.XXXXXX)}
[[ $ROOT == /tmp/oxpinyin-runtime.* && -d $ROOT ]] || exit 2
# OXPINYIN_RPM selects a specific package (e.g. the Fedora-native RPM built by
# packaging/fedora); by default the CPack RPM in the build directory is used.
RPM=${OXPINYIN_RPM:-$(find "$BUILD" -maxdepth 1 -name 'fcitx5-oxpinyin-*.rpm' -print -quit)}
[[ -n $RPM && -f $RPM ]] || {
    echo "error: no RPM found (set OXPINYIN_RPM or build the package)" >&2
    exit 1
}
dnf --use-host-config --installroot="$ROOT" --releasever=44 --setopt=install_weak_deps=False \
    install -y "$RPM" bash coreutils
rpm --root "$ROOT" -qa | sort > "$BUILD/minimal-runtime-packages.txt"
# Explicit test instrumentation, not dependencies shipped by the addon.
mkdir -p "$ROOT/usr/lib64/fcitx5" "$ROOT/usr/share/fcitx5/addon"
for module in testfrontend testim; do
    cp "/usr/lib64/fcitx5/lib$module.so" "$ROOT/usr/lib64/fcitx5/"
    cp "/usr/share/fcitx5/testing/addon/$module.conf" "$ROOT/usr/share/fcitx5/addon/"
done
cp "$BUILD/test/package-runtime" "$ROOT/usr/bin/oxpinyin-package-probe"
chroot "$ROOT" /usr/bin/ldd /usr/lib64/fcitx5/oxpinyin.so | tee "$BUILD/minimal-runtime-ldd.txt"
! grep 'not found' "$BUILD/minimal-runtime-ldd.txt"
mkdir -p "$ROOT/tmp/user" "$ROOT/tmp/empty"
for mode in working no-punctuation; do
    chroot "$ROOT" /usr/bin/env OXPINYIN_USER_DATA_DIR=/tmp/user \
        /usr/bin/oxpinyin-package-probe /usr "$mode"
done
chroot "$ROOT" /usr/bin/env OXPINYIN_USER_DATA_DIR=/tmp/user \
    OXPINYIN_SYSTEM_DATA_DIR=/tmp/empty \
    /usr/bin/oxpinyin-package-probe /usr no-data
# Prove the SONAME is required, modifying only the disposable package root.
mv "$ROOT/usr/lib64/libpinyin.so.15" "$ROOT/tmp/libpinyin.disabled"
chroot "$ROOT" /usr/bin/env OXPINYIN_USER_DATA_DIR=/tmp/user \
    /usr/bin/oxpinyin-package-probe /usr no-engine
mv "$ROOT/tmp/libpinyin.disabled" "$ROOT/usr/lib64/libpinyin.so.15"
printf '%s\n' 'Fedora minimal runtime dependency/discovery/commit/failure gates: PASS' \
    | tee "$BUILD/minimal-runtime-result.txt"
