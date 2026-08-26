/*
 * SPDX-FileCopyrightText: 2026 Shenghao Chen <shenghaoc@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Hermetic test stub of the fcitx5-chinese-addons cloudpinyin module. Loaded
 * as the "cloudpinyin" addon only by the cloud-stub test runner, it answers
 * every request SYNCHRONOUSLY with a fixed canned hanzi (a cache-hit model),
 * so the reused CloudPinyinCandidateWord fills in its ctor -- no network,
 * fully deterministic. The class name CloudPinyin is required: the engine
 * reaches the cloud over the addon ABI via call<fcitx::ICloudPinyin::request>,
 * and the DECLARE/EXPORT function tables key off "CloudPinyin::request"; a
 * different class name fails the EXPORT static_assert (resolve would break).
 */
// Header-scoped shadow suppression, same justification as src/oxpinyin.cpp:
// the upstream header shadows its own `pinyin` parameter in a lambda, and
// the module include dir is plain -I, so gcc -Wshadow treats it as ours.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include "cloudpinyin_public.h"
#pragma GCC diagnostic pop
#include "stubhanzi.h"

#include <fcitx-utils/key.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <string>

using namespace fcitx;

class CloudPinyin : public AddonInstance {
public:
    // AddonInstance has only a default ctor (the real cloudpinyin likewise
    // default-constructs its base and uses the manager for its own members);
    // the stub keeps no state, so the manager is unused.
    CloudPinyin(AddonManager *) {}
    ~CloudPinyin() override = default;

    // Cache-hit model: invoke the callback inline with the canned hanzi BEFORE
    // returning. CloudPinyinCandidateWord's ctor calls this via call<>, takes
    // the watch() ref, and fill()s synchronously while constructor_ is still
    // true (so no update()); the candidate is populated by the time the engine
    // inspects it.
    void request(const std::string &pinyin, CloudPinyinCallback callback) {
        callback(pinyin, kStubCloudHanzi);
    }
    const KeyList &toggleKey() const {
        static const KeyList empty;
        return empty;
    }
    void resetError() {}

private:
    FCITX_ADDON_EXPORT_FUNCTION(CloudPinyin, request)
    FCITX_ADDON_EXPORT_FUNCTION(CloudPinyin, toggleKey)
    FCITX_ADDON_EXPORT_FUNCTION(CloudPinyin, resetError)
};

class CloudPinyinFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        return new CloudPinyin(manager);
    }
};

FCITX_ADDON_FACTORY_V2(cloudpinyin, CloudPinyinFactory);
