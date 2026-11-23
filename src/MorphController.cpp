#include "PCH.h"

#include "MorphController.h"

namespace SPSArousalMorphs {
namespace {
constexpr auto kMorphKey = "SPSArousalMorphs";
// Source key used before the rename to SPS Arousal Morphs. SKEE keeps morph
// values in the save, so contributions under this key are cleared whenever the
// addon clears its own, which includes every game load.
constexpr auto kLegacyMorphKey = "SPSRetractableAddon";

RE::PlayerCharacter* ReadyPlayer()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    return player && player->Is3DLoaded() && player->Get3D() ? player : nullptr;
}

class ActorCollector final : public SKEE::IBodyMorphInterface::ActorVisitor {
public:
    void Visit(RE::TESObjectREFR* refr) override
    {
        if (refr) {
            actors_.push_back(refr);
        }
    }

    std::vector<RE::TESObjectREFR*>& Actors() { return actors_; }

private:
    std::vector<RE::TESObjectREFR*> actors_;
};
}

bool MorphController::Connect(SKEE::IInterfaceMap* interfaceMap)
{
    if (bodyMorph_) {
        return true;
    }
    if (!interfaceMap) {
        logger::error("RaceMenu/SKEE connection failed: interface map is null");
        return false;
    }

    auto* pluginInterface = interfaceMap->QueryInterface("BodyMorph");
    if (!pluginInterface) {
        logger::error("RaceMenu/SKEE BodyMorph interface was not found");
        return false;
    }

    const auto interfaceVersion = pluginInterface->GetVersion();
    if (interfaceVersion < SKEE::IBodyMorphInterface::kCurrentPluginVersion) {
        logger::error("RaceMenu/SKEE BodyMorph interface V{} is incompatible; V{} is required",
            interfaceVersion, SKEE::IBodyMorphInterface::kCurrentPluginVersion);
        return false;
    }

    bodyMorph_ = static_cast<SKEE::IBodyMorphInterface*>(pluginInterface);
    logger::info("RaceMenu/SKEE BodyMorph interface V{} detected", interfaceVersion);
    return true;
}

bool MorphController::ApplyMorphs(const std::vector<MorphSetting>& morphs, float extension)
{
    auto* player = ReadyPlayer();
    if (!bodyMorph_) {
        if (!applicationErrorLogged_.exchange(true))
            logger::error("Morph application rejected: RaceMenu/SKEE BodyMorph is unavailable");
        return false;
    }
    if (!player) {
        if (!applicationErrorLogged_.exchange(true))
            logger::warn("Morph application deferred: player 3D is not ready");
        return false;
    }
    if (morphs.empty()) {
        if (!applicationErrorLogged_.exchange(true))
            logger::error("Morph application rejected: no valid morphs are configured");
        return false;
    }

    if (!WriteMorphs(player, morphs, extension)) {
        return false;
    }

    applied_ = true;
    applicationErrorLogged_ = false;
    return true;
}

bool MorphController::WriteMorphs(RE::Actor* actor, const std::vector<MorphSetting>& morphs, float extension)
{
    extension = std::clamp(extension, 0.0F, 1.0F);
    bool succeeded = true;
    for (const auto& morph : morphs) {
        if (morph.name.empty()) {
            continue;
        }
        if (!morph.enabled) {
            if (bodyMorph_->HasBodyMorph(actor, morph.name.c_str(), kMorphKey)) {
                bodyMorph_->ClearMorph(actor, morph.name.c_str(), kMorphKey);
            }
            continue;
        }
        const auto target = morph.retractedValue +
            (morph.extendedValue - morph.retractedValue) * extension;
        const auto previousOwned = bodyMorph_->GetMorph(actor, morph.name.c_str(), kMorphKey);
        const auto otherSources = bodyMorph_->GetBodyMorphs(actor, morph.name.c_str()) - previousOwned;
        const auto ownedValue = target - otherSources;

        bodyMorph_->SetMorph(actor, morph.name.c_str(), kMorphKey, ownedValue);
        const auto stored = bodyMorph_->GetMorph(actor, morph.name.c_str(), kMorphKey);
        if (std::abs(stored - ownedValue) > 0.0001F) {
            succeeded = false;
            if (!applicationErrorLogged_.exchange(true))
                logger::error("RaceMenu/SKEE did not retain {} contribution {:.2f}; read back {:.2f}",
                    morph.name, ownedValue, stored);
        }
    }

    // This runs on the SKSE game task. Apply each interpolated value now so
    // RaceMenu cannot coalesce the transition into a single final update.
    bodyMorph_->ApplyBodyMorphs(actor, false);
    return succeeded;
}

bool MorphController::ApplyActorMorphs(RE::Actor* actor, const std::vector<MorphSetting>& morphs, float extension)
{
    return bodyMorph_ && actor && WriteMorphs(actor, morphs, extension);
}

void MorphController::LogActorDiagnostics(RE::Actor* actor, const std::vector<MorphSetting>& morphs)
{
    if (!bodyMorph_ || !actor) {
        return;
    }
    for (const auto& morph : morphs) {
        logger::info("  morph {}: addon value {:.2f}, total from all sources {:.2f}", morph.name,
            bodyMorph_->GetMorph(actor, morph.name.c_str(), kMorphKey),
            bodyMorph_->GetBodyMorphs(actor, morph.name.c_str()));
    }
    std::size_t shapes = 0;
    std::size_t shapesWithTri = 0;
    RE::BSVisit::TraverseScenegraphGeometries(actor->Get3D(), [&](RE::BSGeometry* geometry) {
        ++shapes;
        const auto* tri = geometry->GetExtraData<RE::NiStringExtraData>("BODYTRI");
        if (tri && tri->value) {
            ++shapesWithTri;
            logger::info("  mesh {}: BODYTRI {}", geometry->name.c_str(), tri->value);
        }
        return RE::BSVisit::BSVisitControl::kContinue;
    });
    logger::info("  {} of {} loaded mesh shapes have a BODYTRI", shapesWithTri, shapes);
}

void MorphController::ClearKeyFrom(RE::TESObjectREFR* actor)
{
    bodyMorph_->ClearBodyMorphKeys(actor, kMorphKey);
    if (actor->Is3DLoaded() && actor->Get3D()) {
        bodyMorph_->ApplyBodyMorphs(actor, true);
    }
}

void MorphController::ClearActorMorphs(RE::FormID formID)
{
    if (!bodyMorph_) {
        return;
    }
    if (auto* actor = RE::TESForm::LookupByID<RE::TESObjectREFR>(formID);
        actor && bodyMorph_->HasBodyMorphKey(actor, kMorphKey)) {
        ClearKeyFrom(actor);
    }
}

void MorphController::ClearOwnedMorphsExcept(const std::unordered_set<RE::FormID>& keep)
{
    if (!bodyMorph_) {
        return;
    }
    // Collected first: SKEE holds its actor-morph lock while visiting.
    ActorCollector actors;
    bodyMorph_->VisitActors(actors);
    std::size_t cleared = 0;
    for (auto* actor : actors.Actors()) {
        if (actor->IsPlayerRef() || keep.contains(actor->GetFormID()) ||
            !bodyMorph_->HasBodyMorphKey(actor, kMorphKey)) {
            continue;
        }
        ClearKeyFrom(actor);
        ++cleared;
    }
    if (cleared != 0) {
        logger::info("Cleared addon morphs from {} NPC(s) no longer configured", cleared);
    }
}

bool MorphController::ClearOwnedMorphs()
{
    if (!bodyMorph_) {
        logger::warn("Could not clear addon morph: RaceMenu/SKEE BodyMorph is unavailable");
        return false;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        logger::warn("Could not clear addon morph: player is unavailable");
        return false;
    }
    const bool hasLegacy = bodyMorph_->HasBodyMorphKey(player, kLegacyMorphKey);
    if (!bodyMorph_->HasBodyMorphKey(player, kMorphKey) && !hasLegacy) {
        applied_ = false;
        return true;
    }

    bodyMorph_->ClearBodyMorphKeys(player, kMorphKey);
    if (hasLegacy) {
        bodyMorph_->ClearBodyMorphKeys(player, kLegacyMorphKey);
        logger::info("Cleared morphs left by the pre-rename source key {}", kLegacyMorphKey);
    }
    applied_ = false;
    if (player->Is3DLoaded() && player->Get3D()) {
        bodyMorph_->ApplyBodyMorphs(player, true);
        logger::info("Cleared all addon-owned morphs for source {}", kMorphKey);
        return true;
    }

    logger::info("Cleared addon-owned morph source {}; player 3D was not ready for a visual update",
        kMorphKey);
    return true;
}
}
