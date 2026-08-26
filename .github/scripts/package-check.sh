#!/bin/bash
# Package / install validation: stage a full DESTDIR install and assert the
# pieces a distro package (or CPack archive) must actually ship, then build a
# quick TXZ via cpack and check the addon lands inside it. Catches install-
# rule breakage that a build+ctest run never notices.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

rm -rf build-pkg
# Distro-style configuration (/usr prefix): fcitx's absolute install
# variables (FCITX_INSTALL_PKGDATADIR etc.) and GNUInstallDirs agree, so a
# DESTDIR staging tree mirrors what a real package ships.
cmake -B build-pkg -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DOXPINYIN_USER_DATA_DIR="$(mktemp -d)"
cmake --build build-pkg

STAGE="$(mktemp -d)/stage"
DESTDIR="$STAGE" cmake --install build-pkg

fail() {
    echo "PACKAGE CHECK FAILED: $1" >&2
    exit 1
}

test -f "$STAGE/usr/lib/fcitx5/oxpinyin.so" ||
    fail "addon module missing from lib/fcitx5"
test -f "$STAGE/usr/share/fcitx5/inputmethod/oxpinyin.conf" ||
    fail "inputmethod config missing"
test -f "$STAGE/usr/share/fcitx5/addon/oxpinyin.conf" ||
    fail "addon config missing"
test -f \
    "$STAGE/usr/share/metainfo/org.fcitx.Fcitx5.Addon.Oxpinyin.metainfo.xml" ||
    fail "appstream metainfo missing"

# Translations ship only when po/LINGUAS actually lists languages (it is
# empty until translations arrive); once it does, a compiled catalog is part
# of the package contract.
if grep -Ev '^[[:space:]]*(#|$)' "$ROOT/po/LINGUAS" | grep . > /dev/null; then
    if ! find "$STAGE/usr/share/locale" -name 'fcitx5-oxpinyin*.mo' |
        grep . > /dev/null; then
        fail "po/LINGUAS has languages but no compiled catalog installed"
    fi
fi

echo "--- staged files:"
(cd "$STAGE" && find usr -type f | sort)

# Archive generator smoke: cpack assembles and contains the addon module.
# (The TXZ generator relativizes install destinations — no usr/ prefix and a
# <name>-<version>-Linux/ toplevel — hence the path-suffix match.)
cd build-pkg
cpack -G TXZ >/dev/null
ARCHIVE="$(ls fcitx5-oxpinyin-*.tar.xz | head -n 1)"
# NOTE: no `grep -q` here — under pipefail, -q's early exit turns tar's
# SIGPIPE into a spurious failure even when the path IS present.
if ! tar -tf "$ARCHIVE" | grep '/lib/fcitx5/oxpinyin\.so$' > /dev/null; then
    fail "archive does not contain the addon module"
fi
echo "--- archive: $ARCHIVE OK"
