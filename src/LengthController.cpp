#include "PCH.h"

#include "LengthController.h"

#include "MorphController.h"
#include "NpcController.h"
#include "SPSBridge.h"

namespace SPSArousalMorphs {
namespace {
using namespace std::chrono_literals;
constexpr auto kTickInterval = 50ms;
constexpr float kExtensionEpsilon = 0.001F;
constexpr float kFullyRetracted = 0.0F;
constexpr float kFullyExtended = 1.0F;
constexpr auto kSMPReleaseSettleDelay = 200ms;
}

void LengthController::StartWorker()
{
    if (workerStarted_.exchange(true)) {
        return;
    }
    worker_ = std::jthread([this](std::stop_token stop) { Run(stop); });
}

void LengthController::BeginGameSession()
{
    sps_.ReleaseSMPHold();
    ++generation_;
    taskQueued_ = false;
    gameReady_ = true;
    currentExtension_ = kFullyExtended;
    targetExtension_ = kFullyExtended;
    lastTick_ = std::chrono::steady_clock::now();
    fullyExtendedSince_.reset();
    lastLoggedState_ = LengthState::Disabled;
    morphs_.ClearOwnedMorphs();
    npcs_.BeginGameSession();

    const auto settings = settings_.Snapshot();
    Publish(settings.enabled ? LengthState::WaitingForSPS : LengthState::Disabled,
        false, 0.0F);
    if (settings.enabled) {
        logger::info("Addon enabled for the new game session; waiting for SPS live data");
    }
}

void LengthController::EndGameSession()
{
    gameReady_ = false;
    ++generation_;
    taskQueued_ = false;
    sps_.ReleaseSMPHold();
    fullyExtendedSince_.reset();
    morphs_.ClearOwnedMorphs();
    npcs_.EndGameSession();
    currentExtension_ = kFullyExtended;
    targetExtension_ = kFullyExtended;
    Publish(settings_.Snapshot().enabled ? LengthState::WaitingForGame : LengthState::Disabled,
        false, 0.0F);
}

void LengthController::OnSettingsChanged()
{
    const auto settings = settings_.Snapshot();
    morphs_.ClearOwnedMorphs();
    npcs_.OnSettingsChanged();
    fullyExtendedSince_.reset();
    if (!settings.enabled) {
        ++generation_;
        sps_.ReleaseSMPHold();
        currentExtension_ = kFullyExtended;
        targetExtension_ = kFullyExtended;
        lastLoggedState_ = LengthState::Disabled;
        Publish(LengthState::Disabled, false, 0.0F);
        logger::info("Addon disabled; its BodyMorph source was cleared");
        return;
    }

    lastTick_ = std::chrono::steady_clock::now();
    logger::info(
        "Addon enabled/settings applied: starts growing at {:.0f}, fully extended at {:.0f}, extension {:.1f}s, retraction {:.1f}s, morphs={}",
        settings.startGrowingArousal, settings.fullyExtendedArousal,
        settings.extensionSeconds, settings.retractionSeconds, settings.morphs.size());
    if (!gameReady_) {
        Publish(LengthState::WaitingForGame, false, 0.0F);
    }
}

void LengthController::QueueSettingsChanged()
{
    auto* tasks = SKSE::GetTaskInterface();
    if (!tasks) {
        logger::warn("SKSE task interface unavailable; applying settings change directly");
        OnSettingsChanged();
        return;
    }
    tasks->AddTask([this]() { OnSettingsChanged(); });
}

LengthStatus LengthController::Status() const
{
    std::scoped_lock guard(statusLock_);
    return status_;
}

const char* LengthController::StateName(LengthState state)
{
    switch (state) {
    case LengthState::Disabled: return "Disabled";
    case LengthState::WaitingForGame: return "Waiting for a loaded game";
    case LengthState::WaitingForSPS: return "Waiting for SPS live data";
    case LengthState::WaitingForPlayer: return "Waiting for the player body";
    case LengthState::NoMorphs: return "No valid morphs configured";
    case LengthState::Retracted: return "Retracted - waiting to grow";
    case LengthState::GrowthRange: return "Progressive growth";
    case LengthState::FullLength: return "Fully extended arousal reached";
    case LengthState::SceneExtended: return "Scene active - held fully extended";
    default: return "Unknown";
    }
}

void LengthController::Run(std::stop_token stop)
{
    while (!stop.stop_requested()) {
        std::this_thread::sleep_for(kTickInterval);
        if (stop.stop_requested()) {
            return;
        }
        if (gameReady_.load() && settings_.Snapshot().enabled) {
            QueueTick();
        }
    }
}

void LengthController::QueueTick()
{
    if (taskQueued_.exchange(true)) {
        return;
    }
    const auto generation = generation_.load();
    auto* tasks = SKSE::GetTaskInterface();
    if (!tasks) {
        taskQueued_ = false;
        return;
    }
    tasks->AddTask([this, generation]() {
        if (generation == generation_.load()) {
            Tick(generation);
            if (gameReady_.load() && settings_.Snapshot().enabled) {
                npcs_.Tick();
            }
        }
        taskQueued_ = false;
    });
}

void LengthController::Tick(std::uint64_t generation)
{
    if (generation != generation_.load() || !gameReady_.load()) {
        return;
    }
    const auto settings = settings_.Snapshot();
    if (!settings.enabled) {
        return;
    }
    if (settings.morphs.empty()) {
        sps_.ReleaseSMPHold();
        fullyExtendedSince_.reset();
        lastTick_ = std::chrono::steady_clock::now();
        Publish(LengthState::NoMorphs, false, 0.0F);
        LogStateChange(LengthState::NoMorphs, 0.0F, targetExtension_);
        return;
    }

    float arousal = 0.0F;
    float erectThreshold = 0.0F;
    if (!sps_.IsConnected() ||
        sps_.ReadArousal(arousal) != SPS::API::Result::Success ||
        sps_.ReadErectThreshold(erectThreshold) != SPS::API::Result::Success) {
        sps_.ReleaseSMPHold();
        fullyExtendedSince_.reset();
        lastTick_ = std::chrono::steady_clock::now();
        Publish(LengthState::WaitingForSPS, false, 0.0F);
        LogStateChange(LengthState::WaitingForSPS, 0.0F, targetExtension_);
        return;
    }

    arousal = std::clamp(arousal, 0.0F, 100.0F);
    erectThreshold = std::clamp(erectThreshold, 0.0F, 100.0F);
    LengthState state = LengthState::Retracted;

    const bool sceneActive = sps_.IsSceneControllingPlayer();
    if (sceneActive) {
        targetExtension_ = kFullyExtended;
        state = LengthState::SceneExtended;
    } else if (arousal >= settings.fullyExtendedArousal) {
        targetExtension_ = kFullyExtended;
        state = LengthState::FullLength;
    } else if (arousal <= settings.startGrowingArousal) {
        targetExtension_ = kFullyRetracted;
        state = LengthState::Retracted;
    } else {
        targetExtension_ = std::clamp(
            (arousal - settings.startGrowingArousal) /
                (settings.fullyExtendedArousal - settings.startGrowingArousal),
            0.0F, 1.0F);
        state = LengthState::GrowthRange;
    }

    const bool spsWantsErectPhysics = arousal >= erectThreshold;
    const bool extensionWasIncomplete =
        targetExtension_ < kFullyExtended - kExtensionEpsilon ||
        currentExtension_ < kFullyExtended - kExtensionEpsilon;
    if (sceneActive || !settings.smpHold) {
        // SPS's scene decision owns physics; never request SMP over it.
        // With the hold turned off, SPS alone decides physics at all times.
        sps_.ReleaseSMPHold();
        fullyExtendedSince_.reset();
    } else if (spsWantsErectPhysics && extensionWasIncomplete) {
        sps_.HoldSMP();
    } else if (!spsWantsErectPhysics) {
        sps_.ReleaseSMPHold();
        fullyExtendedSince_.reset();
    }

    const auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - lastTick_).count();
    lastTick_ = now;
    elapsed = std::clamp(elapsed, 0.0F, 0.25F);

    const float difference = targetExtension_ - currentExtension_;
    if (std::abs(difference) > kExtensionEpsilon) {
        const float duration = difference > 0.0F ? settings.extensionSeconds : settings.retractionSeconds;
        const float step = elapsed / duration;
        float next = currentExtension_ +
            std::copysign(std::min(std::abs(difference), step), difference);
        if (std::abs(targetExtension_ - next) <= kExtensionEpsilon) {
            next = targetExtension_;
        }
        if (morphs_.ApplyMorphs(settings.morphs, next)) {
            currentExtension_ = next;
        } else {
            state = LengthState::WaitingForPlayer;
        }
    } else {
        currentExtension_ = targetExtension_;
        if (!morphs_.IsApplied()) {
            if (!morphs_.ApplyMorphs(settings.morphs, currentExtension_)) {
                state = LengthState::WaitingForPlayer;
            }
        }
    }

    const bool fullyExtended =
        targetExtension_ >= kFullyExtended - kExtensionEpsilon &&
        currentExtension_ >= kFullyExtended - kExtensionEpsilon;
    if (spsWantsErectPhysics && sps_.IsHoldingSMP()) {
        if (fullyExtended) {
            if (!fullyExtendedSince_) {
                fullyExtendedSince_ = now;
                logger::info("Full extension reached; holding SMP for {} ms before release",
                    kSMPReleaseSettleDelay.count());
            } else if (now - *fullyExtendedSince_ >= kSMPReleaseSettleDelay) {
                sps_.ReleaseSMPHold();
                fullyExtendedSince_.reset();
            }
        } else {
            fullyExtendedSince_.reset();
        }
    } else if (!sps_.IsHoldingSMP()) {
        fullyExtendedSince_.reset();
    }

    Publish(state, true, arousal, erectThreshold);
    LogStateChange(state, arousal, targetExtension_);
}

void LengthController::Publish(
    LengthState state, bool dataReady, float arousal, float erectThreshold)
{
    std::scoped_lock guard(statusLock_);
    status_.state = state;
    status_.dataReady = dataReady;
    status_.arousal = arousal;
    status_.erectThreshold = erectThreshold;
    status_.holdingSMP = sps_.IsHoldingSMP();
    status_.settlingBeforeRelease = fullyExtendedSince_.has_value();
    status_.currentExtension = currentExtension_;
    status_.targetExtension = targetExtension_;
}

void LengthController::LogStateChange(LengthState state, float arousal, float target)
{
    if (state == lastLoggedState_) {
        return;
    }
    lastLoggedState_ = state;
    if (state == LengthState::WaitingForSPS) {
        logger::info("Waiting for SPS live arousal and erect-threshold data");
    } else if (state == LengthState::WaitingForPlayer) {
        logger::info("Waiting for the player body before applying configured morphs");
    } else {
        logger::info("Extension state changed: {} (arousal {:.1f}, target {:.0f}%)",
            StateName(state), arousal, target * 100.0F);
    }
}
}
