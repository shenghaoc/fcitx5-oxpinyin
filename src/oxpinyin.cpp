/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "oxpinyin.h"
#include "oxpinyin_paths.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

#include <fcitx-config/iniparser.h>
#include <fcitx-utils/capabilityflags.h>
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

constexpr auto kConfigFile = "conf/oxpinyin.conf";

// Scheme mapping onto the capi enums (pinyin.h values only).
int doublePinyinValue(OxpinyinInputScheme scheme) {
    switch (scheme) {
    case OxpinyinInputScheme::DoublePinyinZRM:
        return DOUBLE_PINYIN_ZRM;
    case OxpinyinInputScheme::DoublePinyinMS:
        return DOUBLE_PINYIN_MS;
    case OxpinyinInputScheme::DoublePinyinZiguang:
        return DOUBLE_PINYIN_ZIGUANG;
    case OxpinyinInputScheme::DoublePinyinABC:
        return DOUBLE_PINYIN_ABC;
    case OxpinyinInputScheme::DoublePinyinPYJJ:
        return DOUBLE_PINYIN_PYJJ;
    case OxpinyinInputScheme::DoublePinyinXiaohe:
        return DOUBLE_PINYIN_XHE;
    default:
        return -1;
    }
}

int zhuyinValue(OxpinyinInputScheme scheme) {
    switch (scheme) {
    case OxpinyinInputScheme::ZhuyinStandard:
        return ZHUYIN_STANDARD;
    case OxpinyinInputScheme::ZhuyinHsu:
        return ZHUYIN_HSU;
    case OxpinyinInputScheme::ZhuyinIBM:
        return ZHUYIN_IBM;
    case OxpinyinInputScheme::ZhuyinGinYieh:
        return ZHUYIN_GINYIEH;
    case OxpinyinInputScheme::ZhuyinEten:
        return ZHUYIN_ETEN;
    case OxpinyinInputScheme::ZhuyinEten26:
        return ZHUYIN_ETEN26;
    case OxpinyinInputScheme::ZhuyinStandardDvorak:
        return ZHUYIN_STANDARD_DVORAK;
    case OxpinyinInputScheme::ZhuyinHsuDvorak:
        return ZHUYIN_HSU_DVORAK;
    case OxpinyinInputScheme::ZhuyinDachenCP26:
        return ZHUYIN_DACHEN_CP26;
    default:
        return -1;
    }
}

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

/*
 * Predicted-candidate word: carries the index back into selectPredicted.
 */
class OxpinyinPredictedWord final : public CandidateWord {
public:
    OxpinyinPredictedWord(OxpinyinEngine *engine, Text display, size_t index)
        : CandidateWord(std::move(display)), engine_(engine), index_(index) {}

    void select(InputContext *ic) const override {
        engine_->state(ic)->selectPredicted(index_);
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
    reloadConfig();
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

void OxpinyinEngine::reloadConfig() {
    readAsIni(config_, kConfigFile);
    applyConfig();
}

void OxpinyinEngine::setConfig(const RawConfig &config) {
    config_.load(config, true);
    safeSaveAsIni(config_, kConfigFile);
    applyConfig();
}

void OxpinyinEngine::applyConfig() {
    if (!context_) {
        return;
    }

    pinyin_option_t bits =
        DYNAMIC_ADJUST | USE_DIVIDED_TABLE | USE_RESPLIT_TABLE;
    if (*config_.incomplete) {
        // One toggle drives both parse modes' shorthand bits.
        bits |= PINYIN_INCOMPLETE | ZHUYIN_INCOMPLETE;
    }
    if (*config_.fuzzyCCh) {
        bits |= PINYIN_AMB_C_CH;
    }
    if (*config_.fuzzySSh) {
        bits |= PINYIN_AMB_S_SH;
    }
    if (*config_.fuzzyZZh) {
        bits |= PINYIN_AMB_Z_ZH;
    }
    if (*config_.fuzzyFH) {
        bits |= PINYIN_AMB_F_H;
    }
    if (*config_.fuzzyGK) {
        bits |= PINYIN_AMB_G_K;
    }
    if (*config_.fuzzyLN) {
        bits |= PINYIN_AMB_L_N;
    }
    if (*config_.fuzzyLR) {
        bits |= PINYIN_AMB_L_R;
    }
    if (*config_.fuzzyAnAng) {
        bits |= PINYIN_AMB_AN_ANG;
    }
    if (*config_.fuzzyEnEng) {
        bits |= PINYIN_AMB_EN_ENG;
    }
    if (*config_.fuzzyInIng) {
        bits |= PINYIN_AMB_IN_ING;
    }
    if (*config_.correctGnNg) {
        bits |= PINYIN_CORRECT_GN_NG;
    }
    if (*config_.correctMgNg) {
        bits |= PINYIN_CORRECT_MG_NG;
    }
    if (*config_.correctIouIu) {
        bits |= PINYIN_CORRECT_IOU_IU;
    }
    if (*config_.correctUeiUi) {
        bits |= PINYIN_CORRECT_UEI_UI;
    }
    if (*config_.correctUenUn) {
        bits |= PINYIN_CORRECT_UEN_UN;
    }
    if (*config_.correctUeVe) {
        bits |= PINYIN_CORRECT_UE_VE;
    }
    pinyin_set_options(context_.get(), bits);

    const auto scheme = *config_.inputScheme;
    if (const auto value = doublePinyinValue(scheme); value > 0) {
        pinyin_set_double_pinyin_scheme(context_.get(),
                                        static_cast<DoublePinyinScheme>(value));
    } else if (const auto value = zhuyinValue(scheme); value > 0) {
        pinyin_set_zhuyin_scheme(context_.get(),
                                 static_cast<ZhuyinScheme>(value));
    }

    const bool schemeChanged = scheme != lastAppliedScheme_;
    lastAppliedScheme_ = scheme;

    if (factory_.registered()) {
        instance_->inputContextManager().foreach([this, schemeChanged](
                                                     InputContext *ic) {
            auto *st = state(ic);
            if (!st->composing() || instance_->inputMethod(ic) != "oxpinyin") {
                return true;
            }
            if (schemeChanged) {
                // Never re-decode a buffer typed under another scheme.
                st->resetState();
            } else {
                st->refresh();
            }
            return true;
        });
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

    // Phase 5: prediction state intercepts keys before composition logic.
    if (predicting_) {
        if (handlePredictingKey(keyEvent)) {
            keyEvent.filterAndAccept();
            return;
        }
        const auto key = keyEvent.key();
        if (key.isSimple() && acceptChar(static_cast<char>(key.sym() & 0xff))) {
            // Prediction was dismissed: process this key as the first key of
            // a fresh composition through the ordinary key path.
            this->keyEvent(keyEvent);
        }
        return;
    }

    if (ic_->inputPanel().candidateList() && handleCandidateKey(keyEvent)) {
        keyEvent.filterAndAccept();
        return;
    }

    const auto key = keyEvent.key(); // normalized form

    if (key.isSimple()) {
        const auto c = static_cast<char>(key.sym() & 0xff);
        if (acceptChar(c)) {
            buffer_.push_back(c);
            parsedLen_ = parseBuffer();
            // Zhuyin keeps any in-keyboard key composing (a lone initial
            // parses to nothing yet but belongs to the syllable being
            // built); for the pinyin modes a first key that parses to
            // nothing is passed through un-swallowed.
            const auto zhuyin =
                zhuyinValue(engine_->config().inputScheme.value()) > 0;
            if (!zhuyin && buffer_.size() == 1 &&
                pinyin_get_parsed_input_length(instance_.get()) == 0) {
                resetState();
                return;
            }
            keyEvent.filterAndAccept();
            updateUI();
            return;
        }
        return; // other simple keys pass through in v1
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
        const auto decoded = sentence();
        const bool fromSentence = !decoded.empty();
        auto committed = decoded;
        if (!fromSentence) {
            committed = buffer_;
        } else if (parsedLen_ < buffer_.size()) {
            committed += buffer_.substr(parsedLen_);
        }
        ic_->commitString(committed);
        if (fromSentence) {
            pinyin_train(instance_.get(), 0);
            pinyin_remember_user_input(instance_.get(), decoded.c_str(), -1);
        }
        keyEvent.filterAndAccept();
        resetState();
        if (*engine_->config().predictWords) {
            enterPredicting(committed);
        }
        return;
    }

    if (key.check(FcitxKey_BackSpace)) {
        if (buffer_.empty()) {
            return;
        }
        const auto pos = buffer_.size() - 1;
        if (pos < cursor_) {
            // Deleting into the chosen prefix: unpin the run covering the
            // deleted position (the engine clears the whole run; false
            // means it was already free) and bound the shell's cursor at
            // the edit point.
            pinyin_clear_constraint(instance_.get(), pos);
            cursor_ = pos;
        }
        buffer_.pop_back();
        if (buffer_.empty()) {
            keyEvent.filterAndAccept();
            resetState();
            return;
        }
        parsedLen_ = parseBuffer();
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

    // Pins the chosen span engine-side; the return is the candidate's
    // absolute end in the raw-input coordinates of the active parse mode.
    const auto newCursor =
        pinyin_choose_candidate(instance_.get(), cursor_, candidate);
    if (newCursor < 0) {
        return;
    }

    if (static_cast<size_t>(newCursor) >= parsedLen_ &&
        parsedLen_ == buffer_.size()) {
        // Whole buffer consumed: commit the constrained sentence the
        // choices produced, then train and remember it (decoded only).
        pinyin_guess_sentence(instance_.get());
        auto committed = sentence();
        if (committed.empty()) {
            const gchar *text = nullptr;
            pinyin_get_candidate_string(instance_.get(), candidate, &text);
            committed = text ? text : "";
        }
        ic_->commitString(committed);
        pinyin_train(instance_.get(), 0);
        pinyin_remember_user_input(instance_.get(), committed.c_str(), -1);
        resetState();
        if (*engine_->config().predictWords) {
            enterPredicting(committed);
        }
        return;
    }

    // Partial choice: keep composing under the pin. cursor_ mirrors the
    // choose return; the engine re-decodes the tail — its own composition
    // offset anchors the rebuilt candidate list.
    cursor_ = static_cast<size_t>(newCursor);
    updateUI();
}

std::string OxpinyinState::sentence() const {
    char *s = nullptr;
    if (pinyin_get_sentence(instance_.get(), 0, &s)) {
        return ownedString(s);
    }
    return ownedString(s);
}

size_t OxpinyinState::parseBuffer() const {
    switch (engine_->config().inputScheme.value()) {
    case OxpinyinInputScheme::DoublePinyinZRM:
    case OxpinyinInputScheme::DoublePinyinMS:
    case OxpinyinInputScheme::DoublePinyinZiguang:
    case OxpinyinInputScheme::DoublePinyinABC:
    case OxpinyinInputScheme::DoublePinyinPYJJ:
    case OxpinyinInputScheme::DoublePinyinXiaohe:
        return pinyin_parse_more_double_pinyins(instance_.get(),
                                                buffer_.c_str());
    case OxpinyinInputScheme::ZhuyinStandard:
    case OxpinyinInputScheme::ZhuyinHsu:
    case OxpinyinInputScheme::ZhuyinIBM:
    case OxpinyinInputScheme::ZhuyinGinYieh:
    case OxpinyinInputScheme::ZhuyinEten:
    case OxpinyinInputScheme::ZhuyinEten26:
    case OxpinyinInputScheme::ZhuyinStandardDvorak:
    case OxpinyinInputScheme::ZhuyinHsuDvorak:
    case OxpinyinInputScheme::ZhuyinDachenCP26:
        return pinyin_parse_more_chewings(instance_.get(), buffer_.c_str());
    default:
        return pinyin_parse_more_full_pinyins(instance_.get(), buffer_.c_str());
    }
}

bool OxpinyinState::acceptChar(char c) const {
    using S = OxpinyinInputScheme;
    switch (engine_->config().inputScheme.value()) {
    case S::DoublePinyinZRM:
    case S::DoublePinyinMS:
    case S::DoublePinyinZiguang:
    case S::DoublePinyinABC:
    case S::DoublePinyinPYJJ:
    case S::DoublePinyinXiaohe:
        return (c >= 'a' && c <= 'z') || c == ';';
    case S::ZhuyinStandard:
    case S::ZhuyinHsu:
    case S::ZhuyinIBM:
    case S::ZhuyinGinYieh:
    case S::ZhuyinEten:
    case S::ZhuyinEten26:
    case S::ZhuyinStandardDvorak:
    case S::ZhuyinHsuDvorak:
    case S::ZhuyinDachenCP26: {
        // The engine knows the active layout's key table. The symbols
        // out-parameter is dereferenced unconditionally, so it must point at
        // real storage even though only the verdict is wanted here; the strv
        // it hands back is ours to release.
        gchar **symbols = nullptr;
        const bool accepted =
            pinyin_in_chewing_keyboard(instance_.get(), c, &symbols);
        for (gchar **symbol = symbols; symbol && *symbol; ++symbol) {
            std::free(*symbol);
        }
        std::free(symbols);
        return accepted;
    }
    default:
        return (c >= 'a' && c <= 'z') || c == '\'';
    }
}

std::string OxpinyinState::auxText() const {
    char *a = nullptr;
    const auto scheme = engine_->config().inputScheme.value();
    using S = OxpinyinInputScheme;
    if (scheme == S::FullPinyin) {
        pinyin_get_full_pinyin_auxiliary_text(instance_.get(), parsedLen_, &a);
    } else if (doublePinyinValue(scheme) > 0) {
        pinyin_get_double_pinyin_auxiliary_text(instance_.get(), parsedLen_,
                                                &a);
    } else {
        pinyin_get_chewing_auxiliary_text(instance_.get(), parsedLen_, &a);
    }
    auto text = ownedString(a);
    // The cursor marker positions the caret in the raw input; the client
    // cursor is pinned at 0 by design, so drop it from the display text.
    text.erase(std::remove(text.begin(), text.end(), '|'), text.end());
    return text;
}

bool OxpinyinState::handlePredictingKey(KeyEvent &keyEvent) {
    const auto key = keyEvent.key();

    // Candidate selection keys while predicting.
    if (const auto list = ic_->inputPanel().candidateList(); list) {
        if (key.check(FcitxKey_space)) {
            // Space selects the first predicted candidate.
            if (!list->empty()) {
                list->candidate(0).select(ic_);
                return true;
            }
        }
        if (key.check(FcitxKey_Page_Down) || key.check(FcitxKey_Page_Up)) {
            if (auto *pageable = list->toPageable();
                pageable && !list->empty()) {
                if (key.check(FcitxKey_Page_Down)) {
                    pageable->next();
                } else {
                    pageable->prev();
                }
                return true;
            }
        }
        if (auto index = key.keyListIndex(selectionKeys());
            index >= 0 && index < list->size()) {
            list->candidate(index).select(ic_);
            return true;
        }
    }

    // Escape: dismiss prediction, stay idle.
    if (key.check(FcitxKey_Escape)) {
        predicting_ = false;
        lastCommitted_.clear();
        updateUI();
        return true;
    }

    // A pinyin key: dismiss prediction; keyEvent() will re-dispatch it to
    // the normal composition path.
    if (key.isSimple()) {
        const auto c = static_cast<char>(key.sym() & 0xff);
        if (acceptChar(c)) {
            predicting_ = false;
            lastCommitted_.clear();
            updateUI();
            return false;
        }
    }

    // Any other key: dismiss prediction and pass through.
    predicting_ = false;
    lastCommitted_.clear();
    updateUI();
    return false;
}

void OxpinyinState::enterPredicting(const std::string &committed) {
    // Called after resetState() has already cleaned the instance. Predict
    // on the clean post-reset instance so no stale pin/buffer bleeds.
    // The panel is cleared up front: a chained prediction that comes back
    // empty must not leave the previous list on screen, where it would be
    // shown as selectable against an instance that no longer backs it.
    auto &panel = ic_->inputPanel();
    panel.reset();

    pinyin_guess_predicted_candidates_with_punctuations(instance_.get(),
                                                        committed.c_str());
    guint count = 0;
    if (!pinyin_get_n_candidate(instance_.get(), &count) || count == 0) {
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
        return; // no predictions — stay idle
    }
    predicting_ = true;
    lastCommitted_ = committed;

    auto list = std::make_unique<CommonCandidateList>();
    list->setSelectionKey(selectionKeys());
    list->setPageSize(*engine_->config().pageSize);
    for (guint i = 0; i < count; ++i) {
        lookup_candidate_t *candidate = nullptr;
        if (!pinyin_get_candidate(instance_.get(), i, &candidate) ||
            !candidate) {
            continue;
        }
        const gchar *text = nullptr;
        pinyin_get_candidate_string(instance_.get(), candidate, &text);
        list->append<OxpinyinPredictedWord>(engine_, Text(text ? text : ""),
                                            static_cast<size_t>(i));
    }
    if (!list->empty()) {
        panel.setCandidateList(std::move(list));
    } else {
        predicting_ = false;
        lastCommitted_.clear();
    }
    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void OxpinyinState::selectPredicted(size_t index) {
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
    std::string committed = text ? text : "";
    if (committed.empty()) {
        return;
    }

    // Train the predicted choice.
    pinyin_choose_predicted_candidate(instance_.get(), candidate);

    // Commit the predicted text.
    ic_->commitString(committed);

    // Re-predict from the newly committed text (chain).
    predicting_ = false;
    lastCommitted_.clear();
    pinyin_reset(instance_.get());
    enterPredicting(committed);
}

void OxpinyinState::updateUI() {
    auto &panel = ic_->inputPanel();
    panel.reset();

    if (buffer_.empty()) {
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
        return;
    }

    std::string converted, aux;
    if (parsedLen_ > 0) {
        pinyin_guess_sentence(instance_.get());
        converted = sentence();
        aux = auxText();
    }

    if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
        // Wiki guidance: the client cursor stays pinned at 0 (the
        // candidate window doesn't jitter while typing) and the composing
        // region carries the highlight.
        Text preedit;
        preedit.setCursor(0);
        if (!converted.empty()) {
            preedit.append(converted, TextFormatFlag::Underline);
        }
        if (!aux.empty()) {
            preedit.append(aux, TextFormatFlags{TextFormatFlag::Underline,
                                                TextFormatFlag::HighLight});
        }
        if (parsedLen_ < buffer_.size()) {
            preedit.append(buffer_.substr(parsedLen_),
                           TextFormatFlag::Underline);
        }
        if (preedit.empty()) {
            preedit.append(buffer_, TextFormatFlag::Underline);
        }
        panel.setClientPreedit(preedit);
        ic_->updatePreedit();
    } else {
        // Panel fallback: the preedit carries what a commit would produce;
        // the typed syllables sit below it.
        Text preedit;
        if (!converted.empty()) {
            preedit.append(converted, TextFormatFlag::Underline);
            if (parsedLen_ < buffer_.size()) {
                preedit.append(buffer_.substr(parsedLen_),
                               TextFormatFlag::Underline);
            }
        } else {
            preedit.append(buffer_, TextFormatFlag::Underline);
        }
        panel.setPreedit(preedit);
        if (!aux.empty()) {
            panel.setAuxDown(Text(aux));
        }
    }

    if (parsedLen_ > 0) {
        pinyin_guess_candidates(instance_.get(), cursor_, 0);
        guint count = 0;
        if (pinyin_get_n_candidate(instance_.get(), &count) && count > 0) {
            auto list = std::make_unique<CommonCandidateList>();
            list->setSelectionKey(selectionKeys());
            list->setPageSize(*engine_->config().pageSize);
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
    // pinyin_reset is the parse-path reset: the constraint store, the
    // selection record, and the raw input SURVIVE it (the L2 lifetime that
    // lets pins ride out a re-parse). Sweep the runs explicitly so no pin
    // leaks into the next composition or across a scheme switch — each
    // clear_constraint hit frees the whole covering run; misses are free
    // cells and cost nothing.
    if (instance_) {
        for (size_t i = buffer_.size(); i-- > 0;) {
            pinyin_clear_constraint(instance_.get(), i);
        }
        pinyin_reset(instance_.get());
    }
    buffer_.clear();
    parsedLen_ = 0;
    cursor_ = 0;
    predicting_ = false;
    lastCommitted_.clear();
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
