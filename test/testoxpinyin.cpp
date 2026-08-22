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
 * Phase 1 assertions only: the addon loads against the real engine data,
 * the IM group switch (keyboard-us + oxpinyin) works, and key handling is
 * logging-only — every key, including modifier combos, passes through
 * unfiltered (sendKeyEvent returns false).
 */
void testLoadAndPassthrough(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *oxpinyin = instance->addonManager().addon("oxpinyin", true);
        FCITX_ASSERT(oxpinyin);

        auto defaultGroup = instance->inputMethodManager().currentGroup();
        defaultGroup.inputMethodList().clear();
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("keyboard-us"));
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("oxpinyin"));
        defaultGroup.setDefaultInputMethod("");
        instance->inputMethodManager().setGroup(defaultGroup);

        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(ic) == "oxpinyin");

        for (const auto *k : {"n", "i", "h", "a", "o"}) {
            FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(k), false));
        }
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Ctrl+A"), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(!ic->inputPanel().candidateList());

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

    instance.eventDispatcher().schedule([&instance]() { instance.exit(); });
    instance.exec();

    return 0;
}
