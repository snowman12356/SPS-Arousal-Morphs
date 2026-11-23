#pragma once

#include "SPSAPI.h"

namespace SPSArousalMorphs {
class SPSBridge {
public:
    bool Connect();
    SPS::API::Result ReadArousal(float& value) const;
    SPS::API::Result ReadErectThreshold(float& value) const;
    // True while the player is in a SexLab/OStim scene, including SPS's
    // scene-end delay. With V3 this reads IsActorInScene, which our own holds do
    // not hide. Without V3 it falls back to GetState, where an active API
    // request (ours included) hides the scene.
    [[nodiscard]] bool IsSceneControllingPlayer() const;
    // V4: player scene role.
    [[nodiscard]] bool HasSceneRole() const { return apiV4_ != nullptr; }
    [[nodiscard]] SPS::API::SceneRole ReadPlayerSceneRole() const;
    // V5: scene role for loaded NPCs. Game thread only. The first call for an
    // NPC returns NotReady while SPS asks the scene framework.
    [[nodiscard]] bool HasActorSceneRole() const { return apiV5_ != nullptr; }
    SPS::API::Result ReadActorSceneRole(std::uint32_t formID, SPS::API::SceneRole& role) const;
    // V4: arousal/scene notifications for the player, on the game thread.
    bool RegisterActorEvents(SPS::API::ActorEventCallback callback, void* userData);
    // V3 per-actor reads. Game thread only. Optional: the player does not need them.
    [[nodiscard]] bool HasActorReadback() const { return apiV3_ != nullptr; }
    SPS::API::Result ReadActorArousal(std::uint32_t formID, float& value) const;
    SPS::API::Result ReadActorInScene(std::uint32_t formID, bool& value) const;
    // Ask SPS to keep SMP while the morphs extend. With V4 this also takes a
    // rise hold so SPS does not start its CBPC rise meanwhile.
    bool HoldSMP();
    void ReleaseSMPHold();
    // Renew the V4 rise hold before SPS's 10 s limit ends it. Call every tick.
    void RefreshRiseHold();

    [[nodiscard]] bool IsConnected() const { return api_ != nullptr; }
    // For the menu thread: true once SPS's API was found and accepted.
    [[nodiscard]] bool IsFound() const { return found_.load(std::memory_order_acquire); }
    [[nodiscard]] bool IsHoldingSMP() const { return smpRequest_ != 0; }

private:
    void ConnectV3(HMODULE module);
    void ConnectV4(HMODULE module);
    void ConnectV5(HMODULE module);
    void TakeRiseHold();
    void ReleaseRiseHold();

    std::atomic<bool> found_{ false };
    const SPS::API::InterfaceV2* api_{ nullptr };
    const SPS::API::InterfaceV3* apiV3_{ nullptr };
    const SPS::API::InterfaceV4* apiV4_{ nullptr };
    const SPS::API::InterfaceV5* apiV5_{ nullptr };
    SPS::API::RequestHandle riseHold_{ 0 };
    std::chrono::steady_clock::time_point riseHoldTakenAt_{};
    SPS::API::RequestHandle smpRequest_{ 0 };
    bool holdAttempted_{ false };
};
}
