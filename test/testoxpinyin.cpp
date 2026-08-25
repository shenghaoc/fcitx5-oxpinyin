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
#include <fcitx/action.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterfacemanager.h>
#include <string>
#include <vector>

#include "oxpinyinconfig.h"
#include "punctuation_public.h"
#ifdef OXPINYIN_TEST_CLOUD_STUB
#include "stubhanzi.h"
#endif

using namespace fcitx;

namespace {

/*
 * The assertions pin fcitx WIRING only — which keys get filtered, what the
 * panel shows, what gets committed. Engine output correctness (candidate
 * quality, sentence text) is pinned by oxpinyin's oracle differentials,
 * not here: expectations are read back from the panel instead of being
 * hardcoded.
 */

// Punctuation is delegated to fcitx5-chinese-addons' shared punctuation
// module. Read the module's own verdict for a key: pair.first of
// getPunctuation is the punctuation mapped to the key (an empty pair means
// no mapping, or the module's global toggle is off). The mapping data is the
// module's, never this addon's — so expectations are read here, not
// hardcoded.
std::string delegatedPunctuation(Instance *instance, uint32_t unicode) {
    auto *punc = instance->addonManager().addon("punctuation", true);
    FCITX_ASSERT(punc);
    const auto &mapped =
        punc->call<IPunctuation::getPunctuation>("zh_CN", unicode);
    return mapped.first;
}

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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}

// Optional-modules invariant: chttrans/fullwidth (and cloudpinyin) REMAIN
// optional dependencies. With none of them enabled in this harness,
// activate() must run its guarded toggle-wiring without crashing, no toggle
// Action is present to add, and composition/commit still work. Punctuation
// is the exception: it is now a HARD dependency (delegated via
// getPunctuation), covered by testPunctuationDelegation in the runners that
// load it and testPunctuationHardDependency in the runner that does not.
// The conversion-modules runner (OXPINYIN_TEST_CONV) pins the modules
// PRESENT instead: their own toggle Actions appear and are not duplicated
// (testStatusToggleActions), so this absent-case invariant is not built
// there.
#ifndef OXPINYIN_TEST_CONV
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}
#endif // !OXPINYIN_TEST_CONV

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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}

// Status-bar toggles: the addon's OWN config switches (PredictWords,
// SpellEnabled) get SimpleActions shaped after fcitx5-chinese-addons'
// pinyin prediction toggle — registered once by name, Activated flips the
// config live and re-syncs the action display, and activate() adds each to
// the IM's InputMethod status group. The chttrans/fullwidth toggles are the
// conversion MODULES' own actions (PR #8 wiring): when those modules are
// enabled (OXPINYIN_TEST_CONV) they appear alongside, exactly once each,
// never re-created here.
void testStatusToggleActions(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);

        // The engine registered its actions once: lookups resolve to the
        // same Action pointers.
        auto *prediction = instance->userInterfaceManager().lookupAction(
            "oxpinyin-prediction");
        auto *spell =
            instance->userInterfaceManager().lookupAction("oxpinyin-spell");
        FCITX_ASSERT(prediction);
        FCITX_ASSERT(spell);
        FCITX_ASSERT(instance->userInterfaceManager().lookupAction(
                         "oxpinyin-prediction") == prediction);
        FCITX_ASSERT(instance->userInterfaceManager().lookupAction(
                         "oxpinyin-spell") == spell);

        // Explicit baseline (defaults): prediction off, spell on. The
        // action display follows the config even for an external setConfig.
        RawConfig config;
        config.setValueByPath("PredictWords", "False");
        config.setValueByPath("SpellEnabled", "True");
        oxpinyin->setConfig(config);
        FCITX_ASSERT(prediction->shortText(ic) == "Prediction Disabled");

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(ic) == "oxpinyin");

        // Status area, InputMethod group: the punctuation module's toggle
        // plus this addon's two — each exactly once. With the conversion
        // modules enabled (OXPINYIN_TEST_CONV), their own toggles join and
        // are likewise single.
        std::vector<std::string> expectedNames{
            "punctuation", "oxpinyin-prediction", "oxpinyin-spell"};
#ifdef OXPINYIN_TEST_CONV
        expectedNames.emplace_back("chttrans");
        expectedNames.emplace_back("fullwidth");
#endif
        const auto actions = ic->statusArea().actions(StatusGroup::InputMethod);
        FCITX_ASSERT(actions.size() == expectedNames.size());
        for (const auto &name : expectedNames) {
            FCITX_ASSERT(std::count_if(actions.begin(), actions.end(),
                                       [&name](Action *a) {
                                           return a->name() == name;
                                       }) == 1);
        }

        // IM off/on on the SAME context re-runs activate(); the InputMethod
        // group is auto-cleared before it, so each action still appears
        // exactly once (nothing accumulates across re-activations). Only
        // one context exists yet: instance->deactivate() touches only the
        // most recent one, so the trigger below must be its re-activation.
        instance->deactivate();
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(ic) == "oxpinyin");
        const auto reappeared =
            ic->statusArea().actions(StatusGroup::InputMethod);
        FCITX_ASSERT(reappeared.size() == expectedNames.size());
        for (const auto &name : expectedNames) {
            FCITX_ASSERT(std::count_if(reappeared.begin(), reappeared.end(),
                                       [&name](Action *a) {
                                           return a->name() == name;
                                       }) == 1);
        }

        // A second, not-yet-activated context has an empty InputMethod
        // group: engine actions are added per context only when THIS IM
        // activates there — no leak across input contexts. It is activated
        // right away and reused for the cross-context sweep assertions.
        auto uuid2 =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic2 = instance->inputContextManager().findByUUID(uuid2);
        FCITX_ASSERT(
            ic2->statusArea().actions(StatusGroup::InputMethod).empty());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid2, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(ic2) == "oxpinyin");
        const auto actions2 =
            ic2->statusArea().actions(StatusGroup::InputMethod);
        FCITX_ASSERT(actions2.size() == expectedNames.size());
        for (const auto &name : expectedNames) {
            FCITX_ASSERT(std::count_if(actions2.begin(), actions2.end(),
                                       [&name](Action *a) {
                                           return a->name() == name;
                                       }) == 1);
        }

        // Prediction toggle: activating flips the config and the display.
        prediction->activate(ic);
        FCITX_ASSERT(prediction->shortText(ic) == "Prediction Enabled");
        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto preedit = ic->inputPanel().preedit().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(preedit);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Return"), false));

        // Prediction on: the mode is in effect (list, no composing preedit).
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(ic->inputPanel().candidateList() &&
                     !ic->inputPanel().candidateList()->empty());

        // A SECOND active context is left predicting too (the config is
        // engine-global; the state is per-context).
        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid2, Key(static_cast<KeySym>(c)), false));
        }
        const auto preedit2 = ic2->inputPanel().preedit().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(preedit2);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid2, Key("Return"), false));
        FCITX_ASSERT(ic2->inputPanel().candidateList() &&
                     !ic2->inputPanel().candidateList()->empty());

        // Toggle off while prediction lists are shown: the lists are
        // dismissed at once EVERYWHERE — the sweep covers every active
        // oxpinyin context, so the mode's UI does not linger anywhere.
        prediction->activate(ic);
        FCITX_ASSERT(prediction->shortText(ic) == "Prediction Disabled");
        FCITX_ASSERT(!ic->inputPanel().candidateList());
        FCITX_ASSERT(!ic2->inputPanel().candidateList());
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(ic2->inputPanel().preedit().toString().empty());

        // Prediction off: a commit shows no prediction list.
        for (const auto c : std::string("nihao")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto preeditOff = ic->inputPanel().preedit().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(preeditOff);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Return"), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        // Spell toggle: default on. The lowercase English word occupies
        // slot 1 (after the first pinyin candidate) — the existing spell
        // suite pins the word; here it pins the ACTION flipping live.
        FCITX_ASSERT(spell->shortText(ic) == "Spell Enabled");
#ifndef OXPINYIN_TEST_NO_SPELL
        // Both active contexts compose the same English-looking buffer; the
        // Spell row sits at slot 1 in each.
        for (const auto c : std::string("rhythm")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        for (const auto c : std::string("rhythm")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid2, Key(static_cast<KeySym>(c)), false));
        }
        const auto list = ic->inputPanel().candidateList();
        FCITX_ASSERT(list && list->size() > 1);
        FCITX_ASSERT(list->candidate(1).text().toString() == "rhythm");
        const auto list2 = ic2->inputPanel().candidateList();
        FCITX_ASSERT(list2 && list2->size() > 1);
        FCITX_ASSERT(list2->candidate(1).text().toString() == "rhythm");
        auto *bulkBefore = list->toBulk();
        FCITX_ASSERT(bulkBefore);
        const auto beforeTotal = bulkBefore->totalSize();
        // Toggle off with the compositions live: the Spell row disappears
        // at once in EVERY active context (the rest of each pinyin list is
        // untouched).
        spell->activate(ic);
        FCITX_ASSERT(spell->shortText(ic) == "Spell Disabled");
        for (InputContext *ctx : {ic, ic2}) {
            const auto spellless = ctx->inputPanel().candidateList();
            auto *bulkAfter = spellless ? spellless->toBulk() : nullptr;
            FCITX_ASSERT(bulkAfter &&
                         bulkAfter->totalSize() == beforeTotal - 1);
            for (int i = 0; i < bulkAfter->totalSize(); ++i) {
                FCITX_ASSERT(bulkAfter->candidateFromAll(i).text().toString() !=
                             "rhythm");
            }
        }
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid2, Key("Escape"), false));
        // Back to the default state for the rest of the suite.
        spell->activate(ic);
        FCITX_ASSERT(spell->shortText(ic) == "Spell Enabled");
#else
        // Spell module absent: the toggle still flips (the config option is
        // the addon's; only the candidates need the module).
        spell->activate(ic);
        FCITX_ASSERT(spell->shortText(ic) == "Spell Disabled");
        spell->activate(ic);
        FCITX_ASSERT(spell->shortText(ic) == "Spell Enabled");
#endif

        instance->deactivate();
        // Destroy the test input contexts so the engine state (and its
        // pinyin instances) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid2);
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}

// Punctuation is DELEGATED to fcitx5-chinese-addons' shared punctuation
// module via getPunctuation — a HARD dependency, wired the way
// fcitx5-chinese-addons' own pinyin does it. There is no internal mapping
// table anymore: whatever the module maps, the engine commits and consumes;
// what the module does not map passes through to the client. Expectations
// are read from the module itself (delegatedPunctuation), because the
// mapping data belongs to the module, not to this addon.
//
// Behavior note: this replaces the ibus-libpinyin FallbackEditor port. The
// module's stateless getPunctuation verdict is authoritative — the port's
// paired-quote alternation and comma/period-after-digit rule no longer
// apply (repeated quote keys commit the same mapped half each time).
void testPunctuationDelegation(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        // Hard dependency wired fcitx5-native: activate() loaded the module,
        // and its toggle Action is registered for the status area (the same
        // wiring chinese-addons' pinyin adds).
        FCITX_ASSERT(
            instance->userInterfaceManager().lookupAction("punctuation"));

        // A representative sweep: mapped keys, keys the module maps to
        // themselves (still consumed), and unmapped keys. The quote keys
        // appear twice: the delegation is stateless, so repeated presses
        // commit the same mapped half.
        // clang-format off
        const KeySym keys[] = {
            FcitxKey_exclam,       FcitxKey_comma,      FcitxKey_period,
            FcitxKey_question,     FcitxKey_colon,      FcitxKey_semicolon,
            FcitxKey_less,         FcitxKey_greater,    FcitxKey_parenleft,
            FcitxKey_parenright,   FcitxKey_bracketleft,
            FcitxKey_bracketright, FcitxKey_braceleft,  FcitxKey_braceright,
            FcitxKey_backslash,    FcitxKey_grave,      FcitxKey_asciitilde,
            FcitxKey_dollar,       FcitxKey_asciicircum,
            FcitxKey_underscore,   FcitxKey_apostrophe, FcitxKey_apostrophe,
            FcitxKey_quotedbl,     FcitxKey_quotedbl,   FcitxKey_at,
            FcitxKey_numbersign,   FcitxKey_slash,      FcitxKey_plus,
        };
        // clang-format on
        for (const auto sym : keys) {
            const auto expected =
                delegatedPunctuation(instance, static_cast<uint32_t>(sym));
            if (expected.empty()) {
                // No mapping (or the module's toggle is off): the key stays
                // with the client.
                FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
                    uuid, Key(sym), false));
            } else {
                testfrontend->call<ITestFrontend::pushCommitExpectation>(
                    expected);
                FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                    uuid, Key(sym), false));
            }
        }
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());

        instance->deactivate();
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}

// Typing punctuation mid-composition commits the sentence first, then
// commits the punctuation delegated by the module.
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

        // '!' mid-composition: commits the sentence, then whatever the
        // punctuation module maps '!' to.
        const auto punct = delegatedPunctuation(instance, FcitxKey_exclam);
        FCITX_ASSERT(!punct.empty());
        testfrontend->call<ITestFrontend::pushCommitExpectation>(preedit);
        testfrontend->call<ITestFrontend::pushCommitExpectation>(punct);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_exclam), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());

        instance->deactivate();
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}

// Finding-D: punctuation typed while a prediction list is shown must still
// commit. The dismissal path re-dispatches EVERY unconsumed key through the
// normal path — where a punctuation key now hits the delegated getPunctuation
// instead of being swallowed.
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

        // '!' is consumed and commits what the punctuation module maps it
        // to — not dropped by the dismissal.
        const auto punct = delegatedPunctuation(instance, FcitxKey_exclam);
        FCITX_ASSERT(!punct.empty());
        testfrontend->call<ITestFrontend::pushCommitExpectation>(punct);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_exclam), false));

        // Prediction is gone and nothing is composing.
        FCITX_ASSERT(!ic->inputPanel().candidateList());
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(ic->inputPanel().clientPreedit().toString().empty());

        config.setValueByPath("PredictWords", "False");
        oxpinyin->setConfig(config);
        instance->deactivate();
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}

#if defined(OXPINYIN_TEST_CLOUDPINYIN) ||                                      \
    defined(OXPINYIN_TEST_CLOUD_ABSENT) || defined(OXPINYIN_TEST_CLOUD_STUB)
// The "☁" (U+2601) unicode char cloudpinyin shows as the loading placeholder
// until an async result fills the row. Shared by the module-present runner,
// the module-absent guard runner (asserts the row never appears), and the
// hermetic stub runner (asserts the row shows the filled hanzi instead).
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}
#endif

#ifdef OXPINYIN_TEST_CLOUD_STUB
// Hermetic cloud-select commit: this runner loads the in-tree STUB cloudpinyin
// addon (not the real module), which answers every request SYNCHRONOUSLY with
// the canned kStubCloudHanzi -- a cache-hit model. So the reused
// CloudPinyinCandidateWord fills in its ctor (no network), the row at
// CloudPinyinIndex-1 carries the canned hanzi (NOT the placeholder), and
// selecting that row routes through the candidate's select callback to
// OxpinyinState::cloudSelected, which commits selected+hanzi directly and
// resets with no engine training. This closes the gap testCloudRowAtSlot's
// comment flags: the positive commit IS asserted here.
void testCloudStubSelectCommit(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        FCITX_ASSERT(oxpinyin);
        // The stub cloudpinyin addon loads in this harness.
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
        // CloudPinyinIndex 2 (1-based) => slot index 1: the stub-filled row,
        // carrying the canned hanzi, NOT the placeholder. Proof the stub
        // filled synchronously in the candidate ctor.
        FCITX_ASSERT(bulk->totalSize() > 1);
        FCITX_ASSERT(bulk->candidateFromAll(1).text().toString() ==
                     kStubCloudHanzi);
        FCITX_ASSERT(bulk->candidateFromAll(1).text().toString() !=
                     kCloudPlaceholder);
        // No stale placeholder row anywhere: the stub filled before the engine
        // inserted, so every candidate carries real text.
        FCITX_ASSERT(!listHasCloudPlaceholder(ic));

        // Select the filled cloud row (slot index 1 => digit "2"): routes to
        // cloudSelected, which commits selected+hanzi and resets. The buffer
        // was unpinned/full, so selected is empty and the commit is exactly the
        // canned hanzi -- no engine choose/train/remember side effect.
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            kStubCloudHanzi);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("2"), false));
        // Composition reset: no preedit, no candidate list.
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        config.setValueByPath("CloudPinyinEnabled", "False");
        oxpinyin->setConfig(config);
        instance->deactivate();
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}
#endif

#if defined(OXPINYIN_TEST_LUA) || defined(OXPINYIN_TEST_LUA_ABSENT)
// The canned imeapi extension (test/lua/imeapi/extensions/oxpinyintest.lua)
// answers the nihao candidate 你好 with the fixed marker 你好世界. These
// constants are shared by the module-present runner (asserts the marker is
// injected and selectable) and the module-absent guard (asserts it never
// appears).
constexpr char kLuaTrigger[] = "\xe4\xbd\xa0\xe5\xa5\xbd"; // 你好
constexpr char kLuaCannedCandidate[] =
    "\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c"; // 你好世界

// Returns the index of `text` among ALL candidates (across pages), or -1.
int luaCandidateIndexOf(InputContext *ic, const std::string &text) {
    const auto list = ic->inputPanel().candidateList();
    if (!list) {
        return -1;
    }
    auto *bulk = list->toBulk();
    if (!bulk) {
        return -1;
    }
    for (int i = 0; i < bulk->totalSize(); ++i) {
        if (bulk->candidateFromAll(i).text().toString() == text) {
            return i;
        }
    }
    return -1;
}
#endif // shared lua-test helpers

#ifdef OXPINYIN_TEST_LUA
// ON build: with fcitx5-lua's luaaddonloader + imeapi loaded and the canned
// extension active, typing nihao triggers imeapi's candidateTrigger on the 你好
// row and injects the canned 你好世界 row immediately after it. The engine's
// own candidates are still present around it. This is the trigger->inject
// assertion, fully deterministic (canned extension, no interpreter
// assumptions).
void testLuaCandidateInjection(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        // The real imeapi module must load in the ON runner.
        auto *imeapi = instance->addonManager().addon("imeapi", true);
        FCITX_ASSERT(imeapi);
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
        const auto list = ic->inputPanel().candidateList();
        FCITX_ASSERT(list);

        // The trigger candidate 你好 is present, and the canned lua row lands
        // IMMEDIATELY after it (the injection shape from chinese-addons).
        const int triggerIdx = luaCandidateIndexOf(ic, kLuaTrigger);
        FCITX_ASSERT(triggerIdx >= 0);
        const int luaIdx = luaCandidateIndexOf(ic, kLuaCannedCandidate);
        FCITX_ASSERT(luaIdx == triggerIdx + 1);

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));
        instance->deactivate();
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}

// Selecting the injected lua row runs the engine-independent workaround
// (OxpinyinState::luaSelected): it commits the canned lua text DIRECTLY and
// resets — never pinyin_choose_candidate, train, or constraints. The commit
// is exactly the canned string, and the composition is gone afterwards. That
// the committed text is the lua marker (not any pinyin lattice sentence) is
// the proof that the select path is engine-independent.
void testLuaSelectCommitEngineIndependent(Instance *instance) {
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
        const auto list = ic->inputPanel().candidateList();
        FCITX_ASSERT(list);
        const auto common =
            std::dynamic_pointer_cast<CommonCandidateList>(list);
        FCITX_ASSERT(common);

        // Locate the lua row; it must be reachable on the first page so a
        // selection key can address it.
        const int luaIdx = luaCandidateIndexOf(ic, kLuaCannedCandidate);
        FCITX_ASSERT(luaIdx >= 0);
        FCITX_ASSERT(luaIdx < common->pageSize());

        // Select it via its digit key (index+1 on the current page). The
        // commit expectation is EXACTLY the canned lua marker — the direct
        // commit, not a pinyin sentence.
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            kLuaCannedCandidate);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(static_cast<KeySym>(FcitxKey_1 + luaIdx)), false));

        // Reset after the direct commit: no preedit, no candidate list.
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        instance->deactivate();
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}
#endif

#ifdef OXPINYIN_TEST_LUA_ABSENT
// Module-absent guard: this harness loads the lua-enabled oxpinyin addon
// (ENABLE_LUA ON) but NO fcitx5-lua modules — luaaddonloader/imeapi are off
// the --enable list. imeapi() is null, so no lua rows are injected even
// though the trigger candidate 你好 appears, and normal pinyin composition +
// commit are intact. This is the guard that keeps the addon fully functional
// without fcitx5-lua installed.
void testLuaModuleAbsent(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        FCITX_ASSERT(oxpinyin);
        // Pin this runner's precondition: imeapi is unreachable here, so the
        // luaCandidateTrigger guard-skip below is exercised for the intended
        // reason.
        FCITX_ASSERT(!instance->addonManager().addon("imeapi", true));
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
        // Composition succeeds and the module-absent guard keeps the canned
        // lua row out: were imeapi() non-null, the trigger candidate would
        // have produced it. (The trigger candidate's own presence is
        // engine-data dependent, so it is not asserted here; the guard is
        // pinned by the imeapi() null check above.)
        const auto list = ic->inputPanel().candidateList();
        FCITX_ASSERT(list && !list->empty());
        FCITX_ASSERT(luaCandidateIndexOf(ic, kLuaCannedCandidate) < 0);

        // Normal commit is unaffected: digit-selecting candidate 0 commits the
        // sentence the panel showed, with no lua direct-commit path involved.
        const auto preedit = ic->inputPanel().preedit().toString();
        FCITX_ASSERT(!preedit.empty());
        testfrontend->call<ITestFrontend::pushCommitExpectation>(preedit);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("1"), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        instance->deactivate();
        // Destroy the test input context so the engine state (and its
        // pinyin instance) is freed before LSan checks at exit.
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
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
#ifdef OXPINYIN_LUA_DATADIR
    // Same split-prefix story for fcitx5-lua: its luaaddonloader/imeapi addon
    // confs and lua/imeapi/imeapi.lua live under the prefix where
    // find_package(Fcitx5ModuleLuaAddonLoader) resolved the module. Add it so
    // imeapi is always loadable in the ENABLE_LUA build.
    dataDirs.emplace_back(OXPINYIN_LUA_DATADIR);
#endif
#ifdef OXPINYIN_TEST_CLOUD_STUB
    // Stub cloudpinyin: add the stub's build dir to BOTH the addon dirs (so its
    // cloudpinyinstub.so is located) and the data dirs (so its
    // addon/cloudpinyin.conf is discovered). Discovery is first-found-wins
    // (AddonManager skips a .conf whose filename is already seen), so the stub
    // dir MUST come before the system pkgdatadir -- which holds a real
    // cloudpinyin.conf (chinese-addons is installed for the ON build) -- or the
    // real module would win and the stub never loads. Insert at index 1, ahead
    // of the system pkgdatadir (index 0 is build/test, which has no
    // cloudpinyin.conf). Only this runner does this, so the real-cloudpinyin
    // and absent runners stay isolated.
    dataDirs.insert(dataDirs.begin() + 1,
                    TESTING_BINARY_DIR "/test/cloudpinyin-stub");
    setupTestingEnvironment(TESTING_BINARY_DIR,
                            {TESTING_BINARY_DIR "/src",
                             TESTING_BINARY_DIR "/test/cloudpinyin-stub"},
                            dataDirs);
#else
    setupTestingEnvironment(TESTING_BINARY_DIR, {TESTING_BINARY_DIR "/src"},
                            dataDirs);
#endif
    char arg0[] = "testoxpinyin";
    char arg1[] = "--disable=all";
#ifdef OXPINYIN_TEST_NO_SPELL
    // Deliberately WITHOUT spell (the lazy Spell-addon pointer stays null);
    // the punctuation module stays enabled — it is a hard dependency of
    // the engine, in every runner that loads the engine.
    char arg2[] = "--enable=testim,testfrontend,oxpinyin,punctuation";
#elif defined(OXPINYIN_TEST_CLOUDPINYIN) && defined(OXPINYIN_TEST_LUA)
    // Both optional modules under test in one runner: the real cloudpinyin
    // module plus fcitx5-lua's luaaddonloader + imeapi. Enabling only makes
    // them loadable; the punctuation module is a hard dependency of the
    // engine, so it is enabled in every module-present runner.
    char arg2[] = "--enable=testim,testfrontend,oxpinyin,spell,punctuation,"
                  "cloudpinyin,luaaddonloader,imeapi";
#elif defined(OXPINYIN_TEST_CLOUDPINYIN)
    // The cloud tests drive the real cloudpinyin module, so it must be enabled
    // alongside spell and the harness addons (it stays on-demand; enabling only
    // makes it loadable). The punctuation module is a hard dependency of the
    // engine, so it is enabled in every module-present runner.
    char arg2[] =
        "--enable=testim,testfrontend,oxpinyin,spell,punctuation,cloudpinyin";
#elif defined(OXPINYIN_TEST_CLOUD_STUB)
    // The stub runner loads the stub cloudpinyin addon in place of the real
    // module; spell stays on so the full normal-composition suite is reused.
    char arg2[] =
        "--enable=testim,testfrontend,oxpinyin,spell,punctuation,cloudpinyin";
#elif defined(OXPINYIN_TEST_LUA)
    // The lua tests drive fcitx5-lua's real modules: luaaddonloader (the
    // shared-library loader for Type=Lua addons) and imeapi (the Lua IME API
    // module that owns the interpreter and loads imeapi extensions). Both
    // must be enabled; imeapi hard-depends on luaaddonloader. Spell stays on
    // so the full normal-composition suite is reused.
    char arg2[] = "--enable=testim,testfrontend,oxpinyin,spell,punctuation,"
                  "luaaddonloader,imeapi";
#elif defined(OXPINYIN_TEST_CONV)
    // Conversion-modules runner: the real chinese-addons chttrans and
    // fullwidth modules are ENABLED, so activate() adds their toggle
    // Actions next to the punctuation module's and this addon's own. Both
    // conversions are dormant by default (fullwidth off; chttrans enables
    // per-IM only after its toggle), so the full normal-composition suite
    // runs unchanged; testStatusToggleActions pins the richer status-area
    // set.
    char arg2[] = "--enable=testim,testfrontend,oxpinyin,spell,punctuation,"
                  "chttrans,fullwidth";
#else
    char arg2[] = "--enable=testim,testfrontend,oxpinyin,spell,punctuation";
#endif
    char *argv[] = {arg0, arg1, arg2};
    fcitx::Log::setLogRule("default=5,oxpinyin=5");
    fcitx::Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);

    testLoadAndPassthrough(&instance);
#ifndef OXPINYIN_TEST_CONV
    testOptionalModulesAbsent(&instance);
#endif
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
    testStatusToggleActions(&instance);
    testPunctuationDelegation(&instance);
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
#ifdef OXPINYIN_TEST_CLOUD_STUB
    testCloudStubSelectCommit(&instance);
#endif
#ifdef OXPINYIN_TEST_LUA
    testLuaCandidateInjection(&instance);
    testLuaSelectCommitEngineIndependent(&instance);
#endif
#ifdef OXPINYIN_TEST_LUA_ABSENT
    testLuaModuleAbsent(&instance);
#endif

    instance.eventDispatcher().schedule([&instance]() { instance.exit(); });
    instance.exec();

    return 0;
}
