#include "PCH.h"

#include "SPSBridge.h"

namespace SPSArousalMorphs {
namespace {
bool HasCapability(std::uint32_t capabilities, SPS::API::Capability capability)
{
    return (capabilities & static_cast<std::uint32_t>(capability)) != 0;
}
}

bool SPSBridge::Connect()
{
    if (api_) {
        return true;
    }

    const auto module = ::GetModuleHandleW(SPS::API::kPluginDLL);
    if (!module) {
        logger::error("SPS is unavailable: SchlongPhysicsSwapper.dll is not loaded");
        return false;
    }

    const auto getAPI = reinterpret_cast<SPS::API::GetAPIV2Fn>(
        ::GetProcAddress(module, SPS::API::kGetAPIFunctionNameV2));
    if (!getAPI) {
        logger::error("SPS is incompatible: the read-only V2 API export was not found");
        return false;
    }

    const auto* candidate = getAPI();
    if (!candidate || candidate->version != SPS::API::kVersionV2 ||
        candidate->size < sizeof(SPS::API::InterfaceV2)) {
        logger::error("SPS is incompatible: expected API V{} and interface size at least {}",
            SPS::API::kVersionV2, sizeof(SPS::API::InterfaceV2));
        return false;
    }
    if (!candidate->GetCapabilities || !candidate->IsActorSupported ||
        !candidate->RequestPhysics || !candidate->ReleasePhysics ||
        !candidate->GetArousal || !candidate->GetErectThreshold) {
        logger::error("SPS is incompatible: required V2 arousal/threshold or physics request functions are missing");
        return false;
    }

    const auto capabilities = candidate->GetCapabilities();
    if (!HasCapability(capabilities, SPS::API::Capability::Player) ||
        !HasCapability(capabilities, SPS::API::Capability::LiveArousalReadback) ||
        !HasCapability(capabilities, SPS::API::Capability::ErectThresholdReadback) ||
        !candidate->IsActorSupported(SPS::API::kPlayerFormID)) {
        logger::error("SPS rejected integration: required player readback is unavailable");
        return false;
    }

    api_ = candidate;
    logger::info("SPS API detected: V{}, size {}, capabilities 0x{:X}; player supported",
        api_->version, api_->size, capabilities);
    ConnectV3(module);
    return true;
}

void SPSBridge::ConnectV3(HMODULE module)
{
    const auto getAPI = reinterpret_cast<SPS::API::GetAPIV3Fn>(
        ::GetProcAddress(module, SPS::API::kGetAPIFunctionNameV3));
    const auto* candidate = getAPI ? getAPI() : nullptr;
    if (!candidate || candidate->version != SPS::API::kVersionV3 ||
        candidate->size < sizeof(SPS::API::InterfaceV3) ||
        !candidate->GetCapabilities || !candidate->GetActorArousal || !candidate->IsActorInScene) {
        logger::warn("SPS API V3 is unavailable; NPC morphs are inactive until SPS is updated");
        return;
    }
    const auto capabilities = candidate->GetCapabilities();
    if (!HasCapability(capabilities, SPS::API::Capability::ActorArousalReadback) ||
        !HasCapability(capabilities, SPS::API::Capability::ActorSceneReadback)) {
        logger::warn("SPS API V3 lacks actor readback; NPC morphs are inactive");
        return;
    }
    apiV3_ = candidate;
    logger::info("SPS API V3 detected; NPC arousal and scene reads are available");
}

SPS::API::Result SPSBridge::ReadActorArousal(std::uint32_t formID, float& value) const
{
    return apiV3_ ? apiV3_->GetActorArousal(formID, &value) : SPS::API::Result::NotReady;
}

SPS::API::Result SPSBridge::ReadActorInScene(std::uint32_t formID, bool& value) const
{
    return apiV3_ ? apiV3_->IsActorInScene(formID, &value) : SPS::API::Result::NotReady;
}

SPS::API::Result SPSBridge::ReadArousal(float& value) const
{
    return api_ ? api_->GetArousal(SPS::API::kPlayerFormID, &value) : SPS::API::Result::NotReady;
}

SPS::API::Result SPSBridge::ReadErectThreshold(float& value) const
{
    return api_ ? api_->GetErectThreshold(&value) : SPS::API::Result::NotReady;
}

bool SPSBridge::IsSceneControllingPlayer() const
{
    if (!api_ || !api_->GetState) {
        return false;
    }
    // SPS fills the snapshot before returning NotReady during a physics handoff,
    // which is common as a scene starts, so that result is still usable.
    SPS::API::StateSnapshot snapshot{};
    const auto result = api_->GetState(SPS::API::kPlayerFormID, &snapshot);
    return (result == SPS::API::Result::Success || result == SPS::API::Result::NotReady) &&
        snapshot.source == SPS::API::ControlSource::Scene;
}

bool SPSBridge::HoldSMP()
{
    if (smpRequest_ != 0) {
        return true;
    }
    if (!api_ || holdAttempted_) {
        return false;
    }

    holdAttempted_ = true;
    SPS::API::PhysicsRequest request{};
    request.state = SPS::API::PhysicsState::SMP;
    request.requesterName = "SPS Arousal Morphs";
    smpRequest_ = api_->RequestPhysics(&request);
    if (smpRequest_ == 0) {
        logger::error("SPS rejected the temporary SMP hold request");
        return false;
    }

    logger::info("Requested SPS to hold SMP while configured morphs finish extending; handle {}",
        smpRequest_);
    return true;
}

void SPSBridge::ReleaseSMPHold()
{
    if (smpRequest_ == 0) {
        holdAttempted_ = false;
        return;
    }

    const auto handle = smpRequest_;
    smpRequest_ = 0;
    holdAttempted_ = false;
    const auto result = api_ ? api_->ReleasePhysics(handle) : SPS::API::Result::NotReady;
    if (result == SPS::API::Result::Success) {
        logger::info("Released SPS SMP hold {}; SPS resumes normal physics control", handle);
    } else if (result == SPS::API::Result::NotFound) {
        logger::info("SPS SMP hold {} was already cleared during the game transition", handle);
    } else {
        logger::warn("Could not release SPS SMP hold {}; result {}", handle,
            static_cast<std::uint32_t>(result));
    }
}
}
