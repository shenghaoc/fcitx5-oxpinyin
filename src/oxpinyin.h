/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FCITX5_OXPINYIN_OXPINYIN_H_
#define FCITX5_OXPINYIN_OXPINYIN_H_

#include "oxpinyinconfig.h"

#include <fcitx-config/rawconfig.h>
#include <fcitx/action.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <memory>
#include <optional>
#include <string>
#ifdef OXPINYIN_ENABLE_LUA
#include <vector>
#endif

extern "C" {
#include <pinyin.h>
}

namespace fcitx {

class OxpinyinState;
#ifdef OXPINYIN_ENABLE_CLOUDPINYIN
class CommonCandidateList;
#endif

class OxpinyinEngine final : public InputMethodEngineV3 {
public:
    explicit OxpinyinEngine(Instance *instance);
    ~OxpinyinEngine() override;

    void keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) override;
    void activate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;
    void deactivate(const InputMethodEntry &entry,
                    InputContextEvent &event) override;
    void reset(const InputMethodEntry &entry,
               InputContextEvent &event) override;
    void save() override;
    void reloadConfig() override;

    const Configuration *getConfig() const override { return &config_; }
    void setConfig(const RawConfig &config) override;

    bool isEngineReady() const { return context_ != nullptr; }

    pinyin_context_t *context() { return context_.get(); }

    OxpinyinState *state(InputContext *ic) {
        return ic->propertyFor(&factory_);
    }

    const OxpinyinConfig &config() const { return config_; }

private:
    friend class OxpinyinState;
    FCITX_ADDON_DEPENDENCY_LOADER(spell, instance_->addonManager())

    // Punctuation is delegated to fcitx5-chinese-addons' shared punctuation
    // module — a HARD dependency (declared in [Addon/Dependencies] and in
    // CMake), unlike the guarded-optional modules below. The loader can
    // still return null on a broken install; the key path degrades instead
    // of crashing.
    FCITX_ADDON_DEPENDENCY_LOADER(punctuation, instance_->addonManager())

    // Options bits + scheme setters from the current config; resets active
    // instances when the scheme changes (a buffer typed under one scheme is
    // never re-decoded under another).
    void applyConfig();

    // Re-sync the toggle actions' text/icons with the current config. Called
    // from the ctor, the Activated handlers, and applyConfig() so a flip OR
    // an external setConfig is reflected at once.
    void syncPredictionAction();
    void syncSpellAction();

    // Optional Chinese-addons conversion modules, resolved by name at
    // runtime. Each loader returns null when the module is not installed; the
    // matching status-area toggle is then simply not added (activate()). Both
    // conversions are the modules' own global CommitFilters — the shell wires
    // only the per-input-context toggle Action, never any conversion itself.
    FCITX_ADDON_DEPENDENCY_LOADER(chttrans, instance_->addonManager())
    FCITX_ADDON_DEPENDENCY_LOADER(fullwidth, instance_->addonManager())

#ifdef OXPINYIN_ENABLE_CLOUDPINYIN
    // Cloud Pinyin toggle hotkey (the module's toggleKey list): flips
    // CloudPinyinEnabled, persists it, and clears the module's error state on
    // enable. Returns true when the key was the toggle and was consumed.
    bool handleCloudToggle(KeyEvent &keyEvent);

    // Optional cloudpinyin module (fcitx5-chinese-addons), resolved by name at
    // runtime. Returns null when the module is absent/disabled, so every cloud
    // path is guarded on cloudpinyin() and the addon runs unaffected. Reached
    // only over the addon ABI (request/toggleKey/resetError) — never linked.
    FCITX_ADDON_DEPENDENCY_LOADER(cloudpinyin, instance_->addonManager())
#endif

#ifdef OXPINYIN_ENABLE_LUA
    // Optional imeapi module (fcitx5-lua's LuaAddonLoader), resolved by name
    // at runtime. Returns null when fcitx5-lua is absent/disabled, so every
    // lua path is guarded on imeapi() and the addon runs unaffected. Reached
    // only over the addon ABI (invokeLuaFunction) — the lua interpreter is
    // owned by LuaAddonLoader, never linked or re-implemented here.
    FCITX_ADDON_DEPENDENCY_LOADER(imeapi, instance_->addonManager())

    // Call imeapi's candidateTrigger for one candidate string and return the
    // extra candidate strings the lua side produced (empty when the module is
    // absent or the trigger yields nothing).
    std::vector<std::string>
    luaCandidateTrigger(InputContext *ic, const std::string &candidateString);
#endif

    Instance *instance_;
    FactoryFor<OxpinyinState> factory_;
    OxpinyinConfig config_;
    OxpinyinInputScheme lastAppliedScheme_ = OxpinyinInputScheme::FullPinyin;
    std::unique_ptr<pinyin_context_t, decltype(&pinyin_fini)> context_;

    // Status-bar toggles for this addon's OWN config switches, shaped after
    // fcitx5-chinese-addons' pinyin prediction toggle: a SimpleAction per
    // option, registered once by name in the engine ctor, and added to each
    // IM's InputMethod status group in activate() (StatusArea auto-clears
    // that group before every activate, so re-adding never duplicates and
    // nothing leaks across input contexts). chttrans/fullwidth (PR #8) and
    // punctuation are the MODULES' own actions and stay out of this pair;
    // cloud's toggleKey is a hotkey, not a status action (same as
    // chinese-addons).
    SimpleAction predictionAction_;
    SimpleAction spellAction_;
};

/*
 * Per-input-context editing state: one engine instance per fcitx input
 * context, plus the client-side raw-key buffer.
 *
 * Phase 4: a selection that does not consume the whole buffer PINS the
 * chosen span engine-side (pinyin_choose_candidate) and keeps composing —
 * cursor_ is the shell's raw-coordinate copy of the choose return (the
 * candidate's absolute end), used for the terminal check, the backspace
 * boundary, and the (validation-only) offset passed to guess_candidates;
 * the engine's own composition offset remains the anchor for candidate
 * generation. Backspacing into the chosen prefix clears the covering run
 * via pinyin_clear_constraint. All pinning stays engine-side — the shell
 * never derives spans or re-decodes under a pin itself.
 *
 * Phase 5: after a commit, if predictWords is enabled, the state enters
 * Predicting: buffer_ empty, cursor_ 0, no pins — the candidate list
 * holds predicted next-word candidates from the engine's bigram model.
 * Selecting a prediction commits it and re-predicts (chain); typing a
 * pinyin key leaves prediction and starts a fresh composition.
 */
class OxpinyinState final : public InputContextProperty {
public:
    OxpinyinState(OxpinyinEngine *engine, InputContext *ic);
    ~OxpinyinState() override;

    void keyEvent(KeyEvent &keyEvent);
    void reset();

    bool composing() const { return !buffer_.empty(); }
    bool predicting() const { return predicting_; }

    void refresh() {
        parsedLen_ = parseBuffer();
        updateUI();
    }

    pinyin_instance_t *instance() { return instance_.get(); }

private:
    friend class OxpinyinCandidateWord;
    friend class OxpinyinPredictedWord;
    friend class OxpinyinSpellCandidateWord;
#ifdef OXPINYIN_ENABLE_LUA
    friend class OxpinyinLuaCandidateWord;
#endif
    friend class OxpinyinEngine;

    // Candidate-list interaction while composing; true when consumed.
    bool handleCandidateKey(KeyEvent &keyEvent);

    // Key handling in the Predicting state; true when consumed.
    bool handlePredictingKey(KeyEvent &keyEvent);

    // Selection at cursor_: terminal (whole buffer consumed) commits via
    // the sentence path; partial pins the span and keeps composing.
    void selectCandidate(size_t index);

    // Prediction: enter the predicting state for a committed string.
    void enterPredicting(const std::string &committed);

    // Select a predicted candidate by index.
    void selectPredicted(size_t index);

    // Spell candidates bypass pinyin selection/training/constraints.
    void selectSpellCandidate(const std::string &word);

    // Delegate a punctuation key to chinese-addons' punctuation module
    // (getPunctuation) and commit the result. Returns true when the key was
    // consumed; false leaves it with the client.
    bool commitPunctuation(uint32_t sym);

#ifdef OXPINYIN_ENABLE_CLOUDPINYIN
    // Inject the cloud row into a freshly built candidate list, when enabled
    // and eligible (module present, unpinned fully-parsed full-pinyin buffer,
    // not a password field). The reused CloudPinyinCandidateWord issues the
    // async request in its ctor and self-fills; the slot is CloudPinyinIndex.
    void maybeAddCloudCandidate(CommonCandidateList &list);

    // Cloud-candidate select: the engine-independent workaround. Commits the
    // cloud hanzi directly and resets — NEVER pinyin_choose_candidate, train,
    // constraints, or user-input learning, so the engine's selection/
    // constraint contract is untouched. `selected` is the already-committed
    // prefix (empty here: cloud fires only on an unpinned full buffer).
    void cloudSelected(const std::string &selected, const std::string &word);
#endif

#ifdef OXPINYIN_ENABLE_LUA
    // Lua-candidate select: the engine-independent workaround (same contract
    // as Spell/Cloud). Commits the lua result directly and resets — NEVER
    // pinyin_choose_candidate, train, constraints, or user-input learning.
    void luaSelected(const std::string &word);
#endif

    std::string sentence() const;
    // Active-scheme aux text (full/double/chewing variant), cursor marker
    // stripped.
    std::string auxText() const;

    // Commit the current sentence (decoded + unparsed tail), train, reset.
    // Returns the committed string (empty if not composing).
    std::string commitSentence();

    // Dispatch on the configured scheme.
    size_t parseBuffer() const;
    bool acceptChar(char c) const;

    void updateUI();
    struct SpellHint {
        bool hasUpper;
        int limit;
    };
    std::optional<SpellHint> spellHint();
    void resetState();

    std::unique_ptr<pinyin_instance_t, decltype(&pinyin_free_instance)>
        instance_;
    InputContext *ic_;
    OxpinyinEngine *engine_;
    std::string buffer_;   // raw keys, scheme-dependent
    size_t parsedLen_ = 0; // bytes the engine accepted
    size_t cursor_ = 0;    // raw-coordinate end of the chosen prefix

    // Phase 5: prediction state
    bool predicting_ = false;
    std::string lastCommitted_; // context for re-prediction chains
};

class OxpinyinEngineFactory final : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override;
};

} // namespace fcitx

#endif // FCITX5_OXPINYIN_OXPINYIN_H_
