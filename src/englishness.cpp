/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "englishness.h"

#include <algorithm>

#include <fcitx-utils/charutils.h>
#include <fcitx-utils/stringutils.h>

namespace fcitx {

std::tuple<bool, int> englishNess(const std::string &input, bool shuangpin) {
    // split() skips empty pieces, so every token has a front() below.
    const auto tokens = stringutils::split(input, " ");
    constexpr int fullPinyinWeight = -2;
    constexpr int shortPinyinWeight = 3;
    constexpr int invalidPinyinWeight = 6;
    int weight = 0;

    if (std::ranges::any_of(input, charutils::isupper)) {
        return {true, std::max<size_t>(
                          1, ((invalidPinyinWeight * tokens.size()) + 7) / 10)};
    }

    for (const auto &token : tokens) {
        if (shuangpin) {
            weight +=
                token.size() == 2 ? fullPinyinWeight / 2 : invalidPinyinWeight;
            continue;
        }
        if (token == "ng") {
            weight += fullPinyinWeight;
            continue;
        }
        const auto first = token.front();
        if (first == '\'') {
            return {false, 0};
        }
        if (first == 'i' || first == 'u' || first == 'v') {
            weight += invalidPinyinWeight;
        } else if (token.size() <= 2) {
            weight += shortPinyinWeight;
        } else if (token.find_first_of("aeiou") != std::string::npos) {
            weight += fullPinyinWeight;
        } else {
            weight += shortPinyinWeight;
        }
    }

    return weight < 0 ? std::tuple{false, 0}
                      : std::tuple{false, (weight + 7) / 10};
}

} // namespace fcitx
