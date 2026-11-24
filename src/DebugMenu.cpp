#include "PCH.h"

#include "DebugMenu.h"

#include "LengthController.h"
#include "NpcController.h"

#include <SKSEMenuFramework.h>

namespace SPSArousalMorphs {
namespace {
DebugMenu* g_debugMenu{ nullptr };
constexpr auto kSectionName = "SPS Arousal Morphs";
constexpr ImGuiMCP::ImVec4 kAmber{ 1.0F, 0.72F, 0.20F, 1.0F };
constexpr ImGuiMCP::ImVec4 kRed{ 1.0F, 0.45F, 0.35F, 1.0F };
constexpr ImGuiMCP::ImVec4 kBlue{ 0.45F, 0.80F, 1.0F, 1.0F };

bool IsBlank(const std::string& value)
{
    return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

constexpr ImGuiMCP::ImVec4 kGold{ 0.79F, 0.64F, 0.36F, 1.0F };
constexpr ImGuiMCP::ImVec4 kGreen{ 0.45F, 0.85F, 0.55F, 1.0F };
constexpr ImGuiMCP::ImVec4 kMuted{ 0.62F, 0.65F, 0.70F, 1.0F };

void DrawTitle(const char* title, const char* subtitle)
{
    ImGuiMCP::TextColored(kGold, "%s", title);
    ImGuiMCP::TextDisabled("%s", subtitle);
    ImGuiMCP::Spacing();
}

void DrawHint(const char* text)
{
    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, kMuted);
    ImGuiMCP::TextWrapped("%s", text);
    ImGuiMCP::PopStyleColor();
}

void DrawBar(float fraction, const char* overlay)
{
    ImGuiMCP::ProgressBar(std::clamp(fraction, 0.0F, 1.0F), ImGuiMCP::ImVec2(-1.0F, 0.0F), overlay);
}

bool DrawDangerButton(const char* label)
{
    constexpr ImGuiMCP::ImVec4 base{ 0.55F, 0.18F, 0.15F, 1.0F };
    constexpr ImGuiMCP::ImVec4 hover{ 0.70F, 0.25F, 0.20F, 1.0F };
    constexpr ImGuiMCP::ImVec4 active{ 0.80F, 0.30F, 0.24F, 1.0F };
    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Button, base);
    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered, hover);
    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive, active);
    const bool pressed = ImGuiMCP::Button(label);
    ImGuiMCP::PopStyleColor(3);
    return pressed;
}

void DrawTip(const char* text)
{
    if (ImGuiMCP::IsItemHovered()) {
        ImGuiMCP::SetTooltip("%s", text);
    }
}

ImGuiMCP::ImVec4 StateColor(LengthState state)
{
    switch (state) {
    case LengthState::GrowthRange:
    case LengthState::FullLength:
    case LengthState::SceneExtended:
    case LengthState::SceneFollowingArousal:
        return kGreen;
    case LengthState::Retracted:
        return kBlue;
    case LengthState::Disabled:
        return kMuted;
    default:
        return kAmber;
    }
}

// The same four sliders on the player page and on every NPC page.
void DrawGrowthSliders(float& start, float& full, float& extend, float& retract)
{
    ImGuiMCP::SeparatorText("When it grows");
    DrawHint("Below the first point the sliders stay soft. Between the two points they grow with arousal. "
             "Above the second they are fully extended.");
    ImGuiMCP::SliderFloat("Starts growing at", &start, 0.0F, 99.0F, "%.0f arousal");
    ImGuiMCP::SliderFloat("Fully extended at", &full, 1.0F, 100.0F, "%.0f arousal");
    if (start >= full) {
        ImGuiMCP::TextColored(kAmber, "Fully extended must be higher than where growth starts.");
    }
    ImGuiMCP::Spacing();
    ImGuiMCP::SeparatorText("How fast");
    ImGuiMCP::SliderFloat("Time to extend", &extend, 0.1F, 30.0F, "%.1f seconds");
    ImGuiMCP::SliderFloat("Time to retract", &retract, 0.1F, 30.0F, "%.1f seconds");
}

enum class ApplyChoice { None, Apply, Discard };

ApplyChoice DrawApplyBar(bool dirty)
{
    ImGuiMCP::Spacing();
    ImGuiMCP::Separator();
    if (dirty) {
        ImGuiMCP::TextColored(kAmber, "You have unsaved changes.");
    } else {
        ImGuiMCP::TextDisabled("All changes saved.");
    }
    auto choice = ApplyChoice::None;
    if (!dirty) {
        ImGuiMCP::BeginDisabled();
    }
    if (ImGuiMCP::Button("Apply changes")) {
        choice = ApplyChoice::Apply;
    }
    ImGuiMCP::SameLine();
    if (ImGuiMCP::Button("Discard changes")) {
        choice = ApplyChoice::Discard;
    }
    if (!dirty) {
        ImGuiMCP::EndDisabled();
    }
    return choice;
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
    SKSEMenuFramework::AddSectionItem("Overview", Render);
    SKSEMenuFramework::AddSectionItem("You", RenderPlayer);
    SKSEMenuFramework::AddSectionItem("NPCs", RenderNpcList);
    registered_ = true;
    SyncNpcPages();
    logger::info("Registered SKSE Menu Framework page (framework version {:.2f})",
        SKSEMenuFramework::GetMenuFrameworkVersion());
}

void __stdcall DebugMenu::Render()
{
    if (g_debugMenu) {
        g_debugMenu->DrawOverview();
    }
}

void __stdcall DebugMenu::RenderPlayer()
{
    if (g_debugMenu) {
        g_debugMenu->DrawPlayer();
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

void DebugMenu::EnsureDraft()
{
    if (!draftReady_) {
        draft_ = settings_.Snapshot();
        draftReady_ = true;
    }
}

bool DebugMenu::PlayerDraftDirty(const Settings& saved) const
{
    return draft_.startGrowingArousal != saved.startGrowingArousal ||
        draft_.fullyExtendedArousal != saved.fullyExtendedArousal ||
        draft_.extensionSeconds != saved.extensionSeconds ||
        draft_.retractionSeconds != saved.retractionSeconds ||
        draft_.morphs != saved.morphs;
}

// Page 1: what is happening now, and the settings that apply to everyone.
// Everything on this page saves at once.
void DebugMenu::DrawOverview()
{
    EnsureDraft();
    const auto available = ImGuiMCP::GetContentRegionAvail();
    ImGuiMCP::BeginChild("##sps_overview", ImGuiMCP::ImVec2(0.0F, available.y), 0);

    DrawTitle("SPS AROUSAL MORPHS", "Your arousal, shown on your body. Changes on this page save at once.");

    const auto saved = settings_.Snapshot();
    bool enabled = saved.enabled;
    if (ImGuiMCP::Checkbox("Mod is on", &enabled)) {
        auto updated = settings_.Snapshot();
        updated.enabled = enabled;
        if (settings_.UpdateAndSave(std::move(updated))) {
            draft_.enabled = enabled;
            controller_.QueueSettingsChanged();
        }
    }

    const auto status = controller_.Status();
    ImGuiMCP::SeparatorText("Right now");
    ImGuiMCP::TextColored(StateColor(status.state), "%s", LengthController::StateName(status.state));
    if (controller_.SpsApiFound()) {
        ImGuiMCP::TextColored(kGreen, "SPS API: found");
    } else {
        ImGuiMCP::TextColored(kAmber, "SPS API: not found");
    }
    if (status.dataReady) {
        char overlay[64]{};
        std::snprintf(overlay, sizeof(overlay), "Arousal  %.0f / 100", status.arousal);
        DrawBar(status.arousal / 100.0F, overlay);
        std::snprintf(overlay, sizeof(overlay), "Length  now %.0f%%   target %.0f%%",
            status.currentExtension * 100.0F, status.targetExtension * 100.0F);
        DrawBar(status.currentExtension, overlay);
        if (draft_.fullyExtendedArousal > status.erectThreshold) {
            ImGuiMCP::TextColored(kAmber,
                "Your 'fully extended' point is above SPS's erect point (%.0f).", status.erectThreshold);
        }
        if (saved.smpHold) {
            ImGuiMCP::TextDisabled("Physics hold: %s",
                status.holdingSMP ? (status.settlingBeforeRelease ? "active, settling" : "active") : "not needed");
        }
    } else {
        DrawHint("Waiting for arousal from SPS.");
    }

    ImGuiMCP::Spacing();
    ImGuiMCP::SeparatorText("During a scene (you and NPCs)");
    int sceneMode = saved.sceneMode;
    bool sceneChanged = ImGuiMCP::RadioButton("Follow role", &sceneMode, kSceneFollowRole);
    DrawHint("    Fully extended only while penetrating. Otherwise it keeps the length from before the scene.");
    sceneChanged |= ImGuiMCP::RadioButton("Always fully extended", &sceneMode, kSceneAlwaysExtended);
    DrawHint("    Fully extended for the whole scene.");
    sceneChanged |= ImGuiMCP::RadioButton("Keep the length from before the scene", &sceneMode, kSceneKeepLength);
    DrawHint("    The length never changes during a scene.");
    if (sceneChanged && sceneMode != saved.sceneMode) {
        auto updated = settings_.Snapshot();
        updated.sceneMode = sceneMode;
        if (settings_.UpdateAndSave(std::move(updated))) {
            draft_.sceneMode = sceneMode;
        }
    }

    ImGuiMCP::Spacing();
    ImGuiMCP::SeparatorText("Physics");
    bool smpHold = saved.smpHold;
    if (ImGuiMCP::Checkbox("Hold SMP briefly while growing", &smpHold)) {
        auto updated = settings_.Snapshot();
        updated.smpHold = smpHold;
        if (settings_.UpdateAndSave(std::move(updated))) {
            draft_.smpHold = smpHold;
            controller_.QueueSettingsChanged();
        }
    }
    DrawHint("Keeps physics steady while the sliders grow, then hands control back to SPS. "
             "Turn it off to let SPS alone decide. Applies to you only.");

    ImGuiMCP::Spacing();
    ImGuiMCP::SeparatorText("Troubleshooting");
    if (ImGuiMCP::Button("Reload settings file")) {
        settings_.Load();
        draft_ = settings_.Snapshot();
        controller_.QueueSettingsChanged();
        QueueSyncNpcPages();
    }
    DrawTip("Reads SPSArousalMorphs.ini again, for edits made outside the game.");
    ImGuiMCP::SameLine();
    if (DrawDangerButton("Turn off and clear morphs")) {
        auto emergency = settings_.Snapshot();
        emergency.enabled = false;
        if (settings_.UpdateAndSave(std::move(emergency))) {
            draft_ = settings_.Snapshot();
            controller_.QueueSettingsChanged();
        }
    }
    DrawTip("Turns the mod off and removes every morph it added. Other mods' morphs are not touched.");

    ImGuiMCP::EndChild();
}

// Page 2: how your own body responds. Edits wait for Apply changes.
void DebugMenu::DrawPlayer()
{
    EnsureDraft();
    const auto available = ImGuiMCP::GetContentRegionAvail();
    ImGuiMCP::BeginChild("##sps_player", ImGuiMCP::ImVec2(0.0F, available.y), 0);

    DrawTitle("YOU", "How your body responds to arousal. Press Apply changes to save.");

    DrawGrowthSliders(draft_.startGrowingArousal, draft_.fullyExtendedArousal,
        draft_.extensionSeconds, draft_.retractionSeconds);
    ImGuiMCP::Spacing();
    DrawMorphEditor(draft_.morphs, newMorphName_);

    const auto saved = settings_.Snapshot();
    switch (DrawApplyBar(PlayerDraftDirty(saved))) {
    case ApplyChoice::Apply: {
        const auto current = settings_.Snapshot();
        draft_.enabled = current.enabled;
        draft_.smpHold = current.smpHold;
        draft_.sceneMode = current.sceneMode;
        SavePlayerDraft();
        break;
    }
    case ApplyChoice::Discard: {
        const auto current = settings_.Snapshot();
        draft_.startGrowingArousal = current.startGrowingArousal;
        draft_.fullyExtendedArousal = current.fullyExtendedArousal;
        draft_.extensionSeconds = current.extensionSeconds;
        draft_.retractionSeconds = current.retractionSeconds;
        draft_.morphs = current.morphs;
        break;
    }
    default:
        break;
    }

    ImGuiMCP::EndChild();
}

// Page 3: add NPCs and see them all at a glance.
void DebugMenu::DrawNpcList()
{
    const auto available = ImGuiMCP::GetContentRegionAvail();
    ImGuiMCP::BeginChild("##sps_npc_list", ImGuiMCP::ImVec2(0.0F, available.y), 0);

    DrawTitle("NPCS", "Give any NPC the same arousal-driven body change.");

    const auto settings = settings_.Snapshot();
    if (!settings.enabled) {
        ImGuiMCP::TextColored(kAmber, "The mod is off (Overview page), so NPCs are not driven.");
    }

    ImGuiMCP::SeparatorText("Add an NPC");
    DrawHint("Look at the NPC in the game, open this menu and press the button. Each NPC gets its own page "
             "in the list on the left. Timing starts from your settings. The slider list starts empty.");
    if (ImGuiMCP::Button("Add the NPC I am looking at")) {
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([this]() { AddCrosshairNpc(); });
        }
    }
    {
        std::scoped_lock guard(npcMessageLock_);
        if (!npcMessage_.empty()) {
            ImGuiMCP::TextColored(kBlue, "%s", npcMessage_.c_str());
        }
    }

    ImGuiMCP::Spacing();
    ImGuiMCP::SeparatorText("Your NPCs");
    ImGuiMCP::TextDisabled("%zu of %zu", settings.npcs.size(), kMaximumNpcs);
    if (settings.npcs.empty()) {
        DrawHint("No NPCs yet.");
    }
    for (const auto& npc : settings.npcs) {
        const auto status = npcs_.Status(npc.Key());
        ImGuiMCP::TextColored(kGold, "%s", npc.name.c_str());
        ImGuiMCP::SameLine();
        ImGuiMCP::TextDisabled("%s", status ? status->state : "Waiting for a loaded game");
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

// One page per NPC.
void DebugMenu::DrawNpcPage(NpcPage& page)
{
    const auto settings = settings_.Snapshot();
    const auto found = std::find_if(settings.npcs.begin(), settings.npcs.end(),
        [&page](const NpcSetting& npc) { return npc.Key() == page.key; });
    if (found == settings.npcs.end()) {
        return;
    }
    if (!page.draftReady) {
        page.draft = *found;
        page.draftReady = true;
    }

    auto& draft = page.draft;
    const auto available = ImGuiMCP::GetContentRegionAvail();
    ImGuiMCP::BeginChild("##sps_npc_page", ImGuiMCP::ImVec2(0.0F, available.y), 0);

    ImGuiMCP::TextColored(kGold, "%s", draft.name.c_str());
    if (draft.runtime) {
        ImGuiMCP::TextDisabled("Created during play. Kept in this save only.  %08X", draft.localFormID);
    } else {
        ImGuiMCP::TextDisabled("%s  %08X", draft.plugin.c_str(), draft.localFormID);
    }
    ImGuiMCP::Spacing();

    ImGuiMCP::Checkbox("Enabled for this NPC", &draft.enabled);

    ImGuiMCP::SeparatorText("Right now");
    if (const auto status = npcs_.Status(page.key)) {
        ImGuiMCP::TextWrapped("%s", status->state);
        if (status->hasArousal) {
            char overlay[64]{};
            std::snprintf(overlay, sizeof(overlay), "Arousal  %.0f / 100", status->arousal);
            DrawBar(status->arousal / 100.0F, overlay);
        }
        char overlay[64]{};
        std::snprintf(overlay, sizeof(overlay), "Length  now %.0f%%   target %.0f%%",
            status->currentExtension * 100.0F, status->targetExtension * 100.0F);
        DrawBar(status->currentExtension, overlay);
        if (status->inScene) {
            ImGuiMCP::TextColored(kBlue, "In a scene");
        }
    } else {
        DrawHint("Waiting for a loaded game.");
    }

    ImGuiMCP::Spacing();
    DrawGrowthSliders(draft.startGrowingArousal, draft.fullyExtendedArousal,
        draft.extensionSeconds, draft.retractionSeconds);
    ImGuiMCP::Spacing();
    DrawMorphEditor(draft.morphs, page.newMorphName);

    switch (DrawApplyBar(!(draft == *found))) {
    case ApplyChoice::Apply: {
        auto updated = settings_.Snapshot();
        const auto target = std::find_if(updated.npcs.begin(), updated.npcs.end(),
            [&page](const NpcSetting& npc) { return npc.Key() == page.key; });
        if (target != updated.npcs.end()) {
            *target = draft;
            if (settings_.UpdateAndSave(std::move(updated))) {
                page.draftReady = false;
                controller_.QueueSettingsChanged();
            }
        }
        break;
    }
    case ApplyChoice::Discard:
        page.draftReady = false;
        break;
    default:
        break;
    }

    ImGuiMCP::Spacing();
    ImGuiMCP::SeparatorText("Remove");
    if (DrawDangerButton("Remove this NPC")) {
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([this, key = page.key]() { RemoveNpc(key); });
        }
    }
    DrawTip("Removes this NPC and clears the morphs the mod added to it.");

    ImGuiMCP::EndChild();
}

void DebugMenu::DrawMorphEditor(std::vector<MorphSetting>& morphs, std::array<char, 128>& newName)
{
    ImGuiMCP::SeparatorText("Sliders to move");
    DrawHint("Each BodySlide slider listed here moves between its Retracted and Extended value. "
             "The slider must be built into the body mesh, or nothing will happen.");

    if (morphs.empty()) {
        ImGuiMCP::TextColored(kRed, "No sliders yet. Add one below.");
    }

    std::optional<std::size_t> removeIndex;
    std::string duplicateOf;

    for (std::size_t index = 0; index < morphs.size(); ++index) {
        auto& morph = morphs[index];
        ImGuiMCP::PushID(static_cast<int>(index));
        ImGuiMCP::Spacing();

        const auto title = morph.name.empty() ? std::string("Unnamed slider") : morph.name;
        ImGuiMCP::TextColored(kBlue, "%s", title.c_str());
        ImGuiMCP::SameLine();
        ImGuiMCP::Checkbox("On", &morph.enabled);
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

        if (!morph.enabled) {
            ImGuiMCP::BeginDisabled();
        }
        ImGuiMCP::SliderFloat("Retracted", &morph.retractedValue, -3.0F, 3.0F, "%.2f");
        ImGuiMCP::SliderFloat("Extended", &morph.extendedValue, -3.0F, 3.0F, "%.2f");
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

    ImGuiMCP::Spacing();
    if (morphs.size() < kMaximumMorphs) {
        ImGuiMCP::PushItemWidth(-120.0F);
        const auto submitted = ImGuiMCP::InputTextWithHint("##new_morph", "Type a slider name, e.g. Penis_Length",
            newName.data(), newName.size(),
            ImGuiMCP::ImGuiInputTextFlags_EnterReturnsTrue);
        ImGuiMCP::PopItemWidth();
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("Add slider") || submitted) {
            const std::string candidate{ newName.data() };
            if (!IsBlank(candidate)) {
                MorphSetting added;
                added.name = candidate;
                morphs.push_back(std::move(added));
                newName.fill('\0');
            }
        }
    } else {
        ImGuiMCP::TextColored(kAmber, "Slider limit reached; remove one to add another.");
    }
    ImGuiMCP::TextDisabled("%zu of %zu sliders", morphs.size(), kMaximumMorphs);
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
    // A game-created reference has a different ID in every save, so it cannot
    // go in the shared INI; it is kept in this save's cosave instead.
    const bool runtime = actor->IsDynamicForm();
    const auto* file = runtime ? nullptr : actor->GetFile(0);
    if (!runtime && !file) {
        SetNpcMessage(std::format("{} has no source plugin and can't be added.", actor->GetName()));
        return;
    }

    auto settings = settings_.Snapshot();
    if (settings.npcs.size() >= kMaximumNpcs) {
        SetNpcMessage("NPC limit reached; remove one first.");
        return;
    }

    NpcSetting added;
    if (runtime) {
        added.runtime = true;
        added.localFormID = actor->GetFormID();
    } else {
        added.plugin = file->GetFilename();
        added.localFormID = actor->GetLocalFormID();
    }
    added.name = actor->GetName();
    added.startGrowingArousal = settings.startGrowingArousal;
    added.fullyExtendedArousal = settings.fullyExtendedArousal;
    added.extensionSeconds = settings.extensionSeconds;
    added.retractionSeconds = settings.retractionSeconds;
    // A new NPC starts with no morphs; the player's list is not copied.
    added.morphs.clear();

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
    SetNpcMessage(runtime
            ? std::format("Added {} (runtime-created: kept in this save's cosave, written when you save).", name)
            : std::format("Added {}.", name));
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
