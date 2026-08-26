/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FCITX5_OXPINYIN_ENGLISHNESS_H_
#define FCITX5_OXPINYIN_ENGLISHNESS_H_

#include <string>
#include <tuple>

namespace fcitx {

/*
 * Score how English-looking a raw input buffer is, matching fcitx5-chinese-
 * addons' English-versus-pinyin scoring exactly (uppercase input is always
 * English and sorts first).
 *
 * Returns {englishFirst, limit}: englishFirst puts the Spell rows at slot 0,
 * otherwise after the top pinyin candidate; limit is also the bounded number
 * of Spell hints to request — always >= 0, 0 meaning "no hints".
 *
 * Standalone TU because it is pure input handling with no engine or fcitx
 * core dependency beyond stringutils/charutils: the fuzz harness links only
 * this.
 */
std::tuple<bool, int> englishNess(const std::string &input, bool shuangpin);

} // namespace fcitx

#endif // FCITX5_OXPINYIN_ENGLISHNESS_H_
