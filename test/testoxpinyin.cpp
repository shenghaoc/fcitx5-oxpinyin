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
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/testing.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>

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

} // namespace

int main() {
    // The system pkgdatadir is needed so the punctuation addon (a hard
    // dependency) resolves from the distro's fcitx5-chinese-addons install
    // — same trick as upstream testpinyin.
    setupTestingEnvironment(
        TESTING_BINARY_DIR, {TESTING_BINARY_DIR "/src"},
        {TESTING_BINARY_DIR "/test", StandardPaths::fcitxPath("pkgdatadir")});
    char arg0[] = "testoxpinyin";
    char arg1[] = "--disable=all";
    // punctuation is a hard addon dependency, so it must be enabled too
    // (upstream testpinyin does the same).
    char arg2[] = "--enable=testim,testfrontend,oxpinyin,punctuation";
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

    instance.eventDispatcher().schedule([&instance]() { instance.exit(); });
    instance.exec();

    return 0;
}
