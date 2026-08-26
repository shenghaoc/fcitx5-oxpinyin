/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "testdir.h"
#include "testfrontend_public.h"

#include <cstdlib>
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

// Engine data resolution FAILS CLOSED: pinyin_init returns null without the
// exported .redb tables, the engine ctor returns early before wiring any
// state, and OxpinyinEngineFactory::create therefore refuses to hand out the
// addon (the same observable as a missing hard dependency — see
// testoxpinyin-punctabsent — reached through the DATA path instead of the
// addon-metadata path). This runner pins that observable end to end with an
// empty system data dir: the session's configured IM name still resolves
// (its .conf descriptor exists) but nothing backs it, so every key passes
// through unfiltered and the panel never engages.
void testEngineMissingDataFailsClosed(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        // pinyin_init failed -> the addon must NOT be handed out.
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
        // The switched slot is named oxpinyin, but no engine ever loaded
        // behind it: keys are not filtered and no panel state exists.
        for (const auto key : {"n", "i", "space", "1", "Return", "BackSpace",
                               "Escape", "exclam"}) {
            FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(key), false));
        }
        FCITX_ASSERT(ic->inputPanel().preedit().toString().empty());
        FCITX_ASSERT(ic->inputPanel().clientPreedit().toString().empty());
        FCITX_ASSERT(!ic->inputPanel().candidateList());

        instance->deactivate();
        testfrontend->call<ITestFrontend::destroyInputContext>(uuid);
    });
}

} // namespace

int main() {
    // Point the system data dir at an EMPTY directory: no .redb tables, so
    // pinyin_init fails closed. Set before Instance creation so the addon
    // load (lazy, inside exec()) inherits it. The user dir gets an equally
    // throwaway location, keeping the harness off any real session state.
    char systemTmpl[] = "/tmp/oxp-noengine-system-XXXXXX";
    char userTmpl[] = "/tmp/oxp-noengine-user-XXXXXX";
    char *systemDir = mkdtemp(systemTmpl);
    char *userDir = mkdtemp(userTmpl);
    if (!systemDir || !userDir) {
        return 1;
    }
    setenv("OXPINYIN_SYSTEM_DATA_DIR", systemDir, /*overwrite=*/1);
    setenv("OXPINYIN_USER_DATA_DIR", userDir, /*overwrite=*/1);

    std::vector<std::string> dataDirs{
        TESTING_BINARY_DIR "/test",
        StandardPaths::fcitxPath("pkgdatadir").string()};
    setupTestingEnvironment(TESTING_BINARY_DIR, {TESTING_BINARY_DIR "/src"},
                            dataDirs);
    char arg0[] = "testoxpinyin-noengine";
    char arg1[] = "--disable=all";
    // Punctuation stays enabled: a hard dependency of the engine's loader,
    // proving the refusal comes from the failed pinyin_init, not from a
    // dependency check.
    char arg2[] = "--enable=testim,testfrontend,oxpinyin,spell,punctuation";
    char *argv[] = {arg0, arg1, arg2};
    fcitx::Log::setLogRule("default=5,oxpinyin=5");
    fcitx::Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);

    testEngineMissingDataFailsClosed(&instance);

    instance.eventDispatcher().schedule([&instance]() { instance.exit(); });
    instance.exec();

    return 0;
}
