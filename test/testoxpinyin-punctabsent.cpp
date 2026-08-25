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
#include <string>
#include <vector>

using namespace fcitx;

namespace {

// Punctuation is a HARD dependency ([Addon/Dependencies] 1=punctuation):
// fcitx5's addon manager refuses to load the engine when the punctuation
// module is not loadable. This harness deselects the module (absent from
// --enable), so the engine must NOT load — the inverted successor of the old
// "addon loads without chinese-addons" invariant. The engine's defensive
// null handling of punctuation() (raw-key commit, production-assert policy)
// covers the impossible-but-asserted case of the module disappearing beneath
// a loaded engine; this load-time gate fires first, so the harness cannot
// reach that path.
void testPunctuationHardDependency(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        // The engine does not load without its punctuation module.
        FCITX_ASSERT(!instance->addonManager().addon("oxpinyin", true));
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        FCITX_ASSERT(testfrontend);
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);

        auto defaultGroup = instance->inputMethodManager().currentGroup();
        defaultGroup.inputMethodList().clear();
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("keyboard-us"));
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("oxpinyin"));
        defaultGroup.setDefaultInputMethod("");
        instance->inputMethodManager().setGroup(defaultGroup);

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        // fcitx still names the switched slot "oxpinyin", but no engine ever
        // loaded behind it: keys are not filtered and no panel state exists.
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_exclam), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        instance->deactivate();
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}

} // namespace

int main() {
    // Same data dirs as the main harness; the punctuation module's conf IS
    // discoverable under the system pkgdatadir — it is simply not enabled,
    // which is what the hard-dependency check must reject.
    std::vector<std::string> dataDirs{
        TESTING_BINARY_DIR "/test",
        StandardPaths::fcitxPath("pkgdatadir").string()};
    setupTestingEnvironment(TESTING_BINARY_DIR, {TESTING_BINARY_DIR "/src"},
                            dataDirs);
    char arg0[] = "testoxpinyin-punctabsent";
    char arg1[] = "--disable=all";
    // Deliberately WITHOUT punctuation, so the engine must fail its
    // load-time dependency check.
    char arg2[] = "--enable=testim,testfrontend,oxpinyin,spell";
    char *argv[] = {arg0, arg1, arg2};
    fcitx::Log::setLogRule("default=5,oxpinyin=5");
    fcitx::Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);

    testPunctuationHardDependency(&instance);

    instance.eventDispatcher().schedule([&instance]() { instance.exit(); });
    instance.exec();

    return 0;
}
