/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "testdir.h"
#include "testfrontend_public.h"

#include <algorithm>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/testing.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/userinterfacemanager.h>
#include <string>
#include <vector>

#include "oxpinyinconfig.h"

using namespace fcitx;

namespace {

/*
 * The assertions pin fcitx WIRING only — which keys get filtered, what the
 * panel shows, what gets committed. Engine output correctness (candidate
 * quality, sentence text) is pinned by oxpinyin's oracle differentials,
 * not here: expectations are read back from the panel instead of being
 * hardcoded.
 */

void setupGroup(Instance *instance) {
    auto defaultGroup = instance->inputMethodManager().currentGroup();
    defaultGroup.inputMethodList().clear();
    defaultGroup.inputMethodList().push_back(
        InputMethodGroupItem("keyboard-us"));
    defaultGroup.inputMethodList().push_back(InputMethodGroupItem("oxpinyin"));
    defaultGroup.setDefaultInputMethod("");
    instance->inputMethodManager().setGroup(defaultGroup);
}

// Phase 1: the addon loads against the real engine data, the IM group
// switch works, and with no composition every key passes through
// unfiltered (sendKeyEvent returns false).
void testLoadAndPassthrough(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        FCITX_ASSERT(oxpinyin);

        setupGroup(instance);

        auto *testfrontend = instance->addonManager().addon("testfrontend");
        // A missing test frontend would otherwise segfault on the first
        // call() below; name the real cause instead.
        FCITX_ASSERT(testfrontend);
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(ic) == "oxpinyin");

        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Ctrl+A"), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        instance->deactivate();
    });
}

// Optional-modules invariant: chttrans/fullwidth (and cloudpinyin) are
// optional dependencies. With none installed — the CI image ships no
// fcitx5-chinese-addons, and this harness enables only oxpinyin — activate()
// must run its guarded toggle-wiring without crashing, no toggle Action is
// present to add, and composition/commit still work. This is THE invariant
// the wiring must preserve: the addon loads and functions with none of the
// optional modules present.
void testOptionalModulesAbsent(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        // Control+space activates oxpinyin, running OxpinyinEngine::activate:
        // it requests chttrans/fullwidth and tries to add their toggle
        // Actions. All are absent here, so the guarded path is exercised.
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(ic) == "oxpinyin");

        // The modules are not loaded, so none of their Actions are registered.
        FCITX_ASSERT(
            !instance->userInterfaceManager().lookupAction("chttrans"));
        FCITX_ASSERT(
            !instance->userInterfaceManager().lookupAction("fullwidth"));
        FCITX_ASSERT(
            !instance->userInterfaceManager().lookupAction("cloudpinyin"));

        // Composition still works with the modules absent.
        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(!ic->inputPanel().preedit().toString().empty() ||
                     !ic->inputPanel().clientPreedit().toString().empty());
        FCITX_ASSERT(ic->inputPanel().candidateList() &&
                     !ic->inputPanel().candidateList()->empty());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        instance->deactivate();
    });
}

// Phase 2: nihao -> filtered keys, non-empty preedit, non-empty candidate
// list, Enter commits the sentence shown in the preedit, panel clears.
void testTypeCommit(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto preedit = ic->inputPanel().preedit().toString();
        FCITX_ASSERT(!preedit.empty());
        const auto candidates = ic->inputPanel().candidateList();
        FCITX_ASSERT(candidates && !candidates->empty());

        // Enter commits the sentence the panel was showing.
        testfrontend->call<ITestFrontend::pushCommitExpectation>(preedit);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Return"), false));

        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        instance->deactivate();
    });
}

// Phase 2: backspace edits the buffer; emptying it clears the panel and
// the next backspace passes through.
void testBackspace(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        // Edit back down to nothing: every backspace is consumed while a
        // buffer exists.
        for (int i = 0; i < 5; ++i) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(FcitxKey_BackSpace), false));
        }
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(!ic->inputPanel().candidateList());
        // With no buffer left, backspace belongs to the client.
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_BackSpace), false));

        instance->deactivate();
    });
}

// Phase 2: Escape drops the composition; typing resumes afterwards.
void testEscape(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        // Fresh composition after the reset.
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("n"), false));
        FCITX_ASSERT(!ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        instance->deactivate();
    });
}

// Phase 2: modifier combos pass through even mid-composition; digit
// selection commits and clears; Page_Down pages the list.
void testCandidatesAndPassthrough(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        // Ctrl-combo passthrough while composing.
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Ctrl+A"), false));
        FCITX_ASSERT(!ic->inputPanel().candidateList()->empty());

        // Digit selection: commit expectation is the sentence the panel
        // shows (candidate 0 is the n-best row).
        const auto preedit = ic->inputPanel().preedit().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(preedit);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("1"), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        // A single syllable with many matches: paging stays in the panel.
        for (const auto c : std::string("shi")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(ic->inputPanel().candidateList() &&
                     !ic->inputPanel().candidateList()->empty());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Page_Down"), false));
        FCITX_ASSERT(ic->inputPanel().candidateList() &&
                     !ic->inputPanel().candidateList()->empty());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Page_Up"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        instance->deactivate();
    });
}

// Phase 3: the aux-text getter feeds the display — after typing, the
// panel (or client) shows the sentence and the typed syllables appear in
// the aux area; no cursor marker leaks into the display.
void testAuxPreedit(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto preedit = ic->inputPanel().preedit().toString();
        const auto client = ic->inputPanel().clientPreedit().toString();
        FCITX_ASSERT(!preedit.empty() || !client.empty());
        const auto aux = ic->inputPanel().auxDown().toString();
        FCITX_ASSERT(!aux.empty());
        FCITX_ASSERT(aux.find('|') == std::string::npos);

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));
        instance->deactivate();
    });
}

// Phase 3: setConfig propagates live — page size honors a new value, a
// fuzzy toggle round-trips through getConfig.
void testConfigApply(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        RawConfig config;
        config.setValueByPath("PageSize", "3");
        oxpinyin->setConfig(config);
        {
            const auto *const current =
                static_cast<const OxpinyinConfig *>(oxpinyin->getConfig());
            FCITX_ASSERT(*current->pageSize == 3);
        }

        for (const auto c : std::string("shi")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto list = ic->inputPanel().candidateList();
        FCITX_ASSERT(list && !list->empty());
        const auto common =
            std::dynamic_pointer_cast<CommonCandidateList>(list);
        FCITX_ASSERT(common && common->pageSize() == 3);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        config.setValueByPath("FuzzyCCh", "True");
        oxpinyin->setConfig(config);
        {
            const auto *const current =
                static_cast<const OxpinyinConfig *>(oxpinyin->getConfig());
            FCITX_ASSERT(*current->fuzzyCCh);
        }
        config.setValueByPath("FuzzyCCh", "False");
        config.setValueByPath("PageSize", "5");
        oxpinyin->setConfig(config);
        {
            const auto *const current =
                static_cast<const OxpinyinConfig *>(oxpinyin->getConfig());
            FCITX_ASSERT(!*current->fuzzyCCh);
            FCITX_ASSERT(*current->pageSize == 5);
        }

        instance->deactivate();
    });
}

// Spell integration: the bundled fcitx Spell dictionary supplies real English
// candidates. Uppercase input occupies slot 0; lower-case English-looking
// input follows the first pinyin candidate in slot 1.
#ifndef OXPINYIN_TEST_NO_SPELL
void testSpellCandidates(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        RawConfig config;
        config.setValueByPath("SpellEnabled", "True");
        oxpinyin->setConfig(config);

        for (const auto c : std::string("Helo")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto uppercase = ic->inputPanel().candidateList();
        FCITX_ASSERT(uppercase && !uppercase->empty());
        const auto uppercaseWord = uppercase->candidate(0).text().toString();
        FCITX_ASSERT(!uppercaseWord.empty());
        FCITX_ASSERT(
            std::all_of(uppercaseWord.begin(), uppercaseWord.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            }));
        testfrontend->call<ITestFrontend::pushCommitExpectation>(uppercaseWord);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("1"), false));
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        for (const auto c : std::string("rhythm")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto lowercase = ic->inputPanel().candidateList();
        FCITX_ASSERT(lowercase && lowercase->size() > 1);
        FCITX_ASSERT(lowercase->candidate(1).text().toString() == "rhythm");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        config.setValueByPath("SpellEnabled", "False");
        oxpinyin->setConfig(config);
        for (const auto c : std::string("rhythm")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto disabled = ic->inputPanel().candidateList();
        FCITX_ASSERT(disabled && !disabled->empty());
        for (int i = 0; i < disabled->size(); ++i) {
            FCITX_ASSERT(disabled->candidate(i).text().toString() != "rhythm");
        }
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(ic->inputPanel().candidateList() &&
                     !ic->inputPanel().candidateList()->empty());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        config.setValueByPath("SpellEnabled", "True");
        oxpinyin->setConfig(config);
        instance->deactivate();
    });
}

// Regression: Space must select the first DISPLAYED candidate (the digit
// path), not pinyin-engine candidate 0 — uppercase input puts a Spell word
// in slot 0, where the engine index would commit the wrong word or nothing.
void testSpellSpaceSelection(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        // Spell-first list (uppercase): Space commits the slot-0 spell word
        // and clears the panel.
        for (const auto c : std::string("Helo")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto spellFirst = ic->inputPanel().candidateList();
        FCITX_ASSERT(spellFirst && !spellFirst->empty());
        const auto spellWord = spellFirst->candidate(0).text().toString();
        FCITX_ASSERT(
            std::all_of(spellWord.begin(), spellWord.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            }));
        testfrontend->call<ITestFrontend::pushCommitExpectation>(spellWord);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        // Mixed list (lower case): spell sits at slot 1; Space still targets
        // displayed slot 0, the pinyin first choice.
        for (const auto c : std::string("rhythm")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto mixed = ic->inputPanel().candidateList();
        FCITX_ASSERT(mixed && mixed->size() > 1);
        FCITX_ASSERT(mixed->candidate(1).text().toString() == "rhythm");
        const auto mixedFirst = mixed->candidate(0).text().toString();
        FCITX_ASSERT(mixedFirst != "rhythm");
        testfrontend->call<ITestFrontend::pushCommitExpectation>(mixedFirst);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        // Whether Space committed or left a pinned composition, reset before
        // the next scenario.
        testfrontend->call<ITestFrontend::sendKeyEvent>(uuid, Key("Escape"),
                                                        false);

        // The pinyin Space path is unchanged: full-pinyin input commits its
        // first candidate.
        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto pinyin = ic->inputPanel().candidateList();
        FCITX_ASSERT(pinyin && !pinyin->empty());
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            pinyin->candidate(0).text().toString());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        instance->deactivate();
    });
}
#else
void testSpellUnavailable(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        // This reaches spellHint(), but the disabled Spell module returns
        // null. The initial uppercase key must remain a client key.
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("H"), false));
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(ic->inputPanel().candidateList() &&
                     !ic->inputPanel().candidateList()->empty());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));
        instance->deactivate();
    });
}
#endif

// Phase 3: scheme switch drives the parse mode — zhuyin parses a bopomofo
// key sequence (standard layout: a=ㄇ, 8=ㄚ), full pinyin still works
// after switching back.
void testSchemeSwitchZhuyin(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        RawConfig config;
        config.setValueByPath("InputScheme", "Standard Zhuyin");
        oxpinyin->setConfig(config);
        {
            const auto *const current =
                static_cast<const OxpinyinConfig *>(oxpinyin->getConfig());
            FCITX_ASSERT(current->inputScheme.value() ==
                         OxpinyinInputScheme::ZhuyinStandard);
        }

        // ㄇㄚ under the standard layout; digits outside the current page
        // are not selection keys while composing in zhuyin mode ('8' is a
        // bopomofo key here).
        for (const auto c : std::string("a8")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(!ic->inputPanel().auxDown().toString().empty() ||
                     !ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(ic->inputPanel().candidateList() &&
                     !ic->inputPanel().candidateList()->empty());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        // Back to full pinyin.
        config.setValueByPath("InputScheme", "Full Pinyin");
        oxpinyin->setConfig(config);
        for (const auto c : std::string("ma")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(!ic->inputPanel().auxDown().toString().empty());
        FCITX_ASSERT(ic->inputPanel().candidateList() &&
                     !ic->inputPanel().candidateList()->empty());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        instance->deactivate();
    });
}

// Phase 4 helpers: pick a partial candidate (a single-character word from
// a multi-syllable composition can never consume the whole buffer) and
// drive a fresh composition of the same keys on the same input context.
namespace {

// Returns the index of the first single-character candidate, or -1.
int singleCharCandidate(const std::shared_ptr<CandidateList> &list) {
    for (int i = 0; i < list->size(); ++i) {
        if (utf8::lengthValidated(list->candidate(i).text().toString()) == 1) {
            return i;
        }
    }
    return -1;
}

// CandidateList::size() is page-local; the discriminating totals live on
// the concrete CommonCandidateList.
int totalSize(const std::shared_ptr<CandidateList> &list) {
    const auto common = std::dynamic_pointer_cast<CommonCandidateList>(list);
    return common ? common->totalSize() : -1;
}

} // namespace

// Phase 4: choosing a partial candidate pins the prefix and keeps
// composing — nothing commits, the preedit carries the chosen prefix plus
// the live tail, and the candidate list becomes the tail's. Finishing
// with Enter commits the constrained sentence.
void testPartialChoiceContinues(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto list1 = ic->inputPanel().candidateList();
        FCITX_ASSERT(list1 && !list1->empty());
        const auto total1 = totalSize(list1);

        const auto partial = singleCharCandidate(list1);
        FCITX_ASSERT(partial >= 0);
        list1->candidate(partial).select(ic);

        // Still composing: no commit happened, preedit alive, and the
        // rebuilt list is the tail's (a smaller total than the whole
        // buffer's; the page-local view can look identical).
        const auto list2 = ic->inputPanel().candidateList();
        const auto preedit = ic->inputPanel().preedit().toString();
        const auto client = ic->inputPanel().clientPreedit().toString();
        FCITX_ASSERT(!preedit.empty() || !client.empty());
        FCITX_ASSERT(list2);
        FCITX_ASSERT(totalSize(list2) != total1);

        // Finish: Enter commits the constrained sentence shown.
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            !preedit.empty() ? preedit : client);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Return"), false));
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        instance->deactivate();
    });
}

// Phase 4: backspacing into the pinned prefix unpins it — after clearing
// back through the choice, the state composes normally and a fresh input
// decodes identically to an unconstrained one (no pin leaks).
void testBackspaceUnpins(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        // Reference: an unconstrained "nihao" composition's first list.
        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto refList = ic->inputPanel().candidateList();
        FCITX_ASSERT(refList && !refList->empty());
        const auto refFirst = refList->candidate(0).text().toString();
        const auto refTotal = totalSize(refList);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        // Partially choose, then backspace through the free tail and into
        // the pinned prefix.
        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto partial =
            singleCharCandidate(ic->inputPanel().candidateList());
        FCITX_ASSERT(partial >= 0);
        ic->inputPanel().candidateList()->candidate(partial).select(ic);
        for (int i = 0; i < 5; ++i) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(FcitxKey_BackSpace), false));
        }
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        // No pin leaked: a fresh identical composition matches the
        // unconstrained reference list.
        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto freshList = ic->inputPanel().candidateList();
        FCITX_ASSERT(freshList && totalSize(freshList) == refTotal);
        FCITX_ASSERT(freshList->candidate(0).text().toString() == refFirst);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        instance->deactivate();
    });
}

// Phase 4: Escape mid-pin drops the constraints (not just the shell
// state) — the composition after the escape decodes unconstrained.
void testEscapeClearsPins(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        // Unconstrained reference for the same keys.
        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto refList = ic->inputPanel().candidateList();
        const auto refFirst = refList->candidate(0).text().toString();
        const auto refTotal = totalSize(refList);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto partial =
            singleCharCandidate(ic->inputPanel().candidateList());
        FCITX_ASSERT(partial >= 0);
        ic->inputPanel().candidateList()->candidate(partial).select(ic);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        // A surviving pin would force the chosen prefix into the decode
        // and shrink the candidate totals; the unconstrained reference
        // must be reproduced exactly.
        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto list = ic->inputPanel().candidateList();
        FCITX_ASSERT(list && !list->empty());
        FCITX_ASSERT(list->candidate(0).text().toString() == refFirst);
        FCITX_ASSERT(totalSize(list) == refTotal);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        instance->deactivate();
    });
}

// Phase 5: after a commit with predictWords on, the engine offers predicted
// next-word candidates — a non-empty candidate list with NO composing
// preedit (prediction, not composition).
void testPredictAfterCommit(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        // Enable prediction.
        RawConfig config;
        config.setValueByPath("PredictWords", "True");
        oxpinyin->setConfig(config);

        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto preedit = ic->inputPanel().preedit().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(preedit);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Return"), false));

        // After commit: prediction candidates shown, no preedit.
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(ic->inputPanel().clientPreedit().toString().empty());
        const auto predicted = ic->inputPanel().candidateList();
        FCITX_ASSERT(predicted && !predicted->empty());

        // Clean up.
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));
        config.setValueByPath("PredictWords", "False");
        oxpinyin->setConfig(config);
        instance->deactivate();
    });
}

// Phase 5: selecting a predicted candidate commits it AND shows a fresh
// predicted list (re-prediction chain).
void testPredictChain(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        RawConfig config;
        config.setValueByPath("PredictWords", "True");
        oxpinyin->setConfig(config);

        // A context with multiple prediction pages lets the harness select a
        // panel-provided candidate that itself has follow-up predictions.
        for (const auto c : std::string("zhongguo")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto preedit = ic->inputPanel().preedit().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(preedit);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Return"), false));

        // We are now in prediction mode. Page to the fixture's next page;
        // candidate text is always read from the panel, never hardcoded.
        FCITX_ASSERT(ic->inputPanel().candidateList() &&
                     !ic->inputPanel().candidateList()->empty());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Page_Down"), false));
        const auto predicted = ic->inputPanel().candidateList();
        FCITX_ASSERT(predicted && !predicted->empty());
        const auto selected = predicted->candidate(0).text().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(selected);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("1"), false));

        // The predicted selection committed and left no composing preedit.
        // Whether that particular word has follow-up predictions is engine
        // data, so what is pinned here is the panel/state agreement: Escape
        // is consumed exactly when a prediction list is on screen, and
        // belongs to the client once the chain runs out.
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(ic->inputPanel().clientPreedit().toString().empty());
        const auto chained = ic->inputPanel().candidateList();
        const bool chainContinues = chained && !chained->empty();

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                         uuid, Key("Escape"), false) == chainContinues);
        config.setValueByPath("PredictWords", "False");
        oxpinyin->setConfig(config);
        instance->deactivate();
    });
}

// Phase 5: typing a pinyin key while in prediction mode exits prediction
// and starts a fresh composition.
void testPredictExitOnTyping(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        RawConfig config;
        config.setValueByPath("PredictWords", "True");
        oxpinyin->setConfig(config);

        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto preedit = ic->inputPanel().preedit().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(preedit);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Return"), false));

        // In prediction mode.
        FCITX_ASSERT(ic->inputPanel().candidateList() &&
                     !ic->inputPanel().candidateList()->empty());

        // Type a pinyin key: exits prediction, starts fresh composition.
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("w"), false));

        // Now composing: preedit reflects the new key, candidate list is
        // the composition's (not prediction's).
        const auto newPreedit = ic->inputPanel().preedit().toString();
        const auto newClient = ic->inputPanel().clientPreedit().toString();
        FCITX_ASSERT(!newPreedit.empty() || !newClient.empty());

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));
        config.setValueByPath("PredictWords", "False");
        oxpinyin->setConfig(config);
        instance->deactivate();
    });
}

// Phase 5: with predictWords off, no prediction list appears after commit
// (Phases 2-4 behavior intact).
void testPredictToggleOff(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        // Explicitly off (default, but be explicit).
        RawConfig config;
        config.setValueByPath("PredictWords", "False");
        oxpinyin->setConfig(config);

        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto preedit = ic->inputPanel().preedit().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(preedit);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Return"), false));

        // No prediction: panel empty, no candidates.
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        instance->deactivate();
    });
}

// Phase 5: Chinese punctuation — representative ASCII→Chinese mappings
// via the internal Punctuation module, behaviour-identical to
// ibus-libpinyin FallbackEditor::processPunctForSimplifiedChinese.
void testPunctuationMapping(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        struct Case {
            KeySym sym;
            std::string expected;
        };
        // clang-format off
        const Case cases[] = {
            {FcitxKey_exclam,       "\xef\xbc\x81"},       // ！
            {FcitxKey_comma,        "\xef\xbc\x8c"},       // ，
            {FcitxKey_period,       "\xe3\x80\x82"},       // 。
            {FcitxKey_question,     "\xef\xbc\x9f"},       // ？
            {FcitxKey_colon,        "\xef\xbc\x9a"},       // ：
            {FcitxKey_semicolon,    "\xef\xbc\x9b"},       // ；
            {FcitxKey_less,         "\xe3\x80\x8a"},       // 《
            {FcitxKey_greater,      "\xe3\x80\x8b"},       // 》
            {FcitxKey_parenleft,    "\xef\xbc\x88"},       // （
            {FcitxKey_parenright,   "\xef\xbc\x89"},       // ）
            {FcitxKey_bracketleft,  "\xe3\x80\x90"},       // 【
            {FcitxKey_bracketright, "\xe3\x80\x91"},       // 】
            {FcitxKey_braceleft,    "\xe3\x80\x8e"},       // 『
            {FcitxKey_braceright,   "\xe3\x80\x8f"},       // 』
            {FcitxKey_backslash,    "\xe3\x80\x81"},       // 、
            {FcitxKey_grave,        "\xc2\xb7"},           // ·
            {FcitxKey_asciitilde,   "\xef\xbd\x9e"},       // ～
            {FcitxKey_dollar,       "\xef\xbf\xa5"},       // ￥
            {FcitxKey_asciicircum,  "\xe2\x80\xa6\xe2\x80\xa6"}, // ……
            {FcitxKey_underscore,   "\xe2\x80\x94\xe2\x80\x94"}, // ——
        };
        // clang-format on

        for (const auto &tc : cases) {
            testfrontend->call<ITestFrontend::pushCommitExpectation>(
                tc.expected);
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(tc.sym), false));
        }

        instance->deactivate();
    });
}

// Phase 5: paired-quote alternation (single and double).
void testPairedQuotes(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        // Single quote: opening, closing, opening again.
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            "\xe2\x80\x98"); // '
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_apostrophe), false));
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            "\xe2\x80\x99"); // '
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_apostrophe), false));
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            "\xe2\x80\x98"); // ' again
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_apostrophe), false));

        // Double quote: opening, closing.
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            "\xe2\x80\x9c"); // "
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_quotedbl), false));
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            "\xe2\x80\x9d"); // "
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_quotedbl), false));

        instance->deactivate();
    });
}

// Phase 5: comma and period after a digit pass through (not converted).
void testCommaAfterDigit(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        // '5' passes through (not consumed by punctuation).
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_5), false));
        // Comma after digit: not consumed.
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_comma), false));
        // Period after digit (prev is now comma, not digit): converted.
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            "\xe3\x80\x82"); // 。
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_period), false));

        instance->deactivate();
    });
}

// Phase 5: unmapped punctuation keys (like @, #, /) pass through.
void testUnmappedPunctPassthrough(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_at), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_numbersign), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_slash), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_plus), false));

        instance->deactivate();
    });
}

// Phase 5: typing punctuation mid-composition commits the sentence
// first, then commits the punctuation.
void testPunctuationMidComposition(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto preedit = ic->inputPanel().preedit().toString();
        FCITX_ASSERT(!preedit.empty());

        // '!' mid-composition: commits the sentence, then the '！'.
        testfrontend->call<ITestFrontend::pushCommitExpectation>(preedit);
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            "\xef\xbc\x81"); // ！
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_exclam), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());

        instance->deactivate();
    });
}

// Punctuation typed while a prediction list is shown must still convert.
// The dismissal path used to re-dispatch only keys acceptChar() accepts,
// so punctuation was swallowed: prediction closed and nothing committed.
void testPunctuationDismissesPrediction(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        RawConfig config;
        config.setValueByPath("PredictWords", "True");
        oxpinyin->setConfig(config);

        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto preedit = ic->inputPanel().preedit().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(preedit);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Return"), false));

        // A prediction list is on screen.
        FCITX_ASSERT(ic->inputPanel().candidateList() &&
                     !ic->inputPanel().candidateList()->empty());

        // '!' is consumed and commits '！' — not dropped by the dismissal.
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            "\xef\xbc\x81"); // ！
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_exclam), false));

        // Prediction is gone and nothing is composing.
        FCITX_ASSERT(!ic->inputPanel().candidateList());
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(ic->inputPanel().clientPreedit().toString().empty());

        config.setValueByPath("PredictWords", "False");
        oxpinyin->setConfig(config);
        instance->deactivate();
    });
}

#if defined(OXPINYIN_TEST_CLOUDPINYIN) || defined(OXPINYIN_TEST_CLOUD_ABSENT)
// The "☁" (U+2601) unicode char cloudpinyin shows as the loading placeholder
// until an async result fills the row. Shared by the module-present runner and
// the module-absent guard runner (which asserts the row never appears).
constexpr char kCloudPlaceholder[] = "\xe2\x98\x81";

bool listHasCloudPlaceholder(InputContext *ic) {
    const auto list = ic->inputPanel().candidateList();
    if (!list) {
        return false;
    }
    auto *bulk = list->toBulk();
    if (!bulk) {
        return false;
    }
    for (int i = 0; i < bulk->totalSize(); ++i) {
        if (bulk->candidateFromAll(i).text().toString() == kCloudPlaceholder) {
            return true;
        }
    }
    return false;
}
#endif // shared cloud-test helpers

#ifdef OXPINYIN_TEST_CLOUDPINYIN
// ON build: with Cloud Pinyin enabled and the real cloudpinyin module loaded,
// typing a full-pinyin buffer issues a cloud request and injects the cloud row
// at the configured slot (CloudPinyinIndex, 1-based). The row carries the "☁"
// placeholder from the instant the request is issued — this is the
// request-issued + row-appears-at-slot assertion, deterministic whether or not
// CI can reach a cloud endpoint. Selecting a FILLED cloud row runs the
// engine-independent workaround (OxpinyinState::cloudSelected: direct commit +
// reset, no engine training); that fill needs the async network result, so the
// positive commit is not asserted here.
void testCloudRowAtSlot(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        FCITX_ASSERT(oxpinyin);
        // The real cloudpinyin module must load in the ON job.
        auto *cloud = instance->addonManager().addon("cloudpinyin", true);
        FCITX_ASSERT(cloud);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        RawConfig config;
        config.setValueByPath("CloudPinyinEnabled", "True");
        config.setValueByPath("CloudPinyinIndex", "2");
        oxpinyin->setConfig(config);

        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto list = ic->inputPanel().candidateList();
        FCITX_ASSERT(list);
        auto *bulk = list->toBulk();
        FCITX_ASSERT(bulk);
        // Slot index 1 (CloudPinyinIndex 2, 1-based) is the cloud placeholder;
        // the engine candidates surround it.
        FCITX_ASSERT(bulk->totalSize() > 1);
        FCITX_ASSERT(bulk->candidateFromAll(1).text().toString() ==
                     kCloudPlaceholder);

        config.setValueByPath("CloudPinyinEnabled", "False");
        oxpinyin->setConfig(config);
        instance->deactivate();
    });
}

// Toggle off: with Cloud Pinyin disabled (the default), no cloud row appears
// and the pinyin candidate list is intact.
void testCloudToggleOff(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        RawConfig config;
        config.setValueByPath("CloudPinyinEnabled", "False");
        oxpinyin->setConfig(config);

        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        // Pinyin candidates present, cloud row absent.
        const auto list = ic->inputPanel().candidateList();
        FCITX_ASSERT(list && !list->empty());
        FCITX_ASSERT(!listHasCloudPlaceholder(ic));

        instance->deactivate();
    });
}

// Toggle hotkey: the module's toggleKey flips CloudPinyinEnabled live. From a
// disabled composition, pressing it enables cloud, is consumed (filtered), and
// the row appears at the slot. A distinct buffer keeps this a fresh cache miss
// so the row is still the placeholder, not a synchronously filled result.
void testCloudToggleHotkey(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        RawConfig config;
        config.setValueByPath("CloudPinyinEnabled", "False");
        oxpinyin->setConfig(config);

        for (const auto c : std::string("beijing")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(!listHasCloudPlaceholder(ic));

        // The module's default toggle key: consumed by the engine, and the
        // cloud row then appears at the slot.
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+Alt+Shift+C"), false));
        FCITX_ASSERT(listHasCloudPlaceholder(ic));

        config.setValueByPath("CloudPinyinEnabled", "False");
        oxpinyin->setConfig(config);
        instance->deactivate();
    });
}
#endif

#ifdef OXPINYIN_TEST_CLOUD_ABSENT
// Module-absent guard: this harness loads the cloud-enabled oxpinyin addon
// (ENABLE_CLOUDPINYIN ON) but NO cloudpinyin module -- it is absent from the
// --enable list and no cloudpinyin metadata dir is wired in. With
// CloudPinyinEnabled flipped ON, the engine's cloud path must still degrade
// gracefully: cloudpinyin() is null, maybeAddCloudCandidate() returns at that
// guard, no placeholder row is injected at the slot, and normal pinyin
// composition + commit are intact. This is the past-the-enabled-check guard
// that the default-disabled CloudPinyinEnabled never reaches on its own.
void testCloudModuleAbsent(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        FCITX_ASSERT(oxpinyin);
        // Pin this runner's precondition: cloudpinyin is unreachable here
        // (absent from --enable, no metadata dir wired in), so cloudpinyin() is
        // null and the guard-skip below is exercised for the intended reason --
        // not merely because an async fill has not landed yet.
        FCITX_ASSERT(!instance->addonManager().addon("cloudpinyin", true));
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        // Flip Cloud Pinyin on despite the module being absent.
        RawConfig config;
        config.setValueByPath("CloudPinyinEnabled", "True");
        config.setValueByPath("CloudPinyinIndex", "2");
        oxpinyin->setConfig(config);

        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        // Composition succeeds and the module-absent guard keeps the cloud row
        // out: were cloudpinyin() non-null with CloudPinyinEnabled on, the
        // placeholder row would sit at the slot from the first request.
        const auto list = ic->inputPanel().candidateList();
        FCITX_ASSERT(list && !list->empty());
        FCITX_ASSERT(!listHasCloudPlaceholder(ic));

        // Normal commit is unaffected: digit-selecting candidate 0 commits the
        // sentence the panel showed, with no cloud direct-commit path involved.
        const auto preedit = ic->inputPanel().preedit().toString();
        FCITX_ASSERT(!preedit.empty());
        testfrontend->call<ITestFrontend::pushCommitExpectation>(preedit);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("1"), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        config.setValueByPath("CloudPinyinEnabled", "False");
        oxpinyin->setConfig(config);
        instance->deactivate();
    });
}
#endif

} // namespace

int main() {
    // Include fcitx's system package-data directory so the isolated harness can
    // discover the real Spell module metadata and its built-in dictionary.
    // All system addons remain disabled unless explicitly named below.
    std::vector<std::string> dataDirs{
        TESTING_BINARY_DIR "/test",
        StandardPaths::fcitxPath("pkgdatadir").string()};
#ifdef OXPINYIN_CLOUDPINYIN_DATADIR
    // cloudpinyin.conf lives under the prefix where find_package(CloudPinyin)
    // resolved the module, which need not be fcitx5 core's pkgdatadir above
    // (a CMAKE_PREFIX_PATH / split-prefix install). Add it so the real
    // cloudpinyin module is always loadable in the ON build.
    dataDirs.emplace_back(OXPINYIN_CLOUDPINYIN_DATADIR);
#endif
    setupTestingEnvironment(TESTING_BINARY_DIR, {TESTING_BINARY_DIR "/src"},
                            dataDirs);
    char arg0[] = "testoxpinyin";
    char arg1[] = "--disable=all";
#ifdef OXPINYIN_TEST_NO_SPELL
    char arg2[] = "--enable=testim,testfrontend,oxpinyin";
#elif defined(OXPINYIN_TEST_CLOUDPINYIN)
    // The cloud tests drive the real cloudpinyin module, so it must be enabled
    // alongside spell and the harness addons (it stays on-demand; enabling only
    // makes it loadable).
    char arg2[] = "--enable=testim,testfrontend,oxpinyin,spell,cloudpinyin";
#else
    char arg2[] = "--enable=testim,testfrontend,oxpinyin,spell";
#endif
    char *argv[] = {arg0, arg1, arg2};
    fcitx::Log::setLogRule("default=5,oxpinyin=5");
    fcitx::Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);

    testLoadAndPassthrough(&instance);
    testOptionalModulesAbsent(&instance);
    testTypeCommit(&instance);
    testBackspace(&instance);
    testEscape(&instance);
    testCandidatesAndPassthrough(&instance);
    testAuxPreedit(&instance);
    testConfigApply(&instance);
#ifndef OXPINYIN_TEST_NO_SPELL
    testSpellCandidates(&instance);
    testSpellSpaceSelection(&instance);
#else
    testSpellUnavailable(&instance);
#endif
    testSchemeSwitchZhuyin(&instance);
    testPartialChoiceContinues(&instance);
    testBackspaceUnpins(&instance);
    testEscapeClearsPins(&instance);
    testPredictAfterCommit(&instance);
    testPredictChain(&instance);
    testPredictExitOnTyping(&instance);
    testPredictToggleOff(&instance);
    testPunctuationMapping(&instance);
    testPairedQuotes(&instance);
    testCommaAfterDigit(&instance);
    testUnmappedPunctPassthrough(&instance);
    testPunctuationMidComposition(&instance);
    testPunctuationDismissesPrediction(&instance);
#ifdef OXPINYIN_TEST_CLOUDPINYIN
    testCloudRowAtSlot(&instance);
    testCloudToggleOff(&instance);
    testCloudToggleHotkey(&instance);
#endif
#ifdef OXPINYIN_TEST_CLOUD_ABSENT
    testCloudModuleAbsent(&instance);
#endif

    instance.eventDispatcher().schedule([&instance]() { instance.exit(); });
    instance.exec();

    return 0;
}
