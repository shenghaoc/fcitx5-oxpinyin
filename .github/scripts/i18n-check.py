#!/usr/bin/env python3
"""Gettext checks: source catalogs (`source`) and installed payloads (`payload ROOT`).

`source` validates the catalogs in po/ without building anything.
`payload ROOT [LOCALEDIR]` proves that a staged install, package payload or archive
extraction actually contains the compiled catalogs in the expected locale
hierarchy (LOCALEDIR is the configured relative locale directory, default
`share/locale`), that each loads and covers every template message, and
that every translated message equals its source `.po` entry, and that the
addon manifest and AppStream metainfo carry the translations too.
Nothing here touches a live installation or locale configuration.
"""
import configparser
import ast
import gettext
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
PO = ROOT / 'po'
DOMAIN = 'fcitx5-oxpinyin'
# msgfmt writes BCP 47 xml:lang tags (zh-Hans-CN) in newer gettext and POSIX
# forms (zh_CN) in older releases; both name the same language and region.
XML_NS = '{http://www.w3.org/XML/1998/namespace}lang'


def linguas():
    return [line.strip() for line in (PO / 'LINGUAS').read_text().splitlines()
            if line.strip() and not line.lstrip().startswith('#')]


def po_entries(path):
    """msgid -> msgstr of a .po/.pot (header and obsolete entries skipped)."""
    entries, msgid, msgstr, current = {}, None, None, None

    def flush():
        if msgid:
            entries[msgid] = msgstr

    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if line.startswith('msgid '):
            flush()
            msgid, msgstr, current = ast.literal_eval(line[6:]), '', 'id'
        elif line.startswith('msgstr '):
            msgstr, current = ast.literal_eval(line[7:]), 'str'
        elif line.startswith('"'):
            part = ast.literal_eval(line)
            if current == 'id':
                msgid += part
            elif current == 'str':
                msgstr += part
    flush()
    return entries


def xml_lang_matches(lang, tag):
    language, _, region = lang.partition('_')
    parts = tag.replace('_', '-').split('-')
    return parts[0] == language and (not region or parts[-1] == region)


def source():
    langs = linguas()
    assert langs, 'po/LINGUAS lists no languages'
    assert sorted(langs) == sorted(p.stem for p in PO.glob('*.po')), \
        'po/LINGUAS and po/*.po disagree'
    for lang in langs:
        po = PO / f'{lang}.po'
        subprocess.run(['msgfmt', '--check', '--check-format', '--check-domain',
                        '--statistics', '-o', '/dev/null', str(po)], check=True)
        # Every template message translated; none fuzzy or obsolete.
        for flag in ('--untranslated', '--only-fuzzy', '--only-obsolete'):
            out = subprocess.run(['msgattrib', flag, str(po)],
                                 capture_output=True, text=True,
                                 check=True).stdout
            assert not out.strip(), f'{lang}: {flag} entries:\n{out}'
        subprocess.run(['msgcmp', '--use-untranslated', str(po),
                        str(PO / f'{DOMAIN}.pot')], check=True)
        assert f'"Language: {lang}\\n"' in po.read_text(encoding='utf-8'), \
            f'{lang}: Language header'
    subprocess.run([str(PO / 'update-pot.sh'), '--check'], check=True)
    print(f'source catalogs ({", ".join(langs)}): PASS')


def payload(root, localedir='share/locale'):
    root = Path(root)
    expected = list(po_entries(PO / f'{DOMAIN}.pot'))
    assert expected, 'empty template'
    files = [p for p in root.rglob('*') if p.is_file()]
    for lang in linguas():
        suffix = f'/{localedir.strip("/")}/{lang}/LC_MESSAGES/{DOMAIN}.mo'
        mo = [p for p in files if str(p).endswith(suffix)]
        assert len(mo) == 1, (lang, 'catalog missing or duplicated', mo)
        with mo[0].open('rb') as handle:
            catalog = gettext.GNUTranslations(handle)
        # Every template message (including the optional Cloud Pinyin ones,
        # which live in the same catalog) must be present in the compiled .mo,
        # and each must translate exactly as its source .po entry says.
        missing = [m for m in expected if m not in catalog._catalog]
        assert not missing, (lang, 'untranslated in installed catalog', missing)
        for msgid, msgstr in po_entries(PO / f'{lang}.po').items():
            assert msgstr and catalog.gettext(msgid) == msgstr, (lang, msgid)
        assert catalog.info()['language'] == lang
        addon = next(p for p in files if str(p).endswith('/addon/oxpinyin.conf'))
        text = addon.read_text(encoding='utf-8')
        assert re.search(rf'^Comment\[{lang}\]=\S', text, re.M), (lang, 'manifest')
        meta = next(p for p in files if str(p).endswith('.metainfo.xml'))
        tags = {e.get(XML_NS) for e in ET.parse(meta).getroot().iter('name')}
        assert any(t and xml_lang_matches(lang, t) for t in tags), (lang, 'metainfo', tags)
    # Product name stays untranslated in the manifests.
    im = next(p for p in files if str(p).endswith('/inputmethod/oxpinyin.conf'))
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    parser.read(im, encoding='utf-8')
    assert parser['InputMethod']['Name'] == 'Oxpinyin'
    print(f'installed translation payload under {root} ({", ".join(linguas())}): PASS')


if __name__ == '__main__':
    if sys.argv[1:2] == ['source']:
        source()
    elif sys.argv[1:2] == ['payload'] and len(sys.argv) in (3, 4):
        payload(*sys.argv[2:])
    else:
        sys.exit(__doc__)
