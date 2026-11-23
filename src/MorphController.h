#pragma once

#include "SKEEAPI.h"
#include "Settings.h"

namespace SPSArousalMorphs {
class MorphController {
public:
    bool Connect(SKEE::IInterfaceMap* interfaceMap);
    bool ApplyMorphs(const std::vector<MorphSetting>& morphs, float extension);
    bool ClearOwnedMorphs();

    // NPC variants. Game thread only, like everything that writes morphs.
    bool ApplyActorMorphs(RE::Actor* actor, const std::vector<MorphSetting>& morphs, float extension);
    void ClearActorMorphs(RE::FormID formID);
    void LogActorDiagnostics(RE::Actor* actor, const std::vector<MorphSetting>& morphs);
    // Clears the addon's key from every actor RaceMenu tracks except the player
    // and the listed form IDs, including actors that are not currently loaded.
    void ClearOwnedMorphsExcept(const std::unordered_set<RE::FormID>& keep);

    [[nodiscard]] bool IsApplied() const { return applied_.load(); }

private:
    bool WriteMorphs(RE::Actor* actor, const std::vector<MorphSetting>& morphs, float extension);
    void ClearKeyFrom(RE::TESObjectREFR* actor);

    SKEE::IBodyMorphInterface* bodyMorph_{ nullptr };
    std::atomic<bool> applied_{ false };
    std::atomic<bool> applicationErrorLogged_{ false };
};
}
