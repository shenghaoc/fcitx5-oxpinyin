/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FCITX5_OXPINYIN_OXPINYINCONFIG_H_
#define FCITX5_OXPINYIN_OXPINYINCONFIG_H_

#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/option.h>
#include <fcitx-utils/i18n.h>

namespace fcitx {

/*
 * Input schemes oxpinyin actually supports (pinyin.h values only — the six
 * compiled double-pinyin tables, DOUBLE_PINYIN_CUSTOMIZED excluded, and all
 * nine zhuyin layouts).
 */
enum class OxpinyinInputScheme {
    FullPinyin,
    DoublePinyinZRM,
    DoublePinyinMS,
    DoublePinyinZiguang,
    DoublePinyinABC,
    DoublePinyinPYJJ,
    DoublePinyinXiaohe,
    ZhuyinStandard,
    ZhuyinHsu,
    ZhuyinIBM,
    ZhuyinGinYieh,
    ZhuyinEten,
    ZhuyinEten26,
    ZhuyinStandardDvorak,
    ZhuyinHsuDvorak,
    ZhuyinDachenCP26,
};

FCITX_CONFIG_ENUM_NAME_WITH_I18N(
    OxpinyinInputScheme, N_("Full Pinyin"), N_("Natural Code (ZRM)"),
    N_("Microsoft Double Pinyin"), N_("Ziguang Double Pinyin"),
    N_("Intelligent ABC Double Pinyin"), N_("Pinyin Jiajia Double Pinyin"),
    N_("Xiaohe Double Pinyin"), N_("Standard Zhuyin"), N_("Hsu Zhuyin"),
    N_("IBM Zhuyin"), N_("Gin-Yieh Zhuyin"), N_("Eten Zhuyin"),
    N_("Eten26 Zhuyin"), N_("Standard Zhuyin (Dvorak)"),
    N_("Hsu Zhuyin (Dvorak)"), N_("Dachen CP26 Zhuyin"));

FCITX_CONFIGURATION(
    OxpinyinConfig,
    OptionWithAnnotation<OxpinyinInputScheme, OxpinyinInputSchemeI18NAnnotation>
        inputScheme{this, "InputScheme", _("Input scheme"),
                    OxpinyinInputScheme::FullPinyin};
    Option<int, IntConstrain> pageSize{
        this, "PageSize", _("Candidate page size"), 5, IntConstrain(1, 10)};
    Option<bool> incomplete{this, "Incomplete", _("Incomplete pinyin"), true};
    Option<bool> fuzzyCCh{this, "FuzzyCCh", _("Fuzzy c and ch"), false};
    Option<bool> fuzzySSh{this, "FuzzySSh", _("Fuzzy s and sh"), false};
    Option<bool> fuzzyZZh{this, "FuzzyZZh", _("Fuzzy z and zh"), false};
    Option<bool> fuzzyFH{this, "FuzzyFH", _("Fuzzy f and h"), false};
    Option<bool> fuzzyGK{this, "FuzzyGK", _("Fuzzy g and k"), false};
    Option<bool> fuzzyLN{this, "FuzzyLN", _("Fuzzy l and n"), false};
    Option<bool> fuzzyLR{this, "FuzzyLR", _("Fuzzy l and r"), false};
    Option<bool> fuzzyAnAng{this, "FuzzyAnAng", _("Fuzzy an and ang"), false};
    Option<bool> fuzzyEnEng{this, "FuzzyEnEng", _("Fuzzy en and eng"), false};
    Option<bool> fuzzyInIng{this, "FuzzyInIng", _("Fuzzy in and ing"), false};
    Option<bool> correctGnNg{this, "CorrectGnNg", _("Correct gn to ng"), false};
    Option<bool> correctMgNg{this, "CorrectMgNg", _("Correct mg to ng"), false};
    Option<bool> correctIouIu{this, "CorrectIouIu", _("Correct iou to iu"),
                              false};
    Option<bool> correctUeiUi{this, "CorrectUeiUi", _("Correct uei to ui"),
                              false};
    Option<bool> correctUenUn{this, "CorrectUenUn", _("Correct uen to un"),
                              false};
    Option<bool> correctUeVe{this, "CorrectUeVe", _("Correct ue to ve"), false};
    Option<bool> predictWords{this, "PredictWords", _("Predict next word"),
                              false};);

} // namespace fcitx

#endif // FCITX5_OXPINYIN_OXPINYINCONFIG_H_
