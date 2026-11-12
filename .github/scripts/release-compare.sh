#!/usr/bin/env bash
# Describe how rebuilt release assets differ from the ones they would replace.
#
#   release-compare.sh OLD_DIR NEW_DIR
#
# For the RPM, SRPM, DEB and TXZ in each directory it prints a diff of the
# package identity, the dependencies and the payload (file list with per-file
# SHA-256). Purely informational: rebuilds are not byte-reproducible, so a
# difference never fails. Only a tool error does.
set -euo pipefail
old="$(realpath "${1:?Usage: release-compare.sh OLD_DIR NEW_DIR}")"
new="$(realpath "${2:?Usage: release-compare.sh OLD_DIR NEW_DIR}")"
for tool in rpm dpkg-deb tar xz sha256sum diff; do
    command -v "$tool" > /dev/null || { echo "error: $tool is required" >&2; exit 1; }
done
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# The single asset of one kind in a directory, or nothing.
pick() {
    local dir=$1 kind=$2 file found=()
    for file in "$dir"/*; do
        [[ -f $file ]] || continue
        case "$kind:$file" in
            srpm:*.src.rpm | deb:*.deb | txz:*.tar.xz) found+=("$file") ;;
            rpm:*.src.rpm) ;;
            rpm:*.rpm) found+=("$file") ;;
        esac
    done
    if (( ${#found[@]} > 1 )); then
        echo "error: more than one $kind asset in $dir" >&2; exit 1
    fi
    printf '%s' "${found[0]:-}"
}

payload_digests() {
    (cd "$1" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 -r sha256sum)
}

describe() {
    local kind=$1 file=$2 scratch=$3
    mkdir -p "$scratch"
    case "$kind" in
        rpm | srpm)
            echo '## package'
            rpm -qp --qf '%{NAME} %{VERSION}-%{RELEASE} %{ARCH}\n' "$file"
            echo '## requires'
            rpm -qpR "$file" | LC_ALL=C sort
            echo '## files (path, mode, digest)'
            rpm -qp --qf '[%{FILENAMES} %{FILEMODES:perms} %{FILEDIGESTS}\n]' "$file" | LC_ALL=C sort
            ;;
        deb)
            echo '## control'
            dpkg-deb -f "$file" Package Version Architecture Section Depends
            echo '## files (digest, path)'
            dpkg-deb -x "$file" "$scratch"
            payload_digests "$scratch"
            ;;
        txz)
            echo '## files (digest, path)'
            tar -xJf "$file" -C "$scratch"
            payload_digests "$scratch"
            ;;
    esac
}

for kind in rpm srpm deb txz; do
    before="$(pick "$old" "$kind")"
    after="$(pick "$new" "$kind")"
    echo "=== $kind"
    if [[ -z $before && -z $after ]]; then
        echo 'in neither set'
    elif [[ -z $before ]]; then
        echo "only in the new set: $(basename "$after")"
    elif [[ -z $after ]]; then
        echo "only in the old set: $(basename "$before")"
    else
        echo "old: $(basename "$before")  $(sha256sum < "$before" | cut -d' ' -f1)"
        echo "new: $(basename "$after")  $(sha256sum < "$after" | cut -d' ' -f1)"
        describe "$kind" "$before" "$work/old-$kind" > "$work/old-$kind.txt" 2> /dev/null
        describe "$kind" "$after" "$work/new-$kind" > "$work/new-$kind.txt" 2> /dev/null
        if diff -u --label "old/$(basename "$before")" --label "new/$(basename "$after")" \
            "$work/old-$kind.txt" "$work/new-$kind.txt"; then
            echo 'identity, dependencies and payload digests are identical'
        fi
    fi
done
