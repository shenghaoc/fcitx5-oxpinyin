/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Punctuation conversion ported from ibus-libpinyin's FallbackEditor
 * (src/PYFallbackEditor.{h,cc}).  The simplified-Chinese mapping and
 * paired-quote state machine are byte-identical to upstream.
 *
 * Upstream's half-to-full-width path (PYHalfFullConverter) is NOT ported:
 * fcitx5 provides a global `fullwidth` module that filters commits
 * session-wide, so it does not belong to this addon.
 */
#include "punctuation.h"

namespace fcitx {

std::optional<std::string> Punctuation::process(uint32_t keyval,
                                                bool chinesePunct) {
    if (keyval < 0x20 || keyval > 0x7E) {
        return std::nullopt;
    }

    const bool isPunct =
        (keyval >= '!' && keyval <= '/') || (keyval >= ':' && keyval <= '@') ||
        (keyval >= '[' && keyval <= '`') || (keyval >= '{' && keyval <= '~');

    if (isPunct && chinesePunct) {
        if (auto r = processChinesePunct(keyval)) {
            return r;
        }
    }

    // Unmapped punctuation, letters, numbers and space all pass through.
    // Upstream records the key here and only here — under `if (!retval)`
    // (PYFallbackEditor.cc) — so a converted key leaves prevCommitted_
    // untouched and `1!,` yields `1！,`.
    prevCommitted_ = keyval;
    return std::nullopt;
}

// Simplified-Chinese punctuation mapping, byte-for-byte identical to
// ibus-libpinyin FallbackEditor::processPunctForSimplifiedChinese.
std::optional<std::string> Punctuation::processChinesePunct(uint32_t keyval) {
    switch (keyval) {
    case '`':
        return "\xc2\xb7"; // · U+00B7
    case '~':
        return "\xef\xbd\x9e"; // ～ U+FF5E
    case '!':
        return "\xef\xbc\x81"; // ！ U+FF01
    case '$':
        return "\xef\xbf\xa5"; // ￥ U+FFE5
    case '^':
        return "\xe2\x80\xa6\xe2\x80\xa6"; // …… U+2026 ×2
    case '(':
        return "\xef\xbc\x88"; // （ U+FF08
    case ')':
        return "\xef\xbc\x89"; // ） U+FF09
    case '_':
        return "\xe2\x80\x94\xe2\x80\x94"; // —— U+2014 ×2
    case '[':
        return "\xe3\x80\x90"; // 【 U+3010
    case ']':
        return "\xe3\x80\x91"; // 】 U+3011
    case '{':
        return "\xe3\x80\x8e"; // 『 U+300E
    case '}':
        return "\xe3\x80\x8f"; // 』 U+300F
    case '\\':
        return "\xe3\x80\x81"; // 、 U+3001
    case ';':
        return "\xef\xbc\x9b"; // ； U+FF1B
    case ':':
        return "\xef\xbc\x9a"; // ： U+FF1A
    case '\'':
        if (singleQuoteOpen_) {
            singleQuoteOpen_ = false;
            return "\xe2\x80\x98"; // ' U+2018
        }
        singleQuoteOpen_ = true;
        return "\xe2\x80\x99"; // ' U+2019
    case '"':
        if (doubleQuoteOpen_) {
            doubleQuoteOpen_ = false;
            return "\xe2\x80\x9c"; // “ U+201C
        }
        doubleQuoteOpen_ = true;
        return "\xe2\x80\x9d"; // ” U+201D
    case ',':
        if (prevCommitted_ >= '0' && prevCommitted_ <= '9') {
            prevCommitted_ = keyval;
            return std::nullopt;
        }
        return "\xef\xbc\x8c"; // ， U+FF0C
    case '.':
        if (prevCommitted_ >= '0' && prevCommitted_ <= '9') {
            prevCommitted_ = keyval;
            return std::nullopt;
        }
        return "\xe3\x80\x82"; // 。 U+3002
    case '<':
        return "\xe3\x80\x8a"; // 《 U+300A
    case '>':
        return "\xe3\x80\x8b"; // 》 U+300B
    case '?':
        return "\xef\xbc\x9f"; // ？ U+FF1F
    default:
        return std::nullopt;
    }
}

void Punctuation::reset() {
    singleQuoteOpen_ = true;
    doubleQuoteOpen_ = true;
    prevCommitted_ = 0;
}

} // namespace fcitx
