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
 * context. Phase 1 owns the instance only; the raw-key buffer and the
 * composition state machine arrive with the Phase 2 engine loop, and the
 * partial-choice hook (Phase 4: choose -> keep composing -> constrained
 * re-decode) resolves in this same class.
 */
class OxpinyinState final : public InputContextProperty {
public:
    OxpinyinState(OxpinyinEngine *engine, InputContext *ic);
    ~OxpinyinState() override;

    void keyEvent(KeyEvent &keyEvent);

    pinyin_instance_t *instance() { return instance_.get(); }

private:
    std::unique_ptr<pinyin_instance_t, decltype(&pinyin_free_instance)>
        instance_;
};

class OxpinyinEngineFactory final : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override;
};

} // namespace fcitx

#endif // FCITX5_OXPINYIN_OXPINYIN_H_
