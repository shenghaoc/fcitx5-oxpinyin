#!/usr/bin/env bash
# The one Debian recipe: build the addon, test it, generate the CPack packages,
# inspect them and lint the DEB. artifacts.yml runs it on every pull request and
# release.yml runs it to build the Debian release assets, so both exercise the
# same dependencies and flags.
#
#   docker run --rm -v "$PWD:/work" -w /work debian:testing \
#       bash .github/scripts/debian-build.sh build-debian
#
# It installs packages, so it runs only inside a disposable container.
set -euo pipefail
[[ -f /run/.containerenv || -f /.dockerenv ]] || {
    echo 'Refusing to install build dependencies outside a container' >&2; exit 2;
}
BUILD=${1:?Usage: debian-build.sh build-directory}
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends g++ cmake make git \
    extra-cmake-modules libfcitx5core-dev libfcitx5utils-dev \
    fcitx5-modules-dev fcitx5-module-punctuation-dev libpinyin15-dev \
    fcitx5-module-chttrans fcitx5-module-fullwidth \
    gettext python3 dpkg-dev rpm cpio appstream file lintian

# SHLIBDEPS adds the library dependencies dpkg-shlibdeps finds. -Wl,-z,now is
# given here, not in CMakeLists.txt, so that no other build's link flags change
# (lintian: hardening-no-bindnow).
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DCPACK_DEBIAN_PACKAGE_SHLIBDEPS=ON \
    -DCMAKE_MODULE_LINKER_FLAGS=-Wl,-z,now
cmake --build "$BUILD" -j2
ctest --test-dir "$BUILD" --output-on-failure
# Runs cpack for TXZ, RPM and DEB and inspects each payload.
python3 "$ROOT/.github/scripts/artifact-check.py" "$BUILD"

# Any lintian error or warning fails; informational tags are only listed.
lintian --allow-root --fail-on error,warning --info --display-info --pedantic \
    "$BUILD"/fcitx5-oxpinyin-*.deb | tee "$BUILD/lintian.txt"
echo 'lintian: no error or warning tags'
