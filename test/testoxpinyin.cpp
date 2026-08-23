/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "testdir.h"
#include "testfrontend_public.h"

#include <fcitx-utils/key.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/testing.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>

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

    instance.eventDispatcher().schedule([&instance]() { instance.exit(); });
    instance.exec();

    return 0;
}
