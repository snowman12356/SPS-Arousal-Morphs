#pragma once

#include "SPSAPI.h"
#include "Settings.h"

namespace SPSArousalMorphs {
class MorphController;
class SPSBridge;

struct NpcStatus {
    const char* state{ "Waiting for a loaded game" };
    bool hasArousal{ false };
    float arousal{ 0.0F };
    bool inScene{ false };
    float currentExtension{ 0.0F };
    float targetExtension{ 0.0F };
};

// Drives the morphs of NPCs added through the menu. Everything except Status
// runs on the SKSE task thread. NPCs make no physics requests.
class NpcController {
public:
    NpcController(SPSBridge& sps, MorphController& morphs, SettingsStore& settings) :
        sps_(sps), morphs_(morphs), settings_(settings) {}

    void BeginGameSession();
    void EndGameSession();
    void OnSettingsChanged();
    void Tick();

    [[nodiscard]] std::optional<NpcStatus> Status(const std::string& key) const;

private:
    using Clock = std::chrono::steady_clock;

    struct State {
        bool resolved{ false };
        RE::FormID formID{ 0 };
        bool loaded{ false };
        bool snapToTarget{ true };
        bool hasArousal{ false };
        float arousal{ 0.0F };
        bool sceneKnown{ false };
        bool inScene{ false };
        SPS::API::SceneRole role{ SPS::API::SceneRole::Unknown };
        float currentExtension{ 0.0F };
        float targetExtension{ 0.0F };
        std::optional<float> preSceneTarget;
        Clock::time_point loadedAt{};
        Clock::time_point nextRead{};
        std::vector<MorphSetting> appliedMorphs;
    };

    State& StateFor(const NpcSetting& npc);
    void Process(const NpcSetting& npc, float elapsed, Clock::time_point now);
    void Publish(const std::string& key, const State& state, const char* text);
    void ReconcileOwnedMorphs(const Settings& settings);

    SPSBridge& sps_;
    MorphController& morphs_;
    SettingsStore& settings_;
    // Settings::sceneMode from the latest Tick, read by Process on the same thread.
    int sceneMode_{ kSceneFollowRole };
    std::unordered_map<std::string, State> states_;
    Clock::time_point lastTick_{};
    std::unordered_map<std::string, std::string> lastLoggedText_;

    mutable std::mutex statusLock_;
    std::unordered_map<std::string, NpcStatus> statuses_;
};
}
