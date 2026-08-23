/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Ported from ibus-libpinyin src/PYEnglishEditor.cc.  The key dispatch
 * order (edit -> page -> label -> space -> enter -> insert), the insert
 * rules, the aux-text format, and the commit+train(0.1)+reset flow are
 * kept exactly; the ibus LookupTable becomes words_/tableCursor_ with the
 * same page/cursor arithmetic, rendered through a CommonCandidateList.
 */
#include "englisheditor.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <fcitx-utils/i18n.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>

#include "oxpinyin.h"

namespace fcitx {

namespace {

/*
 * Carries the word's global index back into the owning state's English
 * editor (upstream candidateClicked -> selectCandidateInPage; a global
 * index makes the page arithmetic exact for any page).
 */
class EnglishCandidateWord final : public CandidateWord {
public:
    EnglishCandidateWord(OxpinyinEngine *engine, Text display, size_t index)
        : CandidateWord(std::move(display)), engine_(engine), index_(index) {}

    void select(InputContext *ic) const override {
        engine_->state(ic)->englishSelectCandidate(index_);
    }

private:
    OxpinyinEngine *engine_;
    size_t index_;
};

const KeyList &englishSelectionKeys() {
    static const KeyList keys = {
        Key(FcitxKey_1), Key(FcitxKey_2), Key(FcitxKey_3), Key(FcitxKey_4),
        Key(FcitxKey_5), Key(FcitxKey_6), Key(FcitxKey_7), Key(FcitxKey_8),
        Key(FcitxKey_9), Key(FcitxKey_0)};
    return keys;
}

} // namespace

bool isEnglishSwitchSymbol(char c, bool doublePinyin) {
    if (EnglishSymbols.find(c) != std::string_view::npos) {
        return true;
    }
    /* For full pinyin, "'" is used. */
    if (doublePinyin && c == '\'') {
        return true;
    }
    /* Use square brackets to flip page */
    if (!kSquareBracketPage && (c == '[' || c == ']')) {
        return true;
    }
    /* For double pinyin, ";" is used. */
    if (!doublePinyin && c == ';') {
        return true;
    }
    return false;
}

EnglishEditor::EnglishEditor(OxpinyinEngine *engine, InputContext *ic)
    : engine_(engine), ic_(ic) {}

size_t EnglishEditor::pageSize() const {
    return static_cast<size_t>(*engine_->config().pageSize);
}

bool EnglishEditor::processKeyEvent(KeySym keyval, KeyStates rawStates) {
    if (rawStates.test(KeyState::Mod4)) {
        return false;
    }

    // Shift is removed (upstream masks it out); CapsLock and the
    // Ctrl/Alt/Super/Hyper/Meta combos pass through to the client.
    if (rawStates & KeyStates{KeyState::Ctrl, KeyState::Alt, KeyState::Super,
                              KeyState::Hyper, KeyState::Meta,
                              KeyState::CapsLock}) {
        return false;
    }

    // handle backspace/delete here.
    if (processEditKey(keyval)) {
        return true;
    }

    // handle page/cursor up/down here.
    if (processPageKey(keyval)) {
        return true;
    }

    // handle label key select here.
    if (processLabelKey(keyval)) {
        return true;
    }

    if (processSpace(keyval)) {
        return true;
    }

    if (processEnter(keyval)) {
        return true;
    }

    cursor_ = std::min(cursor_, text_.length());

    /* Remember the input string. */
    if (cursor_ == 0) {
        // g_return_val_if_fail: the first char is always the trigger.
        if (keyval != FcitxKey_v && keyval != FcitxKey_V) {
            return false;
        }
        text_.insert(cursor_, 1, static_cast<char>(keyval));
        cursor_++;
    } else {
        if (text_[0] != 'v' && text_[0] != 'V') {
            return false;
        }

        if ((keyval >= FcitxKey_a && keyval <= FcitxKey_z) ||
            (keyval >= FcitxKey_A && keyval <= FcitxKey_Z)) {
            text_.insert(cursor_, 1, static_cast<char>(keyval));
            cursor_++;
        }

        // The upstream g_unichar_ispunct guard is subsumed by the set.
        if (keyval <= 127 && EnglishSymbols.find(static_cast<char>(keyval)) !=
                                 std::string_view::npos) {
            text_.insert(cursor_, 1, static_cast<char>(keyval));
            cursor_++;
        }

        if (!kSquareBracketPage && (FcitxKey_bracketleft == keyval ||
                                    FcitxKey_bracketright == keyval)) {
            text_.insert(cursor_, 1, static_cast<char>(keyval));
            cursor_++;
        }
    }

    /* Deal other staff with updateStateFromInput (). */
    updateStateFromInput();
    update();
    return true;
}

bool EnglishEditor::processEditKey(KeySym keyval) {
    switch (keyval) {
    case FcitxKey_Delete:
    case FcitxKey_KP_Delete:
        removeCharAfter();
        updateStateFromInput();
        update();
        return true;
    case FcitxKey_BackSpace:
        removeCharBefore();
        updateStateFromInput();
        update();
        return true;
    default:
        break;
    }
    return false;
}

bool EnglishEditor::processPageKey(KeySym keyval) {
    switch (keyval) {
    case FcitxKey_comma:
        if (kCommaPeriodPage) {
            pageUp();
            return true;
        }
        break;
    case FcitxKey_minus:
        if (kMinusEqualPage) {
            pageUp();
            return true;
        }
        break;
    case FcitxKey_bracketleft:
        if (kSquareBracketPage) {
            pageUp();
            return true;
        }
        break;
    case FcitxKey_period:
        if (kCommaPeriodPage) {
            pageDown();
            return true;
        }
        break;
    case FcitxKey_equal:
        if (kMinusEqualPage) {
            pageDown();
            return true;
        }
        break;
    case FcitxKey_bracketright:
        if (kSquareBracketPage) {
            pageDown();
            return true;
        }
        break;

    case FcitxKey_Up:
    case FcitxKey_KP_Up:
        cursorUp();
        return true;

    case FcitxKey_Down:
    case FcitxKey_KP_Down:
        cursorDown();
        return true;

    case FcitxKey_Page_Up:
    case FcitxKey_KP_Page_Up:
        pageUp();
        return true;

    case FcitxKey_Page_Down:
    case FcitxKey_KP_Page_Down:
        pageDown();
        return true;

    case FcitxKey_Escape:
        reset();
        return true;
    default:
        break;
    }
    return false;
}

bool EnglishEditor::processLabelKey(KeySym keyval) {
    if (keyval >= FcitxKey_1 && keyval <= FcitxKey_9) {
        return selectCandidateInPage(keyval - FcitxKey_1);
    }
    if (keyval == FcitxKey_0) {
        return selectCandidateInPage(9);
    }
    return false;
}

bool EnglishEditor::processEnter(KeySym keyval) {
    if (keyval != FcitxKey_Return) {
        return false;
    }

    if (text_.length() == 0) {
        return false;
    }

    // With only the trigger char ("v"), upstream commits an empty word and
    // trains it; kept for parity — Enter must still leave English mode.
    // The empty user-db row is inert: a GLOB with a non-empty prefix can
    // never list it.
    std::string word = text_;
    word.erase(0, 1);

    ic_->commitString(word);
    engine_->englishDatabase()->train(word.c_str(), kTrainFactor);
    reset();
    return true;
}

bool EnglishEditor::processSpace(KeySym keyval) {
    if (!(keyval == FcitxKey_space || keyval == FcitxKey_KP_Space)) {
        return false;
    }

    if (text_ == "v" || text_ == "V") {
        reset();
        return true;
    }

    return selectCandidate(tableCursor_);
}

bool EnglishEditor::selectCandidateInPage(size_t index) {
    const size_t size = pageSize();
    if (index >= size) {
        return false;
    }
    index += (tableCursor_ / size) * size;

    return selectCandidate(index);
}

bool EnglishEditor::selectCandidate(size_t index) {
    if (index >= words_.size()) {
        return false;
    }

    // Copy: reset() clears words_ while the commit still needs the text.
    const std::string word = words_[index];
    ic_->commitString(word);
    engine_->englishDatabase()->train(word.c_str(), kTrainFactor);
    reset();
    return true;
}

bool EnglishEditor::updateStateFromInput() {
    /* Do parse and candidates update here. */
    /* prefix v double check here. */
    if (text_.empty()) {
        auxiliary_.clear();
        cursor_ = 0;
        clearLookupTable();
        return false;
    }

    if ('v' != text_[0] && 'V' != text_[0]) {
        auxiliary_.clear();
        clearLookupTable();
        return false;
    }

    auxiliary_ = text_[0];

    if (1 == text_.length()) {
        clearLookupTable();

        const char *helpString = _("Please input the English word.");
        const int spaceLen =
            std::max(0, kAuxTextLen - static_cast<int>(utf8::length(
                                          std::string_view(helpString))));
        auxiliary_.append(spaceLen, ' ');
        auxiliary_ += helpString;

        return true;
    }

    auxiliary_ += " ";

    const std::string prefix = text_.substr(1);
    auxiliary_ += prefix;

    /* lookup table candidate fill here. */
    std::vector<std::string> words;
    if (!engine_->englishDatabase()->listWords(prefix.c_str(), words)) {
        // Upstream keeps the previous lookup table on a failed query.
        return false;
    }

    clearLookupTable();
    words_ = std::move(words);
    return true;
}

/* Auxiliary Functions */

void EnglishEditor::pageUp() {
    // ibus_lookup_table_page_up, round=FALSE.
    if (tableCursor_ >= pageSize()) {
        tableCursor_ -= pageSize();
        update();
    }
}

void EnglishEditor::pageDown() {
    // ibus_lookup_table_page_down, round=FALSE.
    if (tableCursor_ + pageSize() < words_.size()) {
        tableCursor_ += pageSize();
        update();
    }
}

void EnglishEditor::cursorUp() {
    // ibus_lookup_table_cursor_up, round=FALSE.
    if (tableCursor_ > 0) {
        tableCursor_--;
        update();
    }
}

void EnglishEditor::cursorDown() {
    // ibus_lookup_table_cursor_down, round=FALSE.
    if (tableCursor_ + 1 < words_.size()) {
        tableCursor_++;
        update();
    }
}

void EnglishEditor::update() {
    updateLookupTable();
    // Upstream never fills m_preedit_text: the English editor has no
    // preedit, only the aux line.  Pushing the (empty) client preedit
    // clears whatever the pinyin composition had on screen before a
    // mid-composition switch.
    updateAuxiliaryText();
    if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
        ic_->updatePreedit();
    }
    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void EnglishEditor::updateAll() {
    updateStateFromInput();
    update();
}

void EnglishEditor::reset() {
    text_.clear();
    updateStateFromInput();
    update();
}

void EnglishEditor::clearLookupTable() {
    words_.clear();
    tableCursor_ = 0;
}

void EnglishEditor::updateLookupTable() {
    // The panel rebuild replaces both the aux text and the candidate
    // list; update() always runs both, so resetting here is safe.
    auto &panel = ic_->inputPanel();
    panel.reset();

    if (words_.empty()) {
        return;
    }

    const size_t size = pageSize();
    auto list = std::make_unique<CommonCandidateList>();
    list->setSelectionKey(englishSelectionKeys());
    list->setPageSize(static_cast<int>(size));
    for (size_t i = 0; i < words_.size(); ++i) {
        list->append<EnglishCandidateWord>(engine_, Text(words_[i]), i);
    }
    list->setGlobalCursorIndex(static_cast<int>(tableCursor_));
    list->setPage(static_cast<int>(tableCursor_ / size));
    panel.setCandidateList(std::move(list));
}

void EnglishEditor::updateAuxiliaryText() {
    if (auxiliary_.empty()) {
        return;
    }
    ic_->inputPanel().setAuxUp(Text(auxiliary_));
}

bool EnglishEditor::removeCharBefore() {
    if (cursor_ <= 0) {
        cursor_ = 0;
        return false;
    }

    if (cursor_ > text_.length()) {
        cursor_ = text_.length();
        return false;
    }

    text_.erase(cursor_ - 1, 1);
    cursor_--;
    return true;
}

bool EnglishEditor::removeCharAfter() {
    if (cursor_ >= text_.length()) {
        cursor_ = text_.length();
        return false;
    }

    text_.erase(cursor_, 1);
    cursor_ = std::min(cursor_, text_.length());
    return true;
}

void EnglishEditor::setText(std::string text, size_t cursor) {
    text_ = std::move(text);
    cursor_ = cursor;
}

} // namespace fcitx
