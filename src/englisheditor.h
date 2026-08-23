/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * English input mode ported from ibus-libpinyin's EnglishEditor
 * (src/PYEnglishEditor.{h,cc}): type v (V in double pinyin) and a word
 * prefix, pick from the prefix-matched word list, committed words are
 * learned into the user database.  The editor shows everything in the
 * auxiliary text (upstream never sets a preedit) and owns the ibus
 * LookupTable cursor/paging semantics.
 */
#ifndef FCITX5_OXPINYIN_ENGLISHEDITOR_H_
#define FCITX5_OXPINYIN_ENGLISHEDITOR_H_

#include <string>
#include <string_view>
#include <vector>

#include <fcitx-utils/key.h>

namespace fcitx {

class InputContext;
class OxpinyinEngine;

// PYEnglishEditor.h EnglishSymbols: the punctuation set that both inserts
// into an English word and, mid-pinyin-composition, switches into English
// mode (PYPPinyinEngine.cc).
inline constexpr std::string_view EnglishSymbols = "`~!@*()+{}\\|:\"/<>?";

// Upstream page-flip gsettings defaults, not (yet) surfaced as options
// here: comma-period-page false, minus-equal-page true,
// square-bracket-page false.  square-bracket-page=false also means [ and ]
// are typed into the word rather than flipping pages.
inline constexpr bool kCommaPeriodPage = false;
inline constexpr bool kMinusEqualPage = true;
inline constexpr bool kSquareBracketPage = false;

// The mid-composition mode-switch symbol test from PYPPinyinEngine.cc
// (its g_unichar_ispunct guard is subsumed: every member is ASCII
// punctuation).  Full pinyin adds ';', double pinyin adds '\''; both add
// the square brackets while square-bracket-page is off.
bool isEnglishSwitchSymbol(char c, bool doublePinyin);

class EnglishEditor {
public:
    EnglishEditor(OxpinyinEngine *engine, InputContext *ic);

    // PYEnglishEditor processKeyEvent: sym is the normalized keysym (ibus
    // keyval), rawStates the unnormalized modifier state.  True means the
    // key was consumed; the caller exits English mode when a consumed key
    // left the text empty (the upstream engine's MODE_INIT rule).
    bool processKeyEvent(KeySym sym, KeyStates rawStates);

    // Editor::setText — used by the engine-side triggers to seed
    // "v"/"V" (+ carried pinyin) before the editor takes over.
    void setText(std::string text, size_t cursor);

    void updateAll(); // updateStateFromInput + update
    void reset();

    bool active() const { return !text_.empty(); }

    // Candidate click/selection path (global index into the word list);
    // false when the index is out of range.
    bool selectCandidate(size_t index);

private:
    bool updateStateFromInput();
    void update();

    void clearLookupTable();
    void updateLookupTable();
    void updateAuxiliaryText();

    bool selectCandidateInPage(size_t index);

    bool processSpace(KeySym keyval);
    bool processEnter(KeySym keyval);

    bool removeCharBefore();
    bool removeCharAfter();

    bool processLabelKey(KeySym keyval);
    bool processEditKey(KeySym keyval);
    bool processPageKey(KeySym keyval);

    // ibus_lookup_table_{page,cursor}_{up,down} over words_/tableCursor_
    // (round=FALSE); update() only on movement, like upstream.
    void pageUp();
    void pageDown();
    void cursorUp();
    void cursorDown();

    size_t pageSize() const;

    OxpinyinEngine *engine_;
    InputContext *ic_;

    std::string text_; // includes the leading v/V
    size_t cursor_ = 0;
    std::string auxiliary_;

    // The lookup table: all prefix matches, with the ibus cursor.
    std::vector<std::string> words_;
    size_t tableCursor_ = 0;

    static constexpr int kAuxTextLen = 50;      // m_aux_text_len
    static constexpr float kTrainFactor = 0.1F; // m_train_factor
};

} // namespace fcitx

#endif // FCITX5_OXPINYIN_ENGLISHEDITOR_H_
