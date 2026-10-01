#!/usr/bin/env bash
# Shell-side helper for the maintainer-authorized, package-installed Fedora KDE
# acceptance run (see TESTING.md, "Package-installed acceptance"). The human
# performs every GUI step; this script only prepares, verifies and rolls back.
#
# It NEVER runs privileged commands (it prints the exact sudo commands for the
# maintainer), never kills or restarts Fcitx5, never uses LD_LIBRARY_PATH or
# development-prefix search paths, and keeps evidence outside the repository.
#
#   packaged-kde.sh record-rpm RPM SOURCE_COMMIT [SHA256]   record NEVRA/sha256/commit
#   packaged-kde.sh backup                         back up Fcitx config + data
#   packaged-kde.sh preflight                      read-only environment audit
#   packaged-kde.sh shadow-aside                   move dev descriptors aside
#   packaged-kde.sh install-cmds                   print sudo install commands
#   packaged-kde.sh log-on                         raise oxpinyin logging at runtime
#   packaged-kde.sh verify TAG                     prove the daemon runs the package
#   packaged-kde.sh rollback-cmds                  restore user files, print sudo undo
#
# Recovery if the desktop has no usable keyboard: Ctrl+Alt+F3, log in, then
# `pkill fcitx5`.
set -euo pipefail
umask 077   # evidence and backups may contain personal data

DIR=${OXPINYIN_ACCEPTANCE_DIR:-$HOME/oxpinyin-packaged-acceptance}
USER_ADDON=$HOME/.local/share/fcitx5/addon/oxpinyin.conf
USER_IM=$HOME/.local/share/fcitx5/inputmethod/oxpinyin.conf
PKG_ADDON=/usr/lib64/fcitx5/oxpinyin.so
ENGINE=/usr/lib64/libpinyin.so.15.0.0
# The engine this acceptance is documented against (Fedora's libpinyin, which
# the package builds against). Override only for a deliberate engine upgrade.
EXPECTED_ENGINE=${OXPINYIN_EXPECTED_ENGINE:-libpinyin-2.11.91-2.fc44.x86_64}
CTL=(busctl --user call org.fcitx.Fcitx5 /controller org.fcitx.Fcitx.Controller1)

die() { echo "ERROR: $*" >&2; exit 1; }
latest() { ls -1d "$DIR/$1"/* 2>/dev/null | sort | tail -n 1; }

fcitx_pid() {
    local sig pid
    read -r sig pid < <(busctl --user call org.freedesktop.DBus /org/freedesktop/DBus \
        org.freedesktop.DBus GetConnectionUnixProcessID s org.fcitx.Fcitx5) ||
        die 'Fcitx5 is not on the session bus'
    [[ $sig == u && $pid =~ ^[0-9]+$ ]] || die 'unexpected D-Bus reply'
    echo "$pid"
}

# Succeeds only if the daemon maps PATH from the very inode now on disk (a
# replaced or reinstalled file shows as a different inode or "(deleted)").
mapped_is_installed() {
    local pid=$1 path=$2 want have
    want=$(stat -c %i "$path")
    have=$(awk -v f="$path" '$6 == f && $7 != "(deleted)" {print $5}' "/proc/$pid/maps" | sort -u)
    [[ -n $have && $have == "$want" ]]
}

cmd_record_rpm() {
    local rpm=${1:?RPM path} commit=${2:?source commit} want=${3:-}
    [[ $commit =~ ^[0-9a-f]{40}$ ]] || die 'SOURCE_COMMIT must be a full 40-hex SHA'
    if [[ -n $want ]]; then
        [[ $(sha256sum "$rpm" | cut -d' ' -f1) == "$want" ]] ||
            die "RPM sha256 differs from the documented artifact $want"
    fi
    mkdir -p "$DIR/rpm"
    cp -p "$rpm" "$DIR/rpm/"
    local copy=$DIR/rpm/$(basename "$rpm")
    {
        echo "recorded: $(date -u +%FT%TZ)"
        echo "source commit: $commit"
        echo "NEVRA: $(rpm -qp --qf '%{NEVRA}' "$copy")"
        echo "license: $(rpm -qp --qf '%{LICENSE}' "$copy")"
        sha256sum "$copy"
        echo "payload files:"
        rpm -qp --qf '[%{FILEDIGESTS} %{FILEMODES:perms} %{FILENAMES}\n]' "$copy" |
            grep -v '/usr/lib/.build-id'
    } | tee "$DIR/rpm/MANIFEST.txt"
}

cmd_backup() {
    local stamp dest
    stamp=$(date +%Y%m%d-%H%M%S)
    dest=$DIR/backup/$stamp
    mkdir -p "$dest"
    chmod 700 "$DIR" "$DIR/backup" "$dest"
    cp -a "$HOME/.config/fcitx5" "$dest/config-fcitx5"
    cp -a "$HOME/.local/share/fcitx5" "$dest/share-fcitx5"
    (cd "$dest" && find config-fcitx5 share-fcitx5 -type f -print0 | sort -z | xargs -0 sha256sum) > "$dest/SHA256SUMS"
    rpm -qa | grep -E '^(fcitx5|libpinyin|kwrite|konsole)' | sort > "$dest/packages.txt"
    cat > "$dest/RESTORE.txt" <<EOT
Backup $stamp of ~/.config/fcitx5 and ~/.local/share/fcitx5.
Verify the backup:  (cd $dest && sha256sum --check --quiet SHA256SUMS)
Full restore (log out of the graphical session first, or stop Fcitx5). Files
created after the backup (learned state, new config) must NOT survive, so the
current directories are moved aside instead of being overlaid:
  mv ~/.config/fcitx5 ~/.config/fcitx5.after-acceptance
  mv ~/.local/share/fcitx5 ~/.local/share/fcitx5.after-acceptance
  cp -a $dest/config-fcitx5 ~/.config/fcitx5
  cp -a $dest/share-fcitx5 ~/.local/share/fcitx5
EOT
    echo "backup: $dest ($(wc -l < "$dest/SHA256SUMS") files)"
}

cmd_preflight() {
    local pid problems=0
    pid=$(fcitx_pid)
    echo "Fcitx5 PID $pid, started $(ps -o lstart= -p "$pid")"
    echo "exe: $(readlink "/proc/$pid/exe")"
    echo "known-good input methods in profile:"
    grep -E '^Name=' "$HOME/.config/fcitx5/profile" | sed 's/^/  /'
    echo "mapped addon/engine in the RUNNING daemon:"
    awk '/oxpinyin\.so|libpinyin\.so\.15/ {print "  " $6}' "/proc/$pid/maps" | sort -u
    if tr '\0' '\n' < "/proc/$pid/environ" | grep -E '^(LD_LIBRARY_PATH|FCITX_ADDON_DIRS|OXPINYIN_)'; then
        echo "WARN: daemon environment carries development overrides"; problems=1
    fi
    for f in "$USER_ADDON" "$USER_IM"; do
        if [[ -e $f ]]; then
            echo "SHADOW: $f exists (overrides /usr/share/fcitx5)"; problems=1
            grep -H -E '^Library=' "$f" 2>/dev/null || true
        fi
    done
    echo "packages: $(rpm -q fcitx5 fcitx5-chinese-addons libpinyin libpinyin-data fcitx5-oxpinyin 2>&1 | tr '\n' ' ')"
    ((problems)) && echo 'ACTION NEEDED: run shadow-aside (after backup) before the package can be accepted.'
    return 0
}

cmd_shadow_aside() {
    local dest
    dest=$(latest backup) || true
    [[ -n $dest && -d $dest ]] || die 'run backup first'
    mkdir -p "$dest/shadowed"
    for f in "$USER_ADDON" "$USER_IM"; do
        if [[ -e $f ]]; then
            mv -v "$f" "$dest/shadowed/$(basename "$(dirname "$f")")-$(basename "$f")"
        fi
    done
    echo "descriptors moved to $dest/shadowed (rollback-cmds restores them)"
}

cmd_install_cmds() {
    local rpm
    rpm=$(ls -1 "$DIR"/rpm/fcitx5-oxpinyin-[0-9]*.x86_64.rpm | tail -n 1)
    cat <<EOF
# Run these yourself (sudo needs your credentials). Currently working
# input methods stay available: keyboard-us and chinese-addons pinyin.
sha256sum -c <(grep -E '\.rpm\$' $DIR/rpm/MANIFEST.txt)
sudo dnf install $rpm
rpm -q fcitx5-oxpinyin && rpm -V fcitx5-oxpinyin && echo 'rpm -V clean'
# Then: gate A (optional) restart only Fcitx5, test nihao in KWrite;
# gate B: full logout/login, then run: $0 verify post-login
EOF
}

cmd_log_on() {
    "${CTL[@]}" SetLogRule s 'default=4,oxpinyin=5'
    echo 'runtime log rule set (reverts when Fcitx5 restarts)'
}

cmd_verify() {
    local tag=${1:?TAG}
    [[ $tag =~ ^[a-zA-Z0-9_-]+$ ]] || die 'bad TAG'
    local out=$DIR/evidence/$tag pid started installed
    mkdir -p "$out"
    pid=$(fcitx_pid)
    {
        date -u +%FT%TZ
        echo "Fcitx5 PID: $pid"
        echo "started: $(ps -o lstart= -p "$pid")"
        readlink "/proc/$pid/exe"
        echo "-- current IM / addon for oxpinyin / group"
        "${CTL[@]}" CurrentInputMethod
        "${CTL[@]}" AddonForIM s oxpinyin
        "${CTL[@]}" InputMethodGroupInfo s Default
        echo "-- mapped addon and engine"
        awk '/oxpinyin\.so|libpinyin\.so\.15/ {print $6}' "/proc/$pid/maps" | sort -u
        echo "-- mapped model data"
        awk '/libpinyin\/data/ {print $6}' "/proc/$pid/maps" | sort -u
        echo "-- package ownership"
        rpm -qf "$PKG_ADDON" "$ENGINE"
        rpm -q fcitx5-oxpinyin libpinyin libpinyin-data
        sha256sum "$PKG_ADDON" "$ENGINE"
        rpm -V fcitx5-oxpinyin && echo 'rpm -V fcitx5-oxpinyin: clean'
    } 2>&1 | tee "$out/runtime.txt"

    # Assertions: the daemon runs the package, not a development build.
    local maps
    maps=$(awk '/oxpinyin\.so|libpinyin\.so\.15/ {print $6}' "/proc/$pid/maps" | sort -u)
    [[ $(grep -c . <<<"$maps") == 2 ]] || die "expected exactly addon+engine mapped, got: $maps"
    grep -Fxq "$PKG_ADDON" <<<"$maps" || die "packaged addon $PKG_ADDON is not mapped"
    grep -Fxq "$ENGINE" <<<"$maps" || die "system engine $ENGINE is not mapped"
    # Both libraries are owned by the expected RPMs (no stray copies).
    [[ $(rpm -qf --qf '%{NAME}\n' "$PKG_ADDON") == fcitx5-oxpinyin ]] || die 'addon not owned by fcitx5-oxpinyin'
    [[ $(rpm -qf --qf '%{NAME}\n' "$ENGINE") == libpinyin ]] || die 'engine not owned by libpinyin'
    ! grep -q -E 'build|/home/|/tmp/' <<<"$maps" || die 'a build/home/tmp path is mapped'
    for f in "$USER_ADDON" "$USER_IM"; do
        [[ ! -e $f ]] || die "development descriptor still shadows the package: $f"
    done
    ! tr '\0' '\n' < "/proc/$pid/environ" | grep -E '^(LD_LIBRARY_PATH|FCITX_ADDON_DIRS|OXPINYIN_)' ||
        die 'daemon environment carries development overrides'
    rpm -V fcitx5-oxpinyin || die 'installed package files differ from the RPM'
    # The engine must be the documented libpinyin build and unmodified.
    [[ $(rpm -q --qf '%{NEVRA}' libpinyin) == "$EXPECTED_ENGINE" ]] ||
        die "engine is $(rpm -q --qf '%{NEVRA}' libpinyin), expected $EXPECTED_ENGINE (set OXPINYIN_EXPECTED_ENGINE if deliberate)"
    rpm -V libpinyin libpinyin-data || die 'libpinyin files differ from their packages'
    # The mapped addon must be the recorded artifact's payload, not merely the
    # same NEVRA: compare the installed file and the mapped inode itself with
    # the digest recorded from the RPM.
    [[ -f $DIR/rpm/MANIFEST.txt ]] || die 'no recorded artifact: run record-rpm first'
    local recorded actual range mapped_hash
    recorded=$(awk -v f="$PKG_ADDON" '$3 == f {print $1}' "$DIR/rpm/MANIFEST.txt")
    [[ $recorded =~ ^[0-9a-f]{64}$ ]] || die "no recorded digest for $PKG_ADDON"
    actual=$(sha256sum "$PKG_ADDON" | cut -d' ' -f1)
    [[ $actual == "$recorded" ]] || die "installed addon $actual != recorded $recorded"
    mapped_is_installed "$pid" "$PKG_ADDON" || die "$PKG_ADDON is not mapped from the installed (non-deleted) file: restart the session"
    mapped_is_installed "$pid" "$ENGINE" || die "$ENGINE is not mapped from the installed (non-deleted) file: restart the session"
    range=$(awk '/oxpinyin\.so/ {print $1; exit}' "/proc/$pid/maps")
    if mapped_hash=$(sha256sum "/proc/$pid/map_files/$range" 2>/dev/null | cut -d' ' -f1); then
        [[ $mapped_hash == "$recorded" ]] || die "mapped addon $mapped_hash != recorded $recorded"
    fi
    if [[ -f $DIR/rpm/MANIFEST.txt ]]; then
        local want have
        want=$(sed -n 's/^NEVRA: //p' "$DIR/rpm/MANIFEST.txt")
        have=$(rpm -q --qf '%{NEVRA}' fcitx5-oxpinyin)
        [[ $want == "$have" ]] || die "installed $have != recorded $want"
    fi
    journalctl --user "_PID=$pid" --no-pager -o short-iso |
        awk '/oxpinyin|addonloader|addonmanager/ {print}' > "$out/journal.txt" || true
    fcitx5-diagnose > "$out/fcitx5-diagnose.txt" 2>&1 || true
    echo "VERIFIED ($tag): daemon $pid maps $PKG_ADDON and $ENGINE from the installed RPMs."
    echo "evidence: $out"
}

cmd_rollback_cmds() {
    local dest f base sub name
    dest=$(latest backup) || true
    for f in "$DIR"/backup/*/shadowed/*; do
        [[ -e $f ]] || continue
        base=$(basename "$f")
        sub=${base%%-*}
        name=${base#*-}
        mkdir -p "$HOME/.local/share/fcitx5/$sub"
        mv -nv "$f" "$HOME/.local/share/fcitx5/$sub/$name"
    done
    cat <<EOF
User-level development descriptors restored (if any were moved aside).
Run yourself to remove the package and return to the development setup:
  sudo dnf remove fcitx5-oxpinyin
  # then log out and in again (or: pkill fcitx5 from a TTY, KDE restarts it)
Full config restore: see $dest/RESTORE.txt
EOF
}

case ${1:-} in
    record-rpm) shift; cmd_record_rpm "$@" ;;
    backup) cmd_backup ;;
    preflight) cmd_preflight ;;
    shadow-aside) cmd_shadow_aside ;;
    install-cmds) cmd_install_cmds ;;
    log-on) cmd_log_on ;;
    verify) shift; cmd_verify "$@" ;;
    rollback-cmds) cmd_rollback_cmds ;;
    *) sed -n '2,20p' "$0"; exit 2 ;;
esac
