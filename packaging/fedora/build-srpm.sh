#!/bin/bash
# Build the source tarball (from the committed tree) and SRPM for the Fedora
# spec. A release tag does not exist yet, so Source0 is produced locally with
# `git archive` using the same top-level directory a GitHub tag archive has.
#
#   packaging/fedora/build-srpm.sh [OUTDIR]     (default: build-srpm/)
#
# OUTDIR must be new, empty, or a previous output of this script (it carries a
# marker file); anything else is refused rather than cleaned.
#
# Needs: git, rpm-build (plus rpmautospec-rpm-macros). Uncommitted changes are
# NOT included; the tarball is the exact tree of HEAD.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(realpath -m "${1:-$ROOT/build-srpm}")"
SPEC="$ROOT/packaging/fedora/fcitx5-oxpinyin.spec"
VERSION="$(rpmspec -q --qf '%{version}\n' --srpm "$SPEC" | head -n1)"

if [[ -n "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ]]; then
    echo "warning: working tree has uncommitted changes; they are not packaged" >&2
fi
# Only ever clean the rpmbuild subdirectories of a directory this script made
# (marked) or an empty/new one; never recursively delete a caller-supplied path.
MARK="$OUT/.oxpinyin-srpm-build"
if [[ -e "$OUT" ]]; then
    [[ -d "$OUT" ]] || { echo "error: $OUT exists and is not a directory" >&2; exit 1; }
    if [[ -n "$(ls -A "$OUT")" && ! -f "$MARK" ]]; then
        echo "error: refusing to reuse non-empty $OUT that this script did not create" >&2
        exit 1
    fi
fi
mkdir -p "$OUT"
for sub in SOURCES SPECS SRPMS RPMS BUILD BUILDROOT; do
    rm -rf "${OUT:?}/$sub"
    mkdir -p "$OUT/$sub"
done
touch "$MARK"
git -C "$ROOT" archive --format=tar.gz \
    --prefix="fcitx5-oxpinyin-$VERSION/" \
    -o "$OUT/SOURCES/fcitx5-oxpinyin-$VERSION.tar.gz" HEAD
cp "$SPEC" "$OUT/SPECS/"
rpmbuild -bs --define "_topdir $OUT" "$OUT/SPECS/fcitx5-oxpinyin.spec"
echo "source commit: $(git -C "$ROOT" rev-parse HEAD)"
ls -1 "$OUT"/SRPMS
