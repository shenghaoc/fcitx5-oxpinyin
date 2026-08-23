/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FCITX5_OXPINYIN_OXPINYIN_H_
#define FCITX5_OXPINYIN_OXPINYIN_H_

#include "oxpinyinconfig.h"

#include <fcitx-config/rawconfig.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/event.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <memory>
#include <string>

extern "C" {
#include <pinyin.h>
}

namespace fcitx {

class OxpinyinState;

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

    // Options bits + scheme setters from the current config; resets active
    // instances when the scheme changes (a buffer typed under one scheme is
    // never re-decoded under another).
    void applyConfig();

    Instance *instance_;
    FactoryFor<OxpinyinState> factory_;
    OxpinyinConfig config_;
    OxpinyinInputScheme lastAppliedScheme_ = OxpinyinInputScheme::FullPinyin;
    std::unique_ptr<pinyin_context_t, decltype(&pinyin_fini)> context_;
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

    std::string sentence() const;
    // Active-scheme aux text (full/double/chewing variant), cursor marker
    // stripped.
    std::string auxText() const;

    // Dispatch on the configured scheme.
    size_t parseBuffer() const;
    bool acceptChar(char c) const;

    void updateUI();
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
