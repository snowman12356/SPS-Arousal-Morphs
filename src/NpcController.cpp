#include "PCH.h"

#include "NpcController.h"

#include "MorphController.h"
#include "SPSBridge.h"

namespace SPSArousalMorphs {
namespace {
using namespace std::chrono_literals;
// NPCs update at half the player's rate; several may be growing at once.
constexpr auto kTickInterval = 100ms;
// SPS asks NPC providers from a cache, so reading faster gains nothing.
constexpr auto kReadInterval = 1s;
// The first scene answer for an actor takes about a second. If SPS still has
// none after this, carry on from arousal instead of waiting indefinitely.
constexpr auto kSceneAnswerTimeout = 5s;
constexpr float kExtensionEpsilon = 0.001F;

RE::FormID ResolveFormID(const NpcSetting& npc)
{
    auto* data = RE::TESDataHandler::GetSingleton();
    return data ? data->LookupFormID(npc.localFormID, npc.plugin) : 0;
}
}

NpcController::State& NpcController::StateFor(const NpcSetting& npc)
{
    auto& state = states_[npc.Key()];
    if (!state.resolved) {
        state.resolved = true;
        state.formID = ResolveFormID(npc);
        if (state.formID == 0) {
            logger::warn("NPC {} ({}:{:08X}) was not found in this load order",
                npc.name, npc.plugin, npc.localFormID);
        }
    }
    return state;
}

void NpcController::BeginGameSession()
{
    states_.clear();
    {
        std::scoped_lock guard(statusLock_);
        statuses_.clear();
    }
    lastTick_ = Clock::now();
    ReconcileOwnedMorphs(settings_.Snapshot());
}

void NpcController::EndGameSession()
{
    // SKEE keeps NPC morphs in the save, so there is nothing to clear here.
    states_.clear();
    std::scoped_lock guard(statusLock_);
    statuses_.clear();
}

void NpcController::OnSettingsChanged()
{
    ReconcileOwnedMorphs(settings_.Snapshot());
}

void NpcController::ReconcileOwnedMorphs(const Settings& settings)
{
    std::unordered_set<RE::FormID> keep;
    std::unordered_set<std::string> configured;
    if (settings.enabled) {
        for (const auto& npc : settings.npcs) {
            if (!npc.enabled) {
                continue;
            }
            auto& state = StateFor(npc);
            configured.insert(npc.Key());
            if (state.formID != 0) {
                keep.insert(state.formID);
            }
            if (state.appliedMorphs != npc.morphs) {
                // A renamed or removed morph must not keep its old value.
                if (!state.appliedMorphs.empty() && state.formID != 0) {
                    morphs_.ClearActorMorphs(state.formID);
                }
                state.appliedMorphs = npc.morphs;
                state.snapToTarget = true;
            }
        }
    }
    morphs_.ClearOwnedMorphsExcept(keep);
    std::erase_if(states_, [&configured](const auto& item) {
        return !configured.contains(item.first);
    });
}

void NpcController::Tick()
{
    const auto now = Clock::now();
    if (now - lastTick_ < kTickInterval) {
        return;
    }
    const float elapsed = std::clamp(std::chrono::duration<float>(now - lastTick_).count(), 0.0F, 0.25F);
    lastTick_ = now;

    const auto settings = settings_.Snapshot();
    for (const auto& npc : settings.npcs) {
        Process(npc, elapsed, now);
    }
}

void NpcController::Process(const NpcSetting& npc, float elapsed, Clock::time_point now)
{
    const auto key = npc.Key();
    if (!npc.enabled) {
        Publish(key, State{}, "Disabled");
        return;
    }
    auto& state = StateFor(npc);
    if (!sps_.HasActorReadback()) {
        Publish(key, state, "Needs SPS with API V3");
        return;
    }
    if (state.formID == 0) {
        Publish(key, state, "Not found in this load order");
        return;
    }
    if (npc.morphs.empty()) {
        Publish(key, state, "No valid morphs configured");
        return;
    }

    auto* actor = RE::TESForm::LookupByID<RE::Actor>(state.formID);
    if (!actor || !actor->Is3DLoaded() || !actor->Get3D()) {
        state.loaded = false;
        state.snapToTarget = true;
        state.hasArousal = false;
        state.sceneKnown = false;
        Publish(key, state, "Not loaded nearby");
        return;
    }
    if (!state.loaded) {
        state.loaded = true;
        state.loadedAt = now;
        state.nextRead = now;
    }

    if (now >= state.nextRead) {
        state.nextRead = now + kReadInterval;
        float arousal = 0.0F;
        if (sps_.ReadActorArousal(state.formID, arousal) == SPS::API::Result::Success) {
            state.arousal = std::clamp(arousal, 0.0F, 100.0F);
            state.hasArousal = true;
        }
        bool inScene = false;
        if (sps_.ReadActorInScene(state.formID, inScene) == SPS::API::Result::Success) {
            state.inScene = inScene;
            state.sceneKnown = true;
        }
    }

    const bool inScene = state.sceneKnown && state.inScene;
    const bool sceneSettled = state.sceneKnown || now - state.loadedAt >= kSceneAnswerTimeout;
    if (!inScene && !state.hasArousal) {
        Publish(key, state, "Waiting for arousal from SPS");
        return;
    }
    if (!sceneSettled) {
        Publish(key, state, "Waiting for scene status from SPS");
        return;
    }

    const char* text = "Retracted";
    if (inScene) {
        state.targetExtension = 1.0F;
        text = "Scene active - held fully extended";
    } else if (state.arousal >= npc.fullyExtendedArousal) {
        state.targetExtension = 1.0F;
        text = "Fully extended";
    } else if (state.arousal <= npc.startGrowingArousal) {
        state.targetExtension = 0.0F;
    } else {
        state.targetExtension = std::clamp(
            (state.arousal - npc.startGrowingArousal) /
                (npc.fullyExtendedArousal - npc.startGrowingArousal),
            0.0F, 1.0F);
        text = "Progressive growth";
    }

    if (state.snapToTarget) {
        // First sight of this NPC since it loaded: nothing to animate from.
        if (morphs_.ApplyActorMorphs(actor, npc.morphs, state.targetExtension)) {
            state.currentExtension = state.targetExtension;
            state.snapToTarget = false;
            logger::info("NPC {} morphs written at {:.0f}% extension (arousal {:.1f}, in scene {})",
                npc.name, state.targetExtension * 100.0F, state.arousal, inScene);
            morphs_.LogActorDiagnostics(actor, npc.morphs);
        } else {
            logger::warn("NPC {}: RaceMenu rejected the morph write", npc.name);
        }
    } else {
        const float difference = state.targetExtension - state.currentExtension;
        if (std::abs(difference) > kExtensionEpsilon) {
            const float duration = difference > 0.0F ? npc.extensionSeconds : npc.retractionSeconds;
            float next = state.currentExtension +
                std::copysign(std::min(std::abs(difference), elapsed / duration), difference);
            if (std::abs(state.targetExtension - next) <= kExtensionEpsilon) {
                next = state.targetExtension;
            }
            if (morphs_.ApplyActorMorphs(actor, npc.morphs, next)) {
                state.currentExtension = next;
            }
        }
    }
    Publish(key, state, text);
}

void NpcController::Publish(const std::string& key, const State& state, const char* text)
{
    if (auto& logged = lastLoggedText_[key]; logged != text) {
        logged = text;
        logger::info("NPC {} state: {}", key, text);
    }

    NpcStatus status;
    status.state = text;
    status.hasArousal = state.hasArousal;
    status.arousal = state.arousal;
    status.inScene = state.sceneKnown && state.inScene;
    status.currentExtension = state.currentExtension;
    status.targetExtension = state.targetExtension;
    std::scoped_lock guard(statusLock_);
    statuses_[key] = status;
}

std::optional<NpcStatus> NpcController::Status(const std::string& key) const
{
    std::scoped_lock guard(statusLock_);
    const auto found = statuses_.find(key);
    return found != statuses_.end() ? std::optional<NpcStatus>(found->second) : std::nullopt;
}
}
