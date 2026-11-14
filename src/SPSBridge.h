#pragma once

#include "SPSAPI.h"

namespace SPSArousalMorphs {
class SPSBridge {
public:
    bool Connect();
    SPS::API::Result ReadArousal(float& value) const;
    SPS::API::Result ReadErectThreshold(float& value) const;
    // True while SPS reports a SexLab/OStim scene as controlling the player,
    // including its scene-end delay. SPS reports an active API request instead
    // of the scene, so this reads false while any request (ours included) is held.
    [[nodiscard]] bool IsSceneControllingPlayer() const;
    // V3 per-actor reads. Game thread only. Optional: the player does not need them.
    [[nodiscard]] bool HasActorReadback() const { return apiV3_ != nullptr; }
    SPS::API::Result ReadActorArousal(std::uint32_t formID, float& value) const;
    SPS::API::Result ReadActorInScene(std::uint32_t formID, bool& value) const;
    bool HoldSMP();
    void ReleaseSMPHold();

    [[nodiscard]] bool IsConnected() const { return api_ != nullptr; }
    [[nodiscard]] bool IsHoldingSMP() const { return smpRequest_ != 0; }

private:
    void ConnectV3(HMODULE module);

    const SPS::API::InterfaceV2* api_{ nullptr };
    const SPS::API::InterfaceV3* apiV3_{ nullptr };
    SPS::API::RequestHandle smpRequest_{ 0 };
    bool holdAttempted_{ false };
};
}
