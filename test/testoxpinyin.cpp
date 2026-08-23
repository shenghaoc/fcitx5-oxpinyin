/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "testdir.h"
#include "testfrontend_public.h"

#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/testing.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>

#include "oxpinyinconfig.h"

#ifdef OXPINYIN_ENGLISH_INPUT_MODE
#include <cstdlib>
#include <filesystem>
#include <vector>

#include <unistd.h>

#include <fcitx-utils/standardpaths.h>

#include <sqlite3.h>
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

        // '.' mid-composition: commits the sentence, then the '。'.
        // ('!' is no longer usable here: with English input mode enabled
        // it is a mode-switch symbol, matching upstream — covered by
        // testEnglishSymbolSwitchMidComposition.)
        testfrontend->call<ITestFrontend::pushCommitExpectation>(preedit);
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            "\xe3\x80\x82"); // 。
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_period), false));
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

#ifdef OXPINYIN_ENGLISH_INPUT_MODE

// Reference listWords: the upstream SQL (byte-identical to the addon's)
// run against the same database files the addon opened.  Valid until the
// first English training in this process — after that the addon's
// in-memory user copy is ahead of the file.
std::vector<std::string> englishReferenceWords(const std::string &prefix) {
    const char *sysDb = std::getenv("OXPINYIN_ENGLISH_SYSTEM_DB");
    FCITX_ASSERT(sysDb && *sysDb);

    std::filesystem::path userDir;
    if (const char *env = std::getenv("OXPINYIN_USER_DATA_DIR"); env && *env) {
        userDir = env;
    } else {
        userDir =
            StandardPaths::global().userDirectory(StandardPathsType::PkgData) /
            "oxpinyin";
    }
    const auto userDb = userDir / "english-user.db";
    // Opening the addon created the user database file.
    FCITX_ASSERT(std::filesystem::is_regular_file(userDb));

    sqlite3 *db = nullptr;
    FCITX_ASSERT(sqlite3_open_v2(sysDb, &db, SQLITE_OPEN_READONLY, nullptr) ==
                 SQLITE_OK);
    const std::string attach =
        "ATTACH DATABASE '" + userDb.string() + "' AS userdb;";
    FCITX_ASSERT(sqlite3_exec(db, attach.c_str(), nullptr, nullptr, nullptr) ==
                 SQLITE_OK);
    const std::string sql =
        "SELECT word FROM ( "
        "SELECT * FROM english UNION ALL SELECT * FROM userdb.english) "
        " WHERE word GLOB \"" +
        prefix + "*\" GROUP BY word ORDER BY SUM(freq) DESC;";
    sqlite3_stmt *stmt = nullptr;
    FCITX_ASSERT(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) ==
                 SQLITE_OK);
    std::vector<std::string> words;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        words.emplace_back(
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return words;
}

// English mode: v opens the editor (aux line, no candidates yet), a
// prefix lists words whose set and order matches the reference SQL
// exactly.  Runs before any English training so the in-memory user copy
// still equals the file the reference query reads.
void testEnglishFrequencyOrder(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("v"), false));
        const auto helpAux = ic->inputPanel().auxUp().toString();
        FCITX_ASSERT(!helpAux.empty() && helpAux.front() == 'v');
        FCITX_ASSERT(!ic->inputPanel().candidateList());
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(ic->inputPanel().clientPreedit().toString().empty());

        for (const auto c : std::string("th")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(ic->inputPanel().auxUp().toString() == "v th");

        const auto list = ic->inputPanel().candidateList();
        FCITX_ASSERT(list && !list->empty());
        const auto bulk = std::dynamic_pointer_cast<CommonCandidateList>(list);
        FCITX_ASSERT(bulk);

        const auto reference = englishReferenceWords("th");
        FCITX_ASSERT(!reference.empty());
        FCITX_ASSERT(static_cast<size_t>(bulk->totalSize()) ==
                     reference.size());
        for (size_t i = 0; i < reference.size(); ++i) {
            FCITX_ASSERT(
                bulk->candidateFromAll(static_cast<int>(i)).text().toString() ==
                reference[i]);
        }

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().empty());

        instance->deactivate();
    });
}

// Digit selection commits the labeled candidate and drops back to pinyin.
void testEnglishTypeCommitAndExit(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        for (const auto c : std::string("vth")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto list = ic->inputPanel().candidateList();
        FCITX_ASSERT(list && !list->empty());
        const auto first = list->candidate(0).text().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(first);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("1"), false));

        // Exited English mode: aux gone, no list.
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().empty());
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        // Back in the pinyin path: a normal composition works.
        for (const auto c : std::string("ni")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(!ic->inputPanel().preedit().toString().empty() ||
                     !ic->inputPanel().clientPreedit().toString().empty());
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().empty());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        instance->deactivate();
    });
}

// True when any candidate on the panel equals the word.
bool englishListContains(InputContext *ic, const std::string &word) {
    const auto bulk = std::dynamic_pointer_cast<CommonCandidateList>(
        ic->inputPanel().candidateList());
    if (!bulk) {
        return false;
    }
    for (int i = 0; i < bulk->totalSize(); ++i) {
        if (bulk->candidateFromAll(i).text().toString() == word) {
            return true;
        }
    }
    return false;
}

// Enter commits the raw typed word and learns it: after the reset, the
// prefix lists the learned word from the user database.  The word is
// pid-unique (letters only — digits are label keys) so reruns against a
// persisted user database stay stable.
void testEnglishEnterLearnsAcrossReset(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        std::string word = "zzzq";
        for (pid_t p = getpid(); p > 0; p /= 10) {
            word += static_cast<char>('a' + (p % 10));
        }

        // No system word starts with zzz: this word is not known yet.
        for (const auto c : "v" + word) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(ic->inputPanel().auxUp().toString() == "v " + word);
        FCITX_ASSERT(!englishListContains(ic, word));

        testfrontend->call<ITestFrontend::pushCommitExpectation>(word);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Return"), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().empty());

        // The learned word survives the editor reset.
        for (const auto c : std::string("vzzz")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(englishListContains(ic, word));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        instance->deactivate();
    });
}

// Space commits the lookup-table cursor's candidate; Down moves it.
void testEnglishSpaceCursorCommit(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        // Space with the cursor at its start commits the first candidate.
        for (const auto c : std::string("vth")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        auto list = ic->inputPanel().candidateList();
        FCITX_ASSERT(list && !list->empty());
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            list->candidate(0).text().toString());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        // Down moves the cursor; Space commits the second candidate.
        for (const auto c : std::string("vth")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Down"), false));
        list = ic->inputPanel().candidateList();
        FCITX_ASSERT(list && list->size() > 1);
        FCITX_ASSERT(list->toCursorMovable());
        FCITX_ASSERT(list->cursorIndex() == 1);
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            list->candidate(1).text().toString());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        instance->deactivate();
    });
}

// Escape leaves English mode; backspacing the text away leaves it too,
// after which backspace belongs to the client again.
void testEnglishEscapeBackspaceExit(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("v"), false));
        FCITX_ASSERT(!ic->inputPanel().auxUp().toString().empty());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().empty());

        for (const auto c : std::string("vx")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_BackSpace), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().front() == 'v');
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_BackSpace), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().empty());
        // English mode is gone: the next backspace is the client's.
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_BackSpace), false));

        instance->deactivate();
    });
}

// An uppercase letter on an empty buffer enters English mode with a "v"
// seed (full pinyin, upstream's A–Z rule); Enter commits the raw word.
void testEnglishUppercaseEntry(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("H"), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString() == "v H");
        testfrontend->call<ITestFrontend::pushCommitExpectation>("H");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Return"), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().empty());

        instance->deactivate();
    });
}

// An English symbol mid-composition switches to English mode, carrying
// the typed pinyin (no commit); the same symbol with no composition still
// goes to the punctuation module.
void testEnglishSymbolSwitchMidComposition(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        // '!' mid-composition: switch, not commit (an unexpected commit
        // would fail the frontend's expectation check).
        for (const auto c : std::string("ni")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_exclam), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString() == "v ni!");
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(ic->inputPanel().clientPreedit().toString().empty());
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));
        // Nothing composing after the escape: backspace passes through.
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_BackSpace), false));

        // ';' is a switch symbol for full pinyin.
        for (const auto c : std::string("ni")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_semicolon), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString() == "v ni;");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        // With no composition, '!' is ordinary punctuation.
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            "\xef\xbc\x81"); // ！
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_exclam), false));

        instance->deactivate();
    });
}

// Paging: Page_Down/Page_Up page, minus/equal page (upstream default),
// comma does not (default off) and is not typed into the word either.
void testEnglishPaging(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        for (const auto c : std::string("va")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(ic->inputPanel().auxUp().toString() == "v a");
        auto list = ic->inputPanel().candidateList();
        FCITX_ASSERT(list && !list->empty());
        {
            const auto bulk =
                std::dynamic_pointer_cast<CommonCandidateList>(list);
            FCITX_ASSERT(bulk && bulk->totalSize() > bulk->pageSize());
        }
        const auto page0 = list->candidate(0).text().toString();

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Page_Down"), false));
        const auto page1 =
            ic->inputPanel().candidateList()->candidate(0).text().toString();
        FCITX_ASSERT(page1 != page0);

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Page_Up"), false));
        FCITX_ASSERT(
            ic->inputPanel().candidateList()->candidate(0).text().toString() ==
            page0);

        // Equal/minus page too (upstream minus-equal-page default true).
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_equal), false));
        FCITX_ASSERT(
            ic->inputPanel().candidateList()->candidate(0).text().toString() ==
            page1);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_minus), false));
        FCITX_ASSERT(
            ic->inputPanel().candidateList()->candidate(0).text().toString() ==
            page0);

        // Comma neither pages (comma-period-page default false) nor
        // inserts (not an English symbol): a consumed no-op.
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_comma), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString() == "v a");
        FCITX_ASSERT(
            ic->inputPanel().candidateList()->candidate(0).text().toString() ==
            page0);

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        instance->deactivate();
    });
}

// EnglishInputMode=False disables every trigger: v never opens the aux
// line, and mid-composition '!' commits sentence + punctuation again.
void testEnglishConfigToggle(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        RawConfig config;
        config.setValueByPath("EnglishInputMode", "False");
        oxpinyin->setConfig(config);

        // 'v' goes to the pinyin path (whether it composes or passes
        // through is the engine's call); the English aux never appears.
        const bool consumed = testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("v"), false);
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().empty());
        if (consumed) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key("Escape"), false));
        }

        // Mid-composition '!' is punctuation again.
        for (const auto c : std::string("ni")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        const auto preedit = ic->inputPanel().preedit().toString();
        FCITX_ASSERT(!preedit.empty());
        testfrontend->call<ITestFrontend::pushCommitExpectation>(preedit);
        testfrontend->call<ITestFrontend::pushCommitExpectation>(
            "\xef\xbc\x81"); // ！
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_exclam), false));

        config.setValueByPath("EnglishInputMode", "True");
        oxpinyin->setConfig(config);
        instance->deactivate();
    });
}

// Double pinyin: 'v' is a composition key (no English), 'V' enters
// English mode, and the apostrophe switches mid-composition with a "V"
// prefix (upstream's double-pinyin trigger set).
void testEnglishDoublePinyinTrigger(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));

        RawConfig config;
        config.setValueByPath("InputScheme", "Natural Code (ZRM)");
        oxpinyin->setConfig(config);

        const bool consumed = testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("v"), false);
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().empty());
        if (consumed) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key("Escape"), false));
        }

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("V"), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString().front() == 'V');
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        for (const auto c : std::string("ni")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_apostrophe), false));
        FCITX_ASSERT(ic->inputPanel().auxUp().toString() == "V ni'");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Escape"), false));

        config.setValueByPath("InputScheme", "Full Pinyin");
        oxpinyin->setConfig(config);
        instance->deactivate();
    });
}

#endif // OXPINYIN_ENGLISH_INPUT_MODE

} // namespace

int main() {
    // No pkgdatadir append: that was only ever needed to reach
    // chinese-addons' punctuation.conf. setupTestingEnvironment already
    // adds fcitxPath("addondir") and fcitxPath("pkgdatadir", "testing")
    // itself, which is where the TestFrontend addon lives.
    setupTestingEnvironment(TESTING_BINARY_DIR, {TESTING_BINARY_DIR "/src"},
                            {TESTING_BINARY_DIR "/test"});
    char arg0[] = "testoxpinyin";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testim,testfrontend,oxpinyin";
    char *argv[] = {arg0, arg1, arg2};
    fcitx::Log::setLogRule("default=5,oxpinyin=5");
    fcitx::Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);

    testLoadAndPassthrough(&instance);
    testTypeCommit(&instance);
    testBackspace(&instance);
    testEscape(&instance);
    testCandidatesAndPassthrough(&instance);
    testAuxPreedit(&instance);
    testConfigApply(&instance);
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
#ifdef OXPINYIN_ENGLISH_INPUT_MODE
    // testEnglishFrequencyOrder must run before any test that trains an
    // English word (see its comment).
    testEnglishFrequencyOrder(&instance);
    testEnglishTypeCommitAndExit(&instance);
    testEnglishEnterLearnsAcrossReset(&instance);
    testEnglishSpaceCursorCommit(&instance);
    testEnglishEscapeBackspaceExit(&instance);
    testEnglishUppercaseEntry(&instance);
    testEnglishSymbolSwitchMidComposition(&instance);
    testEnglishPaging(&instance);
    testEnglishConfigToggle(&instance);
    testEnglishDoublePinyinTrigger(&instance);
#endif

    instance.eventDispatcher().schedule([&instance]() { instance.exit(); });
    instance.exec();

    return 0;
}
