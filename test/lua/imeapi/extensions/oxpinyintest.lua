--
-- SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
--
-- SPDX-License-Identifier: GPL-3.0-or-later
--
---- encoding: UTF-8
-- Canned imeapi extension for the oxpinyin lua harness (test-only, never
-- installed). When the candidate list contains the nihao candidate 你好,
-- answer with the fixed marker 你好世界. That makes the trigger->inject and
-- select->commit paths deterministic without any network or real data.
function oxpinyintest_nihao(input)
    return { "你好世界" }
end

ime.register_trigger("oxpinyintest_nihao", "test canned lua candidate", {},
                     { "你好" })
