#pragma once

#include "Settings.h"

namespace SPSArousalMorphs {
class LengthController;
class NpcController;

class DebugMenu {
public:
    DebugMenu(SettingsStore& settings, LengthController& controller, NpcController& npcs) :
        settings_(settings), controller_(controller), npcs_(npcs) {}

    void Register();
    void DrawNpcSlot(std::size_t slot);

private:
    // The framework's render callback carries no context, so each NPC page is
    // bound to one fixed slot and the slot says which NPC it shows.
    struct NpcPage {
        bool used{ false };
        std::string key;
        std::string title;
        NpcSetting draft;
        bool draftReady{ false };
        std::array<char, 128> newMorphName{};
    };

    static void __stdcall Render();
    static void __stdcall RenderNpcList();
    void Draw();
    void DrawNpcList();
    void DrawNpcPage(NpcPage& page);
    void DrawMorphEditor(std::vector<MorphSetting>& morphs, std::array<char, 128>& newName);
    void SavePlayerDraft();
    void SyncNpcPages();
    void QueueSyncNpcPages();
    void AddCrosshairNpc();
    void RemoveNpc(const std::string& key);
    void SetNpcMessage(std::string message);

    SettingsStore& settings_;
    LengthController& controller_;
    NpcController& npcs_;
    Settings draft_;
    bool draftReady_{ false };
    bool registered_{ false };

    std::array<char, 128> newMorphName_{};

    // Pages are written by SKSE tasks and read by the render callback.
    std::mutex pagesLock_;
    std::array<NpcPage, kMaximumNpcs> pages_;
    std::mutex npcMessageLock_;
    std::string npcMessage_;
};
}
