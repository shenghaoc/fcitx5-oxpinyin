/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Punctuation conversion ported from ibus-libpinyin's FallbackEditor
 * (src/PYFallbackEditor.{h,cc}), simplified-Chinese mapping.
 */
#ifndef FCITX5_OXPINYIN_PUNCTUATION_H_
#define FCITX5_OXPINYIN_PUNCTUATION_H_

#include <cstdint>
#include <optional>
#include <string>

namespace fcitx {

class Punctuation {
public:
    // Given a keyval (printable ASCII 0x20–0x7E), return the Chinese
    // punctuation string to commit, or nullopt for pass-through.  Tracks
    // prev-committed state internally for the comma/period-after-digit
    // rule.
    //
    // Half-to-full-width conversion is deliberately NOT ported from
    // upstream's PYHalfFullConverter: fcitx5 ships a global `fullwidth`
    // module that filters commits session-wide, the way fcitx5-chewing
    // relies on it. It is not a per-IME feature.
    std::optional<std::string> process(uint32_t keyval, bool chinesePunct);

    // Reset paired-quote alternation and prev-committed tracking.
    void reset();

private:
    std::optional<std::string> processChinesePunct(uint32_t keyval);

    bool singleQuoteOpen_ = true;
    bool doubleQuoteOpen_ = true;
    uint32_t prevCommitted_ = 0;
};

} // namespace fcitx

#endif // FCITX5_OXPINYIN_PUNCTUATION_H_
