/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FCITX5_OXPINYIN_OXPINYIN_H_
#define FCITX5_OXPINYIN_OXPINYIN_H_

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

    bool isEngineReady() const { return context_ != nullptr; }

    pinyin_context_t *context() { return context_.get(); }

    OxpinyinState *state(InputContext *ic) {
        return ic->propertyFor(&factory_);
    }

private:
    Instance *instance_;
    FactoryFor<OxpinyinState> factory_;
    std::unique_ptr<pinyin_context_t, decltype(&pinyin_fini)> context_;
};

/*
 * Per-input-context editing state: one engine instance per fcitx input
 * context, plus the client-side raw-key buffer.
 *
 * v1 (Phase 2): the offset handed to guess_candidates/choose_candidate
 * stays 0 and a selection always resolves to a commit — the full sentence
 * when the chosen candidate consumes the whole buffer, the candidate's own
 * text otherwise. The choose -> keep composing -> constrained re-decode
 * flow (Phase 4) replaces the partial branch here: it will call
 * pinyin_choose_candidate at the pin offset, keep the buffer, and re-run
 * guess_*, clearing pins with pinyin_clear_constraint on backspace into a
 * pinned run. Constraint behaviour itself stays engine-side, never in
 * this class.
 */
class OxpinyinState final : public InputContextProperty {
public:
    OxpinyinState(OxpinyinEngine *engine, InputContext *ic);
    ~OxpinyinState() override;

    void keyEvent(KeyEvent &keyEvent);
    void reset();

    pinyin_instance_t *instance() { return instance_.get(); }

private:
    friend class OxpinyinCandidateWord;

    // Candidate-list interaction while composing; true when consumed.
    bool handleCandidateKey(KeyEvent &keyEvent);

    // v1 commit: whole-buffer choices take the sentence path (guess ->
    // commit -> train -> remember), partial choices commit the candidate's
    // own text.
    void selectCandidate(size_t index);

    std::string sentence() const;

    void updateUI();
    void resetState();

    std::unique_ptr<pinyin_instance_t, decltype(&pinyin_free_instance)>
        instance_;
    InputContext *ic_;
    OxpinyinEngine *engine_;
    std::string buffer_;   // raw pinyin keys, engine-neutral
    size_t parsedLen_ = 0; // bytes the engine accepted as pinyin
};

class OxpinyinEngineFactory final : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override;
};

} // namespace fcitx

#endif // FCITX5_OXPINYIN_OXPINYIN_H_
