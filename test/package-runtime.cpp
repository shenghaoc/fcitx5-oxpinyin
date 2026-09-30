/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "testfrontend_public.h"
#include <fcitx-utils/key.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/testing.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <string>

// Standalone package probe: discovery uses installed descriptors/library,
// never build-tree copies. TestFrontend itself is a harness prerequisite.
int main(int argc, char **args) {
    FCITX_ASSERT(argc == 3);
    const std::string root = args[1];
    const std::string mode = args[2];
    fcitx::setupTestingEnvironment(
        root, {root + "/" OXPINYIN_PACKAGE_LIBDIR "/fcitx5"},
        {root + "/share/fcitx5",
         fcitx::StandardPaths::fcitxPath("pkgdatadir").string()});
    char arg0[] = "package-runtime";
    char arg1[] = "--disable=all";
    char withPunctuation[] =
        "--enable=testfrontend,testim,oxpinyin,punctuation";
    char withoutPunctuation[] = "--enable=testfrontend,testim,oxpinyin";
    char *argv[] = {arg0, arg1,
                    mode == "no-punctuation" ? withoutPunctuation
                                             : withPunctuation};
    fcitx::Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);
    instance.eventDispatcher().schedule([&]() {
        auto *engine = instance.addonManager().addon("oxpinyin", true);
        if (mode != "working") {
            FCITX_ASSERT(!engine);
        } else {
            FCITX_ASSERT(engine);
            auto *frontend = instance.addonManager().addon("testfrontend");
            FCITX_ASSERT(frontend);
            auto group = instance.inputMethodManager().currentGroup();
            group.inputMethodList().clear();
            group.inputMethodList().emplace_back("keyboard-us");
            group.inputMethodList().emplace_back("oxpinyin");
            group.setDefaultInputMethod("");
            instance.inputMethodManager().setGroup(group);
            auto uuid =
                frontend->call<fcitx::ITestFrontend::createInputContext>(
                    "package-check");
            auto *ic = instance.inputContextManager().findByUUID(uuid);
            FCITX_ASSERT(frontend->call<fcitx::ITestFrontend::sendKeyEvent>(
                uuid, fcitx::Key("Control+space"), false));
            for (const auto c : std::string("nihao")) {
                FCITX_ASSERT(frontend->call<fcitx::ITestFrontend::sendKeyEvent>(
                    uuid, fcitx::Key(static_cast<fcitx::KeySym>(c)), false));
            }
            auto list = ic->inputPanel().candidateList();
            FCITX_ASSERT(list && !list->empty());
            frontend->call<fcitx::ITestFrontend::pushCommitExpectation>(
                list->candidate(0).text().toString());
            FCITX_ASSERT(frontend->call<fcitx::ITestFrontend::sendKeyEvent>(
                uuid, fcitx::Key("space"), false));
            FCITX_ASSERT(!ic->inputPanel().candidateList());
            instance.deactivate();
            frontend->call<fcitx::ITestFrontend::destroyInputContext>(uuid);
        }
        instance.exit();
    });
    return instance.exec();
}
