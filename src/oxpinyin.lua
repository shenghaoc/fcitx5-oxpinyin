--
-- SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
--
-- SPDX-License-Identifier: GPL-3.0-or-later
--
---- encoding: UTF-8
-- Default imeapi extension for oxpinyin: date/time candidates, the same
-- candidate-trigger delegation shape fcitx5-chinese-addons' pinyin.lua uses
-- (a candidate string of 日期/时间 triggers the extra rows). Every function
-- guards on the current input method being oxpinyin, so this extension stays
-- inert for any other IME that shares the imeapi extensions directory.
local fcitx = require("fcitx")

local function oxpinyin_active()
    return fcitx.currentInputMethod() == "oxpinyin"
end

function oxpinyin_get_today(input)
    if not oxpinyin_active() then
        return nil
    end
    return { os.date("%Y-%m-%d"), os.date("%Y年%m月%d日") }
end

function oxpinyin_get_current_time(input)
    if not oxpinyin_active() then
        return nil
    end
    return { os.date("%H:%M"), os.date("%H时%M分") }
end

------------
ime.register_trigger("oxpinyin_get_current_time", "显示时间", {}, { "时间" })
ime.register_trigger("oxpinyin_get_today", "显示日期", {}, { "日期" })
