/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FCITX5_OXPINYIN_TEST_STUBHANZI_H_
#define FCITX5_OXPINYIN_TEST_STUBHANZI_H_

namespace fcitx {

// The fixed hanzi the hermetic cloudpinyin stub answers every request with,
// shared between the stub addon and the test so the assertion pins the exact
// word. U+9F99 (long) is chosen to NOT collide with any pinyin candidate the
// engine offers for the test's "nihao" buffer (ni/hao carry no l/ong
// syllable), so the cloud row is injected and not deduped out by
// CloudPinyinCandidateWord's cache-hit dedup.
constexpr char kStubCloudHanzi[] = "\xe9\xbe\x99"; // U+9F99

} // namespace fcitx

#endif // FCITX5_OXPINYIN_TEST_STUBHANZI_H_
