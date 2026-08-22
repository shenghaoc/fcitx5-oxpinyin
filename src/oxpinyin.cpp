/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "oxpinyin.h"
#include "oxpinyin_paths.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

#include <fcitx-utils/i18n.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

FCITX_DEFINE_LOG_CATEGORY(oxpinyin_log, "oxpinyin");

#define OXPINYIN_DEBUG() FCITX_LOGC(oxpinyin_log, Debug)
#define OXPINYIN_ERROR() FCITX_LOGC(oxpinyin_log, Error)

namespace fcitx {

namespace {

std::pair<std::filesystem::path, std::filesystem::path> resolveDataDirs() {
    // System dir: env override first (keeps the harness and CI off any real
    // session state), then the compiled-in install location if it exists on
    // this machine, then fcitx StandardPaths (PkgData + "oxpinyin").
    std::filesystem::path systemDir;
    if (const char *env = std::getenv("OXPINYIN_SYSTEM_DATA_DIR");
        env && *env) {
        systemDir = env;
    } else if (std::filesystem::exists(OXPINYIN_COMPILED_DATADIR)) {
        systemDir = OXPINYIN_COMPILED_DATADIR;
    } else if (auto located = StandardPaths::global().locate(
                   StandardPathsType::PkgData, "oxpinyin/phrase_index.redb");
               !located.empty()) {
        systemDir = located.parent_path();
    } else {
        // pinyin_init fails closed on missing tables; pass the compiled-in
        // path so the failure names the expected location.
        systemDir = OXPINYIN_COMPILED_DATADIR;
    }

    std::filesystem::path userDir;
    if (const char *env = std::getenv("OXPINYIN_USER_DATA_DIR"); env && *env) {
        userDir = env;
    } else {
        userDir =
            StandardPaths::global().userDirectory(StandardPathsType::PkgData) /
            "oxpinyin";
    }
    return {systemDir, userDir};
}

} // namespace

OxpinyinEngine::OxpinyinEngine(Instance *instance)
    : instance_{instance}, factory_([this](InputContext &ic) {
          return new OxpinyinState(this, &ic);
      }),
      context_{nullptr, &pinyin_fini} {
    auto [systemDir, userDir] = resolveDataDirs();

    std::error_code ec;
    if (!std::filesystem::create_directories(userDir, ec) &&
        !std::filesystem::is_directory(userDir)) {
        OXPINYIN_ERROR() << "Cannot create user data dir " << userDir << ": "
                         << ec.message();
        return;
    }

    OXPINYIN_DEBUG() << "pinyin_init systemdir=" << systemDir
                     << " userdir=" << userDir;
    context_.reset(pinyin_init(systemDir.c_str(), userDir.c_str()));
    if (!context_) {
        OXPINYIN_ERROR()
            << "pinyin_init failed (data missing in " << systemDir
            << "; needs pinyin_index/phrase_index/bigram .redb tables and "
               "interpolation2.text)";
        return;
    }

    instance_->inputContextManager().registerProperty("oxpinyinstate",
                                                      &factory_);
}

OxpinyinEngine::~OxpinyinEngine() = default;

void OxpinyinEngine::keyEvent(const InputMethodEntry & /*entry*/,
                              KeyEvent &keyEvent) {
    if (!factory_.registered()) {
        return;
    }
    OXPINYIN_DEBUG() << "keyEvent: " << keyEvent.key().toString();
    // Phase 1 skeleton: log only, never filter. The Phase 2 engine loop
    // (buffer -> parse -> guess -> candidates) takes over here.
    keyEvent.inputContext()->propertyFor(&factory_)->keyEvent(keyEvent);
}

void OxpinyinEngine::activate(const InputMethodEntry & /*entry*/,
                              InputContextEvent & /*event*/) {}

void OxpinyinEngine::deactivate(const InputMethodEntry &entry,
                                InputContextEvent &event) {
    reset(entry, event);
}

void OxpinyinEngine::reset(const InputMethodEntry & /*entry*/,
                           InputContextEvent &event) {
    if (!factory_.registered()) {
        return;
    }
    // Phase 1: lifecycle only; pinyin_reset lands with the Phase 2 loop.
    OXPINYIN_DEBUG() << "reset";
    event.inputContext()->propertyFor(&factory_);
}

OxpinyinState::OxpinyinState(OxpinyinEngine *engine, InputContext * /*ic*/)
    : instance_{nullptr, &pinyin_free_instance} {
    if (!engine->isEngineReady()) {
        OXPINYIN_ERROR() << "State created without an engine context";
        return;
    }
    instance_.reset(pinyin_alloc_instance(engine->context()));
}

OxpinyinState::~OxpinyinState() = default;

void OxpinyinState::keyEvent(KeyEvent & /*keyEvent*/) {
    // Phase 1: logging happens in OxpinyinEngine::keyEvent.
}

AddonInstance *OxpinyinEngineFactory::create(AddonManager *manager) {
    registerDomain("fcitx5-oxpinyin", FCITX_INSTALL_LOCALEDIR);
    auto *engine = new OxpinyinEngine(manager->instance());
    if (!engine->isEngineReady()) {
        delete engine;
        return nullptr;
    }
    return engine;
}

} // namespace fcitx

FCITX_ADDON_FACTORY_V2(oxpinyin, fcitx::OxpinyinEngineFactory);
