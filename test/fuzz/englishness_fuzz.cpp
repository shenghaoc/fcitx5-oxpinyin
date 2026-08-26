/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// libFuzzer harness over the shell's pure input-handling seam
// (fcitx::englishNess). Everything it consumes comes from the raw typed
// buffer, so arbitrary bytes are fair input: empty strings, spaces only,
// lone apostrophes, non-ASCII bytes, huge tokens.
//
// Determinism: the function is pure (no globals, no clock, no filesystem),
// so replays of a crashing input reproduce exactly. CI bounds every run
// (-runs/-timeout/-rss_limit_mb); explorative campaigns belong to the
// nightly workflow.
#include "englishness.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // One real composition never approaches this size; keeping inputs bound
    // makes smoke runs strictly deterministic in cost.
    if (size > 4096) {
        return 0;
    }
    const std::string input(reinterpret_cast<const char *>(data), size);
    const bool shuangpin = size % 2 != 0;

    const auto [englishFirst, limit] = fcitx::englishNess(input, shuangpin);
    (void)englishFirst;
    // Contract: limit is the number of Spell hints to request — never
    // negative, and small enough to hand to the spell provider.
    if (limit < 0 || limit > 100000) {
        std::abort();
    }
    return 0;
}
