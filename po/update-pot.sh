#!/bin/bash
# Regenerate po/fcitx5-oxpinyin.pot from every user-visible source and merge
# the result into each catalog listed in po/LINGUAS. Run from anywhere.
#
#   po/update-pot.sh           rewrite the template and merge the catalogs
#   po/update-pot.sh --check   change nothing; fail if the committed template
#                              no longer matches the sources' message set
#
# Sources: C++ _() / N_() strings, the addon and input-method manifests
# (Comment= / Name=) and the AppStream metainfo. Debug output, config keys,
# API identifiers and paths are deliberately not marked for translation.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
COMMITTED=po/fcitx5-oxpinyin.pot
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
POT="$COMMITTED"
[[ "${1:-}" == --check ]] && POT="$TMP/check.pot"

COMMON=(--from-code=UTF-8 --add-comments=TRANSLATORS: --sort-by-file
        --package-name=fcitx5-oxpinyin --msgid-bugs-address=https://github.com/shenghaoc/fcitx5-oxpinyin/issues
        --copyright-holder="Shenghao Chen")

xgettext "${COMMON[@]}" -L C++ -k_ -kN_ -o "$TMP/cpp.pot" \
    src/oxpinyin.cpp src/oxpinyin.h src/oxpinyinconfig.h src/englishness.cpp
# The addon/inputmethod manifests: only Comment= is translated. Name=Oxpinyin
# is the product name and stays untranslated, so it is not extracted.
xgettext "${COMMON[@]}" -L Desktop --keyword= --keyword=Comment \
    -o "$TMP/conf.pot" src/oxpinyin-addon.conf.in.in
# gettext only locates the metainfo ITS rules for *.metainfo.xml names, so
# extract from a correctly named copy and restore the source name afterwards.
META=org.fcitx.Fcitx5.Addon.Oxpinyin.metainfo.xml
cp "$META.in" "$TMP/$META"
(cd "$TMP" && xgettext "${COMMON[@]}" -o xml.pot "$META")
sed -i "s#^\\(\\#: \\)$META#\\1$META.in#" "$TMP/xml.pot"

# The developer name is a personal name, not translatable text.
msgcat --use-first --sort-by-file "$TMP/cpp.pot" "$TMP/conf.pot" "$TMP/xml.pot" |
    sed -e 's/charset=CHARSET/charset=UTF-8/' |
    msggrep -v -K -F -e "Shenghao Chen" |
    sed -e 's/^# SOME DESCRIPTIVE TITLE\./# fcitx5-oxpinyin translation template./' \
        -e 's/^# Copyright (C) YEAR /# Copyright (C) 2026 /' \
        -e 's/^# This file is distributed under the same license as the fcitx5-oxpinyin package\./# This file is distributed under the GPL-3.0-or-later license./' \
        -e '/^# FIRST AUTHOR/d' \
        -e 's/^"Project-Id-Version: .*/"Project-Id-Version: fcitx5-oxpinyin 0.1.0\\n"/' > "$POT"

if [[ "${1:-}" == --check ]]; then
    # Message-set comparison both ways; line numbers and dates may differ.
    msgcmp --use-untranslated "$POT" "$COMMITTED" &&
        msgcmp --use-untranslated "$COMMITTED" "$POT" || {
        echo "po/fcitx5-oxpinyin.pot is stale: run po/update-pot.sh" >&2
        exit 1
    }
    echo "po/fcitx5-oxpinyin.pot matches the sources"
    exit 0
fi

while read -r lang; do
    [[ -z "$lang" || "$lang" == \#* ]] && continue
    msgmerge --update --backup=none --no-fuzzy-matching -q "po/$lang.po" "$POT"
done < po/LINGUAS
echo "updated $POT and catalogs from po/LINGUAS"
