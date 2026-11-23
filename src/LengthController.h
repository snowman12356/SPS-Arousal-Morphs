#pragma once

#include "SPSAPI.h"
#include "Settings.h"

namespace SPSArousalMorphs {
class MorphController;
class NpcController;
class SPSBridge;

enum class LengthState {
    Disabled,
    WaitingForGame,
    WaitingForSPS,
    WaitingForPlayer,
    NoMorphs,
    Retracted,
    GrowthRange,
    FullLength,
    SceneExtended,
    SceneFollowingArousal
};

struct LengthStatus {
    LengthState state{ LengthState::Disabled };
    bool dataReady{ false };
    float arousal{ 0.0F };
    float erectThreshold{ 0.0F };
    bool holdingSMP{ false };
    bool settlingBeforeRelease{ false };
    float currentExtension{ 1.0F };
    float targetExtension{ 1.0F };
};

class LengthController {
public:
    LengthController(SPSBridge& sps, MorphController& morphs, NpcController& npcs, SettingsStore& settings) :
        sps_(sps), morphs_(morphs), npcs_(npcs), settings_(settings) {}

    void StartWorker();
    // Registers for SPS V4 arousal events. Call once SPS is connected.
    void ConnectEvents();
    void BeginGameSession();
    void EndGameSession();
    void OnSettingsChanged();
    // For the menu: runs OnSettingsChanged on the SKSE task thread, where Tick
    // also runs, so controller and SPS-hold state are only touched there.
    void QueueSettingsChanged();

    [[nodiscard]] LengthStatus Status() const;
    [[nodiscard]] static const char* StateName(LengthState state);

private:
    static void __cdecl OnSPSEvent(const SPS::API::ActorEvent* event, void* userData);
    void HandleArousalEvent(float arousal, bool inScene);
    bool eventsConnected_{ false };
    void Run(std::stop_token stop);
    void QueueTick();
    void Tick(std::uint64_t generation);
    void Publish(LengthState state, bool dataReady, float arousal, float erectThreshold = 0.0F);
    void LogStateChange(LengthState state, float arousal, float target);

    SPSBridge& sps_;
    MorphController& morphs_;
    NpcController& npcs_;
    SettingsStore& settings_;
    std::jthread worker_;
    std::atomic<bool> workerStarted_{ false };
    std::atomic<bool> gameReady_{ false };
    std::atomic<bool> taskQueued_{ false };
    std::atomic<std::uint64_t> generation_{ 0 };
    mutable std::mutex statusLock_;
    LengthStatus status_;
    float currentExtension_{ 1.0F };
    float targetExtension_{ 1.0F };
    // Arousal-driven target from the latest tick outside a scene; a scene where
    // the player is not penetrating holds this length.
    std::optional<float> preSceneTarget_;
    LengthState lastLoggedState_{ LengthState::Disabled };
    std::chrono::steady_clock::time_point lastTick_{};
    std::optional<std::chrono::steady_clock::time_point> fullyExtendedSince_;
};
}
