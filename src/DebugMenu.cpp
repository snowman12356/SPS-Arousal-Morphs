#include "PCH.h"

#include "DebugMenu.h"

#include "LengthController.h"
#include "NpcController.h"

#include <SKSEMenuFramework.h>

namespace SPSArousalMorphs {
namespace {
DebugMenu* g_debugMenu{ nullptr };
constexpr std::size_t kMaximumMorphs = 32;
constexpr auto kSectionName = "SPS Arousal Morphs";
constexpr ImGuiMCP::ImVec4 kAmber{ 1.0F, 0.72F, 0.20F, 1.0F };
constexpr ImGuiMCP::ImVec4 kRed{ 1.0F, 0.45F, 0.35F, 1.0F };
constexpr ImGuiMCP::ImVec4 kBlue{ 0.45F, 0.80F, 1.0F, 1.0F };

bool IsBlank(const std::string& value)
{
    return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

template <std::size_t Slot>
void __stdcall RenderSlot()
{
    if (g_debugMenu) {
        g_debugMenu->DrawNpcSlot(Slot);
    }
}

template <std::size_t... Slots>
constexpr auto MakeSlotRenderers(std::index_sequence<Slots...>)
{
    return std::array<void(__stdcall*)(), sizeof...(Slots)>{ &RenderSlot<Slots>... };
}

constexpr auto kSlotRenderers = MakeSlotRenderers(std::make_index_sequence<kMaximumNpcs>{});

std::string PageTitle(const NpcSetting& npc, const std::vector<NpcSetting>& all)
{
    auto name = npc.name.empty() ? std::string("Unnamed") : npc.name;
    // '/' separates menu levels in framework paths.
    std::replace(name.begin(), name.end(), '/', '-');
    std::replace(name.begin(), name.end(), '\\', '-');
    const auto sameName = std::count_if(all.begin(), all.end(),
        [&npc](const NpcSetting& other) { return other.name == npc.name; });
    return sameName > 1 ? std::format("NPC: {} [{:X}]", name, npc.localFormID)
                        : std::format("NPC: {}", name);
}
}

void DebugMenu::Register()
{
    if (registered_) {
        return;
    }
    if (!SKSEMenuFramework::IsInstalled()) {
        logger::error("SKSE Menu Framework not found; addon settings are unavailable in game");
        return;
    }

    g_debugMenu = this;
    SKSEMenuFramework::SetSection(kSectionName);
    SKSEMenuFramework::AddSectionItem("Length Control", Render);
    SKSEMenuFramework::AddSectionItem("NPCs", RenderNpcList);
    registered_ = true;
    SyncNpcPages();
    logger::info("Registered SKSE Menu Framework page (framework version {:.2f})",
        SKSEMenuFramework::GetMenuFrameworkVersion());
}

void __stdcall DebugMenu::Render()
{
    if (g_debugMenu) {
        g_debugMenu->Draw();
    }
}

void __stdcall DebugMenu::RenderNpcList()
{
    if (g_debugMenu) {
        g_debugMenu->DrawNpcList();
    }
}

void DebugMenu::SavePlayerDraft()
{
    // NPC entries are edited on their own pages; never write a stale copy back.
    draft_.npcs = settings_.Snapshot().npcs;
    if (settings_.UpdateAndSave(draft_)) {
        draft_ = settings_.Snapshot();
        controller_.QueueSettingsChanged();
    }
}

void DebugMenu::Draw()
{
    if (!draftReady_) {
        draft_ = settings_.Snapshot();
        draftReady_ = true;
    }

    // The page is taller than most windows once a few morphs are added, so scroll
    // it inside the parent region instead of letting it spill off screen.
    const auto available = ImGuiMCP::GetContentRegionAvail();
    ImGuiMCP::BeginChild("##sps_page", ImGuiMCP::ImVec2(0.0F, available.y), 0);

    ImGuiMCP::TextColored(kBlue, "SPS AROUSAL MORPHS");
    ImGuiMCP::Separator();

    auto saved = settings_.Snapshot();
    bool enabled = saved.enabled;
    if (ImGuiMCP::Checkbox("Enable addon", &enabled)) {
        draft_.enabled = enabled;
        SavePlayerDraft();
    }
    bool smpHold = saved.smpHold;
    if (ImGuiMCP::Checkbox("Temporary SMP hold while extending", &smpHold)) {
        draft_.smpHold = smpHold;
        SavePlayerDraft();
    }

    ImGuiMCP::TextWrapped(
        "The addon drives the BodyMorphs listed below. With the SMP hold on, it asks SPS to keep SMP active "
        "until extension finishes. With it off, SPS alone decides physics.");
    ImGuiMCP::Separator();

    ImGuiMCP::SliderFloat("Start Growing At", &draft_.startGrowingArousal,
        0.0F, 99.0F, "%.0f arousal");
    ImGuiMCP::SliderFloat("Fully Extended At", &draft_.fullyExtendedArousal,
        1.0F, 100.0F, "%.0f arousal");
    ImGuiMCP::SliderFloat("Extension Time", &draft_.extensionSeconds, 0.1F, 30.0F, "%.1f seconds");
    ImGuiMCP::SliderFloat("Retraction Time", &draft_.retractionSeconds, 0.1F, 30.0F, "%.1f seconds");

    ImGuiMCP::Separator();
    DrawMorphEditor(draft_.morphs, newMorphName_);

    ImGuiMCP::Separator();
    if (ImGuiMCP::Button("Apply settings")) {
        const auto current = settings_.Snapshot();
        draft_.enabled = current.enabled;
        draft_.smpHold = current.smpHold;
        SavePlayerDraft();
    }
    ImGuiMCP::SameLine();
    if (ImGuiMCP::Button("Reload INI")) {
        settings_.Load();
        draft_ = settings_.Snapshot();
        controller_.QueueSettingsChanged();
        QueueSyncNpcPages();
    }
    if (ImGuiMCP::Button("Emergency: disable and clear addon morphs")) {
        auto emergency = settings_.Snapshot();
        emergency.enabled = false;
        if (settings_.UpdateAndSave(std::move(emergency))) {
            draft_ = settings_.Snapshot();
            controller_.QueueSettingsChanged();
        }
    }

    ImGuiMCP::Separator();
    const auto status = controller_.Status();
    ImGuiMCP::TextWrapped("State: %s", LengthController::StateName(status.state));
    if (status.dataReady) {
        ImGuiMCP::TextWrapped("SPS arousal: %.1f / 100", status.arousal);
        ImGuiMCP::TextWrapped("SPS erect threshold: %.1f / 100", status.erectThreshold);
        if (draft_.fullyExtendedArousal > status.erectThreshold) {
            ImGuiMCP::TextColored(kAmber, "Warning: Fully Extended At is above the SPS erect threshold.");
        }
    }
    ImGuiMCP::TextWrapped("Temporary SMP hold: %s",
        !saved.smpHold ? "Off" :
        status.holdingSMP ? (status.settlingBeforeRelease ? "Active - settling" : "Active") : "Inactive");
    ImGuiMCP::TextWrapped("Current extension: %.0f%%", status.currentExtension * 100.0F);
    ImGuiMCP::TextWrapped("Target extension: %.0f%%", status.targetExtension * 100.0F);

    ImGuiMCP::EndChild();
}

void DebugMenu::DrawNpcList()
{
    const auto available = ImGuiMCP::GetContentRegionAvail();
    ImGuiMCP::BeginChild("##sps_npc_list", ImGuiMCP::ImVec2(0.0F, available.y), 0);

    ImGuiMCP::TextColored(kBlue, "NPCS");
    ImGuiMCP::Separator();
    ImGuiMCP::TextWrapped(
        "Look at an NPC, open this menu and press Add. Each NPC gets its own page in this section, "
        "starting from the player's current settings.");

    const auto settings = settings_.Snapshot();
    if (!settings.enabled) {
        ImGuiMCP::TextColored(kAmber, "The addon is off on the Length Control page, so NPCs are not driven.");
    }

    if (ImGuiMCP::Button("Add NPC in crosshair")) {
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([this]() { AddCrosshairNpc(); });
        }
    }
    {
        std::scoped_lock guard(npcMessageLock_);
        if (!npcMessage_.empty()) {
            ImGuiMCP::TextWrapped("%s", npcMessage_.c_str());
        }
    }

    ImGuiMCP::Separator();
    ImGuiMCP::TextWrapped("Added NPCs: %zu of %zu", settings.npcs.size(), kMaximumNpcs);
    for (const auto& npc : settings.npcs) {
        const auto status = npcs_.Status(npc.Key());
        ImGuiMCP::TextWrapped("%s: %s", npc.name.c_str(), status ? status->state : "Waiting for a loaded game");
    }

    ImGuiMCP::EndChild();
}

void DebugMenu::DrawNpcSlot(std::size_t slot)
{
    if (slot >= pages_.size()) {
        return;
    }
    std::scoped_lock guard(pagesLock_);
    if (pages_[slot].used) {
        DrawNpcPage(pages_[slot]);
    }
}

void DebugMenu::DrawNpcPage(NpcPage& page)
{
    if (!page.draftReady) {
        const auto settings = settings_.Snapshot();
        const auto found = std::find_if(settings.npcs.begin(), settings.npcs.end(),
            [&page](const NpcSetting& npc) { return npc.Key() == page.key; });
        if (found == settings.npcs.end()) {
            return;
        }
        page.draft = *found;
        page.draftReady = true;
    }

    auto& draft = page.draft;
    const auto available = ImGuiMCP::GetContentRegionAvail();
    ImGuiMCP::BeginChild("##sps_npc_page", ImGuiMCP::ImVec2(0.0F, available.y), 0);

    ImGuiMCP::TextColored(kBlue, "%s", draft.name.c_str());
    ImGuiMCP::TextDisabled("%s  %08X", draft.plugin.c_str(), draft.localFormID);
    ImGuiMCP::Separator();

    ImGuiMCP::Checkbox("Enabled for this NPC", &draft.enabled);
    ImGuiMCP::SliderFloat("Start Growing At", &draft.startGrowingArousal, 0.0F, 99.0F, "%.0f arousal");
    ImGuiMCP::SliderFloat("Fully Extended At", &draft.fullyExtendedArousal, 1.0F, 100.0F, "%.0f arousal");
    ImGuiMCP::SliderFloat("Extension Time", &draft.extensionSeconds, 0.1F, 30.0F, "%.1f seconds");
    ImGuiMCP::SliderFloat("Retraction Time", &draft.retractionSeconds, 0.1F, 30.0F, "%.1f seconds");

    ImGuiMCP::Separator();
    DrawMorphEditor(draft.morphs, page.newMorphName);

    ImGuiMCP::Separator();
    if (ImGuiMCP::Button("Apply settings")) {
        auto settings = settings_.Snapshot();
        const auto found = std::find_if(settings.npcs.begin(), settings.npcs.end(),
            [&page](const NpcSetting& npc) { return npc.Key() == page.key; });
        if (found != settings.npcs.end()) {
            *found = draft;
            if (settings_.UpdateAndSave(std::move(settings))) {
                page.draftReady = false;
                controller_.QueueSettingsChanged();
            }
        }
    }
    ImGuiMCP::SameLine();
    if (ImGuiMCP::Button("Remove this NPC")) {
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([this, key = page.key]() { RemoveNpc(key); });
        }
    }

    ImGuiMCP::Separator();
    if (const auto status = npcs_.Status(page.key)) {
        ImGuiMCP::TextWrapped("State: %s", status->state);
        if (status->hasArousal) {
            ImGuiMCP::TextWrapped("Arousal: %.1f / 100", status->arousal);
        }
        ImGuiMCP::TextWrapped("In scene: %s", status->inScene ? "Yes" : "No");
        ImGuiMCP::TextWrapped("Current extension: %.0f%%", status->currentExtension * 100.0F);
        ImGuiMCP::TextWrapped("Target extension: %.0f%%", status->targetExtension * 100.0F);
    } else {
        ImGuiMCP::TextWrapped("State: Waiting for a loaded game");
    }

    ImGuiMCP::EndChild();
}

void DebugMenu::DrawMorphEditor(std::vector<MorphSetting>& morphs, std::array<char, 128>& newName)
{
    ImGuiMCP::TextWrapped("Configured morphs: %zu of %zu", morphs.size(), kMaximumMorphs);

    if (morphs.empty()) {
        ImGuiMCP::TextColored(kRed, "No valid morphs are configured.");
    }

    std::optional<std::size_t> removeIndex;
    std::string duplicateOf;

    for (std::size_t index = 0; index < morphs.size(); ++index) {
        auto& morph = morphs[index];
        ImGuiMCP::PushID(static_cast<int>(index));

        if (index > 0) {
            ImGuiMCP::Dummy(ImGuiMCP::ImVec2(0.0F, 10.0F));
        }

        const auto title = morph.name.empty() ? std::string("Unnamed BodySlide") : morph.name;
        ImGuiMCP::SeparatorText(title.c_str());
        ImGuiMCP::Checkbox("Enabled", &morph.enabled);
        ImGuiMCP::SameLine();
        if (ImGuiMCP::SmallButton("Remove")) {
            removeIndex = index;
        }

        std::array<char, 128> nameBuffer{};
        const auto copyLength = std::min(morph.name.size(), nameBuffer.size() - 1);
        std::memcpy(nameBuffer.data(), morph.name.data(), copyLength);
        ImGuiMCP::SetNextItemWidth(-1.0F);
        if (ImGuiMCP::InputTextWithHint("##bodyslide", "BodySlide slider name",
                nameBuffer.data(), nameBuffer.size())) {
            morph.name = nameBuffer.data();
        }

        // Short names go on the slider itself. Long names stay on the heading
        // above, because a long ImGui label shrinks the slider until it is unusable.
        const auto retractedLabel = morph.name.size() > 22
            ? std::format("Retracted##retracted_{}", index)
            : std::format("{} retracted##retracted_{}", title, index);
        const auto extendedLabel = morph.name.size() > 22
            ? std::format("Extended##extended_{}", index)
            : std::format("{} extended##extended_{}", title, index);
        if (!morph.enabled) {
            ImGuiMCP::BeginDisabled();
        }
        ImGuiMCP::SliderFloat(retractedLabel.c_str(), &morph.retractedValue, -3.0F, 3.0F, "%.2f");
        ImGuiMCP::SliderFloat(extendedLabel.c_str(), &morph.extendedValue, -3.0F, 3.0F, "%.2f");
        if (!morph.enabled) {
            ImGuiMCP::EndDisabled();
        }

        // Warn about a duplicate before Apply, since the INI writer de-duplicates
        // silently and the user would otherwise see an entry vanish.
        for (std::size_t other = 0; other < morphs.size(); ++other) {
            if (other != index && morphs[other].name == morph.name && !IsBlank(morph.name)) {
                duplicateOf = morph.name;
                break;
            }
        }
        if (IsBlank(morph.name)) {
            ImGuiMCP::TextColored(kRed, "Name is empty; this entry is ignored when applied.");
        }

        ImGuiMCP::PopID();
    }

    if (removeIndex) {
        morphs.erase(morphs.begin() + static_cast<std::ptrdiff_t>(*removeIndex));
    }

    if (!duplicateOf.empty()) {
        ImGuiMCP::TextColored(kAmber, "Duplicate name '%s'; only the first entry is kept.", duplicateOf.c_str());
    }

    if (morphs.size() < kMaximumMorphs) {
        ImGuiMCP::TextWrapped("Type a BodySlide slider name, then press Enter or click Add:");
        ImGuiMCP::PushItemWidth(-120.0F);
        const auto submitted = ImGuiMCP::InputTextWithHint("##new_morph", "e.g. Penis_Length",
            newName.data(), newName.size(),
            ImGuiMCP::ImGuiInputTextFlags_EnterReturnsTrue);
        ImGuiMCP::PopItemWidth();
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("Add") || submitted) {
            const std::string candidate{ newName.data() };
            if (!IsBlank(candidate)) {
                MorphSetting added;
                added.name = candidate;
                morphs.push_back(std::move(added));
                newName.fill('\0');
            }
        }
    } else {
        ImGuiMCP::TextColored(kAmber, "Morph limit reached; remove an entry to add another.");
    }

    ImGuiMCP::TextDisabled("Changes apply when you press Apply settings; the INI is rewritten.");
}

void DebugMenu::SetNpcMessage(std::string message)
{
    std::scoped_lock guard(npcMessageLock_);
    npcMessage_ = std::move(message);
}

void DebugMenu::AddCrosshairNpc()
{
    auto* pick = RE::CrosshairPickData::GetSingleton();
    const auto target = pick ? pick->GetActiveTarget().get() : RE::NiPointer<RE::TESObjectREFR>{};
    auto* actor = target ? target->As<RE::Actor>() : nullptr;
    if (!actor || actor->IsPlayerRef()) {
        SetNpcMessage("No NPC in the crosshair.");
        return;
    }
    const auto* file = actor->GetFile(0);
    if (actor->IsDynamicForm() || !file) {
        // A game-created reference has a different ID in every save, so an
        // INI entry shared by all saves could not find it again.
        SetNpcMessage(std::format("{} was created at runtime and can't be saved to the shared list.", actor->GetName()));
        return;
    }

    auto settings = settings_.Snapshot();
    if (settings.npcs.size() >= kMaximumNpcs) {
        SetNpcMessage("NPC limit reached; remove one first.");
        return;
    }

    NpcSetting added;
    added.plugin = file->GetFilename();
    added.localFormID = actor->GetLocalFormID();
    added.name = actor->GetName();
    added.startGrowingArousal = settings.startGrowingArousal;
    added.fullyExtendedArousal = settings.fullyExtendedArousal;
    added.extensionSeconds = settings.extensionSeconds;
    added.retractionSeconds = settings.retractionSeconds;
    added.morphs = settings.morphs;

    const auto key = added.Key();
    if (std::any_of(settings.npcs.begin(), settings.npcs.end(),
            [&key](const NpcSetting& npc) { return npc.Key() == key; })) {
        SetNpcMessage(std::format("{} is already added.", added.name));
        return;
    }

    const auto name = added.name;
    settings.npcs.push_back(std::move(added));
    if (!settings_.UpdateAndSave(std::move(settings))) {
        SetNpcMessage("Could not write the INI.");
        return;
    }
    controller_.OnSettingsChanged();
    SyncNpcPages();
    SetNpcMessage(std::format("Added {}.", name));
    logger::info("Added NPC {} ({})", name, key);
}

void DebugMenu::RemoveNpc(const std::string& key)
{
    auto settings = settings_.Snapshot();
    const auto removed = std::erase_if(settings.npcs,
        [&key](const NpcSetting& npc) { return npc.Key() == key; });
    if (removed == 0 || !settings_.UpdateAndSave(std::move(settings))) {
        return;
    }
    controller_.OnSettingsChanged();
    SyncNpcPages();
    logger::info("Removed NPC {}", key);
}

void DebugMenu::QueueSyncNpcPages()
{
    if (auto* tasks = SKSE::GetTaskInterface()) {
        tasks->AddTask([this]() { SyncNpcPages(); });
    }
}

// Never call from a render callback: AddSectionItem changes the menu tree
// immediately, and the framework is walking that tree while it renders.
void DebugMenu::SyncNpcPages()
{
    if (!registered_) {
        return;
    }
    const auto settings = settings_.Snapshot();
    std::scoped_lock guard(pagesLock_);

    for (auto& page : pages_) {
        if (!page.used) {
            continue;
        }
        const bool stillConfigured = std::any_of(settings.npcs.begin(), settings.npcs.end(),
            [&page](const NpcSetting& npc) { return npc.Key() == page.key; });
        if (!stillConfigured) {
            SKSEMenuFramework::DeleteSection(std::format("{}/{}", kSectionName, page.title));
            page = NpcPage{};
        } else {
            page.draftReady = false;
        }
    }

    for (const auto& npc : settings.npcs) {
        const auto key = npc.Key();
        const auto title = PageTitle(npc, settings.npcs);
        auto page = std::find_if(pages_.begin(), pages_.end(),
            [&key](const NpcPage& candidate) { return candidate.used && candidate.key == key; });
        if (page != pages_.end()) {
            if (page->title == title) {
                continue;
            }
            SKSEMenuFramework::DeleteSection(std::format("{}/{}", kSectionName, page->title));
        } else {
            page = std::find_if(pages_.begin(), pages_.end(),
                [](const NpcPage& candidate) { return !candidate.used; });
            if (page == pages_.end()) {
                continue;
            }
            page->used = true;
            page->key = key;
        }
        page->title = title;
        const auto slot = static_cast<std::size_t>(std::distance(pages_.begin(), page));
        SKSEMenuFramework::AddSectionItem(title, kSlotRenderers[slot]);
    }
}
}
