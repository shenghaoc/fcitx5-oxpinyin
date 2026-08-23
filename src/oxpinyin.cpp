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
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>

FCITX_DEFINE_LOG_CATEGORY(oxpinyin_log, "oxpinyin");

#define OXPINYIN_DEBUG() FCITX_LOGC(oxpinyin_log, Debug)
#define OXPINYIN_ERROR() FCITX_LOGC(oxpinyin_log, Error)

namespace fcitx {

namespace {

// Phase 2 constant; Phase 3 moves page size into FCITX_CONFIGURATION.
constexpr int kPageSize = 5;

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

// pinyin_get_sentence hands back a libc-malloc'd, caller-owned buffer (the
// engine's contract names g_free, which is free() there). The borrowed
// candidate strings are copied on sight instead: they die at the next
// candidate rebuild.
std::string ownedString(const char *s) {
    if (!s) {
        return {};
    }
    std::string result(s);
    std::free(const_cast<char *>(s));
    return result;
}

const KeyList &selectionKeys() {
    static const KeyList keys = {
        Key(FcitxKey_1), Key(FcitxKey_2), Key(FcitxKey_3), Key(FcitxKey_4),
        Key(FcitxKey_5), Key(FcitxKey_6), Key(FcitxKey_7), Key(FcitxKey_8),
        Key(FcitxKey_9), Key(FcitxKey_0)};
    return keys;
}

} // namespace

/*
 * Carries the candidate index back into the owning state; the display text
 * is a copy taken at list-build time.
 */
class OxpinyinCandidateWord final : public CandidateWord {
public:
    OxpinyinCandidateWord(OxpinyinEngine *engine, Text display, size_t index)
        : CandidateWord(std::move(display)), engine_(engine), index_(index) {}

    void select(InputContext *ic) const override {
        engine_->state(ic)->selectCandidate(index_);
    }

private:
    OxpinyinEngine *engine_;
    size_t index_;
};

/*******************************************************************************
 * OxpinyinEngine
 ******************************************************************************/

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
    keyEvent.inputContext()->propertyFor(&factory_)->keyEvent(keyEvent);
}

void OxpinyinEngine::activate(const InputMethodEntry & /*entry*/,
                              InputContextEvent & /*event*/) {}

void OxpinyinEngine::deactivate(const InputMethodEntry &entry,
                                InputContextEvent &event) {
    save();
    reset(entry, event);
}

void OxpinyinEngine::reset(const InputMethodEntry & /*entry*/,
                           InputContextEvent &event) {
    if (!factory_.registered()) {
        return;
    }
    event.inputContext()->propertyFor(&factory_)->reset();
}

void OxpinyinEngine::save() {
    if (context_) {
        OXPINYIN_DEBUG() << "pinyin_save";
        pinyin_save(context_.get());
    }
}

/*******************************************************************************
 * OxpinyinState
 ******************************************************************************/

OxpinyinState::OxpinyinState(OxpinyinEngine *engine, InputContext *ic)
    : instance_{nullptr, &pinyin_free_instance}, ic_{ic}, engine_{engine} {
    if (!engine->isEngineReady()) {
        OXPINYIN_ERROR() << "State created without an engine context";
        return;
    }
    instance_.reset(pinyin_alloc_instance(engine->context()));
}

OxpinyinState::~OxpinyinState() = default;

void OxpinyinState::keyEvent(KeyEvent &keyEvent) {
    if (!instance_) {
        return;
    }
    if (keyEvent.isRelease()) {
        return;
    }

    if (ic_->inputPanel().candidateList() && handleCandidateKey(keyEvent)) {
        keyEvent.filterAndAccept();
        return;
    }

    const auto key = keyEvent.key(); // normalized form

    if (key.isSimple()) {
        const auto c = static_cast<char>(key.sym() & 0xff);
        if ((c >= 'a' && c <= 'z') || c == '\'') {
            buffer_.push_back(c);
            parsedLen_ = pinyin_parse_more_full_pinyins(instance_.get(),
                                                        buffer_.c_str());
            if (buffer_.size() == 1 &&
                pinyin_get_parsed_input_length(instance_.get()) == 0) {
                // First key is not parseable pinyin: don't swallow it.
                resetState();
                return;
            }
            keyEvent.filterAndAccept();
            updateUI();
            return;
        }
        return; // digits and other simple keys pass through in v1
    }

    if (key.check(FcitxKey_space)) {
        if (buffer_.empty()) {
            return;
        }
        selectCandidate(0);
        keyEvent.filterAndAccept();
        return;
    }

    if (key.check(FcitxKey_Return)) {
        if (buffer_.empty()) {
            return;
        }
        auto committed = sentence();
        if (committed.empty()) {
            committed = buffer_;
        } else if (parsedLen_ < buffer_.size()) {
            committed += buffer_.substr(parsedLen_);
        }
        ic_->commitString(committed);
        keyEvent.filterAndAccept();
        resetState();
        return;
    }

    if (key.check(FcitxKey_BackSpace)) {
        if (buffer_.empty()) {
            return;
        }
        buffer_.pop_back();
        if (buffer_.empty()) {
            keyEvent.filterAndAccept();
            resetState();
            return;
        }
        parsedLen_ =
            pinyin_parse_more_full_pinyins(instance_.get(), buffer_.c_str());
        keyEvent.filterAndAccept();
        updateUI();
        return;
    }

    if (key.check(FcitxKey_Escape)) {
        if (buffer_.empty()) {
            return;
        }
        keyEvent.filterAndAccept();
        resetState();
        return;
    }

    // Modifier combos and everything else pass through to the client.
}

bool OxpinyinState::handleCandidateKey(KeyEvent &keyEvent) {
    // Copy the shared_ptr: selection replaces the panel's list while we
    // still hold this one alive.
    const auto list = ic_->inputPanel().candidateList();
    if (!list) {
        return false;
    }
    const auto key = keyEvent.key();

    if (key.check(FcitxKey_Page_Down) || key.check(FcitxKey_Page_Up)) {
        if (auto *pageable = list->toPageable(); pageable && !list->empty()) {
            if (key.check(FcitxKey_Page_Down)) {
                pageable->next(); // CommonCandidateList wraps at the ends
            } else {
                pageable->prev();
            }
            return true;
        }
    }

    // Selection keys address the current page; the word itself carries the
    // engine-side candidate index across pages.
    if (auto index = key.keyListIndex(selectionKeys());
        index >= 0 && index < list->size()) {
        list->candidate(index).select(ic_);
        return true;
    }

    return false;
}

void OxpinyinState::selectCandidate(size_t index) {
    guint count = 0;
    if (!pinyin_get_n_candidate(instance_.get(), &count) || index >= count) {
        return;
    }
    lookup_candidate_t *candidate = nullptr;
    if (!pinyin_get_candidate(instance_.get(), index, &candidate) ||
        !candidate) {
        return;
    }
    const gchar *text = nullptr;
    pinyin_get_candidate_string(instance_.get(), candidate, &text);
    const std::string chosen = text ? text : "";

    const auto newOffset =
        pinyin_choose_candidate(instance_.get(), 0, candidate);
    if (newOffset >= 0 && static_cast<size_t>(newOffset) >= parsedLen_ &&
        parsedLen_ == buffer_.size()) {
        // Whole buffer consumed: commit the sentence the choice produced,
        // then train and remember it.
        pinyin_guess_sentence(instance_.get());
        auto committed = sentence();
        if (committed.empty()) {
            committed = chosen;
        }
        ic_->commitString(committed);
        pinyin_train(instance_.get(), 0);
        pinyin_remember_user_input(instance_.get(), committed.c_str(), -1);
    } else {
        // Partial choice: v1 commits the candidate's own text and clears.
        // Phase 4 replaces this branch with the constrained re-decode
        // (choose -> keep composing -> re-run guess_*).
        ic_->commitString(chosen);
    }
    resetState();
}

std::string OxpinyinState::sentence() const {
    char *s = nullptr;
    if (pinyin_get_sentence(instance_.get(), 0, &s)) {
        return ownedString(s);
    }
    return ownedString(s);
}

void OxpinyinState::updateUI() {
    auto &panel = ic_->inputPanel();
    panel.reset();

    if (buffer_.empty()) {
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
        return;
    }

    pinyin_guess_sentence(instance_.get());
    auto text = sentence();
    Text preedit;
    if (!text.empty()) {
        preedit.append(text, TextFormatFlag::Underline);
        if (parsedLen_ < buffer_.size()) {
            preedit.append(buffer_.substr(parsedLen_),
                           TextFormatFlag::Underline);
        }
    } else {
        preedit.append(buffer_, TextFormatFlag::Underline);
    }
    // Panel preedit in Phase 2; Phase 3 moves to client preedit (cursor at
    // 0 + highlight) with the aux-text getters feeding the pinyin part.
    panel.setPreedit(preedit);

    if (parsedLen_ > 0) {
        pinyin_guess_candidates(instance_.get(), 0, 0);
        guint count = 0;
        if (pinyin_get_n_candidate(instance_.get(), &count) && count > 0) {
            auto list = std::make_unique<CommonCandidateList>();
            list->setSelectionKey(selectionKeys());
            list->setPageSize(kPageSize);
            for (guint i = 0; i < count; ++i) {
                lookup_candidate_t *candidate = nullptr;
                if (!pinyin_get_candidate(instance_.get(), i, &candidate) ||
                    !candidate) {
                    continue;
                }
                const gchar *text = nullptr;
                pinyin_get_candidate_string(instance_.get(), candidate, &text);
                list->append<OxpinyinCandidateWord>(
                    engine_, Text(text ? text : ""), static_cast<size_t>(i));
            }
            if (!list->empty()) {
                panel.setCandidateList(std::move(list));
            }
        }
    }

    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void OxpinyinState::reset() { resetState(); }

void OxpinyinState::resetState() {
    buffer_.clear();
    parsedLen_ = 0;
    if (instance_) {
        pinyin_reset(instance_.get());
    }
    updateUI();
}

/*******************************************************************************
 * OxpinyinEngineFactory
 ******************************************************************************/

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
