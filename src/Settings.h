#pragma once

namespace SPSArousalMorphs {
struct MorphSetting {
    std::string name{ "Penis_Length" };
    float retractedValue{ -0.50F };
    float extendedValue{ 0.0F };
    bool enabled{ true };

    bool operator==(const MorphSetting&) const = default;
};

// Stored as plugin plus local form ID because the INI is shared by every save
// and raw form IDs change with the load order.
struct NpcSetting {
    std::string plugin;
    std::uint32_t localFormID{ 0 };
    std::string name;
    bool enabled{ true };
    float startGrowingArousal{ 20.0F };
    float fullyExtendedArousal{ 60.0F };
    float extensionSeconds{ 3.0F };
    float retractionSeconds{ 3.0F };
    std::vector<MorphSetting> morphs;

    [[nodiscard]] std::string Key() const { return std::format("{}|{:08X}", plugin, localFormID); }
    bool operator==(const NpcSetting&) const = default;
};

inline constexpr std::size_t kMaximumNpcs = 32;

struct Settings {
    bool enabled{ false };
    // Ask SPS for a temporary SMP hold while the morphs finish extending.
    bool smpHold{ true };
    float startGrowingArousal{ 20.0F };
    float fullyExtendedArousal{ 60.0F };
    float extensionSeconds{ 3.0F };
    float retractionSeconds{ 3.0F };
    std::vector<MorphSetting> morphs{ MorphSetting{} };
    std::vector<NpcSetting> npcs;

    bool operator==(const Settings&) const = default;
};

class SettingsStore {
public:
    void Load();
    bool UpdateAndSave(Settings value);
    [[nodiscard]] Settings Snapshot() const;

private:
    static Settings Normalize(Settings value);
    bool Save(const Settings& value) const;

    mutable std::mutex lock_;
    Settings value_;
};
}
