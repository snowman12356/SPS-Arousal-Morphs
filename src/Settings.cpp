#include "PCH.h"

#include "Settings.h"

#include <SimpleIni.h>

namespace SPSArousalMorphs {
namespace {
constexpr auto kSettingsPath = "Data/SKSE/Plugins/SPSArousalMorphs.ini";
// Settings file used before the rename. Read only when the new file is missing,
// then saved under the new name; the old file is left untouched.
constexpr auto kLegacySettingsPath = "Data/SKSE/Plugins/SPSRetractableAddon.ini";
constexpr std::size_t kMaximumMorphs = 32;

std::string Trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

void NormalizeTiming(float& start, float& full, float& extension, float& retraction)
{
    start = std::clamp(start, 0.0F, 99.0F);
    full = std::clamp(full, start + 1.0F, 100.0F);
    extension = std::clamp(extension, 0.1F, 30.0F);
    retraction = std::clamp(retraction, 0.1F, 30.0F);
}

void NormalizeMorphs(std::vector<MorphSetting>& morphs)
{
    std::vector<MorphSetting> normalized;
    normalized.reserve(std::min(morphs.size(), kMaximumMorphs));
    std::unordered_set<std::string> names;
    for (auto& morph : morphs) {
        morph.name = Trim(std::move(morph.name));
        if (morph.name.empty() || names.contains(morph.name)) {
            continue;
        }
        morph.retractedValue = std::clamp(morph.retractedValue, -3.0F, 3.0F);
        morph.extendedValue = std::clamp(morph.extendedValue, -3.0F, 3.0F);
        names.insert(morph.name);
        normalized.push_back(std::move(morph));
        if (normalized.size() == kMaximumMorphs) {
            break;
        }
    }
    morphs = std::move(normalized);
}

void LoadMorphs(CSimpleIniA& ini, const std::string& prefix, long count, std::vector<MorphSetting>& morphs)
{
    morphs.clear();
    const auto limit = std::min<long>(count, static_cast<long>(kMaximumMorphs));
    for (long index = 1; index <= limit; ++index) {
        const auto section = std::format("{}{}", prefix, index);
        MorphSetting morph;
        morph.name = ini.GetValue(section.c_str(), "Name", "");
        morph.retractedValue = static_cast<float>(
            ini.GetDoubleValue(section.c_str(), "RetractedValue", morph.retractedValue));
        morph.extendedValue = static_cast<float>(
            ini.GetDoubleValue(section.c_str(), "ExtendedValue", morph.extendedValue));
        morph.enabled = ini.GetBoolValue(section.c_str(), "Enabled", morph.enabled);
        morphs.push_back(std::move(morph));
    }
}

void SaveMorphs(CSimpleIniA& ini, const std::string& prefix, const std::vector<MorphSetting>& morphs)
{
    for (std::size_t index = 0; index < morphs.size(); ++index) {
        const auto section = std::format("{}{}", prefix, index + 1);
        const auto& morph = morphs[index];
        ini.SetValue(section.c_str(), "Name", morph.name.c_str());
        ini.SetBoolValue(section.c_str(), "Enabled", morph.enabled);
        ini.SetDoubleValue(section.c_str(), "RetractedValue", morph.retractedValue);
        ini.SetDoubleValue(section.c_str(), "ExtendedValue", morph.extendedValue);
    }
}
}

Settings SettingsStore::Normalize(Settings value)
{
    NormalizeTiming(value.startGrowingArousal, value.fullyExtendedArousal,
        value.extensionSeconds, value.retractionSeconds);
    NormalizeMorphs(value.morphs);

    std::vector<NpcSetting> npcs;
    std::unordered_set<std::string> keys;
    for (auto& npc : value.npcs) {
        npc.plugin = Trim(std::move(npc.plugin));
        if (npc.plugin.empty() || npc.localFormID == 0 || !keys.insert(npc.Key()).second) {
            continue;
        }
        NormalizeTiming(npc.startGrowingArousal, npc.fullyExtendedArousal,
            npc.extensionSeconds, npc.retractionSeconds);
        NormalizeMorphs(npc.morphs);
        npcs.push_back(std::move(npc));
        if (npcs.size() == kMaximumNpcs) {
            break;
        }
    }
    value.npcs = std::move(npcs);
    return value;
}

void SettingsStore::Load()
{
    CSimpleIniA ini;
    ini.SetUnicode();
    auto result = ini.LoadFile(kSettingsPath);
    bool migratedLegacyFile = false;
    if (result < 0) {
        ini.Reset();
        result = ini.LoadFile(kLegacySettingsPath);
        migratedLegacyFile = result >= 0;
        if (migratedLegacyFile) {
            logger::info("Migrating settings from {} to {}", kLegacySettingsPath, kSettingsPath);
        }
    }

    Settings loaded;
    bool migratedLegacyMorph = false;
    if (result >= 0) {
        loaded.enabled = ini.GetBoolValue("General", "Enabled", loaded.enabled);
        loaded.smpHold = ini.GetBoolValue("General", "SMPHold", loaded.smpHold);
        loaded.startGrowingArousal = static_cast<float>(
            ini.GetDoubleValue("Length", "StartGrowingArousal", loaded.startGrowingArousal));
        loaded.fullyExtendedArousal = static_cast<float>(
            ini.GetDoubleValue("Length", "FullyExtendedArousal", loaded.fullyExtendedArousal));
        loaded.extensionSeconds = static_cast<float>(
            ini.GetDoubleValue("Timing", "ExtensionSeconds", loaded.extensionSeconds));
        loaded.retractionSeconds = static_cast<float>(
            ini.GetDoubleValue("Timing", "RetractionSeconds", loaded.retractionSeconds));

        const auto configuredCount = ini.GetLongValue("Morphs", "Count", -1);
        if (configuredCount >= 0) {
            LoadMorphs(ini, "Morph", configuredCount, loaded.morphs);
        } else {
            loaded.morphs.front().retractedValue = static_cast<float>(
                ini.GetDoubleValue("Length", "MinimumPenisLength",
                    loaded.morphs.front().retractedValue));
            migratedLegacyMorph = true;
        }

        const auto npcCount = std::min<long>(
            ini.GetLongValue("NPCs", "Count", 0), static_cast<long>(kMaximumNpcs));
        for (long index = 1; index <= npcCount; ++index) {
            const auto section = std::format("NPC{}", index);
            NpcSetting npc;
            npc.plugin = ini.GetValue(section.c_str(), "Plugin", "");
            npc.localFormID = static_cast<std::uint32_t>(
                std::strtoul(ini.GetValue(section.c_str(), "LocalFormID", "0"), nullptr, 16));
            npc.name = ini.GetValue(section.c_str(), "Name", "");
            npc.enabled = ini.GetBoolValue(section.c_str(), "Enabled", npc.enabled);
            npc.startGrowingArousal = static_cast<float>(
                ini.GetDoubleValue(section.c_str(), "StartGrowingArousal", npc.startGrowingArousal));
            npc.fullyExtendedArousal = static_cast<float>(
                ini.GetDoubleValue(section.c_str(), "FullyExtendedArousal", npc.fullyExtendedArousal));
            npc.extensionSeconds = static_cast<float>(
                ini.GetDoubleValue(section.c_str(), "ExtensionSeconds", npc.extensionSeconds));
            npc.retractionSeconds = static_cast<float>(
                ini.GetDoubleValue(section.c_str(), "RetractionSeconds", npc.retractionSeconds));
            LoadMorphs(ini, section + ".Morph", ini.GetLongValue(section.c_str(), "MorphCount", 0), npc.morphs);
            loaded.npcs.push_back(std::move(npc));
        }
    } else {
        logger::info("Addon settings file not found; using safe defaults");
    }

    loaded = Normalize(loaded);
    {
        std::scoped_lock guard(lock_);
        value_ = loaded;
    }
    if (result < 0 || migratedLegacyMorph || migratedLegacyFile) {
        Save(loaded);
    }

    logger::info(
        "Settings loaded: enabled={}, starts growing at {:.0f}, fully extended at {:.0f}, extension {:.1f}s, retraction {:.1f}s, morphs={}",
        loaded.enabled, loaded.startGrowingArousal, loaded.fullyExtendedArousal,
        loaded.extensionSeconds, loaded.retractionSeconds, loaded.morphs.size());
    for (const auto& morph : loaded.morphs) {
        logger::info("Configured morph: {} (enabled={}, retracted {:.2f}, extended {:.2f})",
            morph.name, morph.enabled, morph.retractedValue, morph.extendedValue);
    }
    for (const auto& npc : loaded.npcs) {
        logger::info("Configured NPC: {} ({}:{:08X}, enabled={}, morphs={})",
            npc.name, npc.plugin, npc.localFormID, npc.enabled, npc.morphs.size());
    }
}

bool SettingsStore::UpdateAndSave(Settings value)
{
    value = Normalize(value);
    if (!Save(value)) {
        return false;
    }
    {
        std::scoped_lock guard(lock_);
        value_ = value;
    }
    return true;
}

Settings SettingsStore::Snapshot() const
{
    std::scoped_lock guard(lock_);
    return value_;
}

bool SettingsStore::Save(const Settings& value) const
{
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.SetBoolValue("General", "Enabled", value.enabled);
    ini.SetBoolValue("General", "SMPHold", value.smpHold);
    ini.SetDoubleValue("Length", "StartGrowingArousal", value.startGrowingArousal);
    ini.SetDoubleValue("Length", "FullyExtendedArousal", value.fullyExtendedArousal);
    ini.SetDoubleValue("Timing", "ExtensionSeconds", value.extensionSeconds);
    ini.SetDoubleValue("Timing", "RetractionSeconds", value.retractionSeconds);
    ini.SetLongValue("Morphs", "Count", static_cast<long>(value.morphs.size()));
    SaveMorphs(ini, "Morph", value.morphs);
    ini.SetLongValue("NPCs", "Count", static_cast<long>(value.npcs.size()));
    for (std::size_t index = 0; index < value.npcs.size(); ++index) {
        const auto section = std::format("NPC{}", index + 1);
        const auto& npc = value.npcs[index];
        ini.SetValue(section.c_str(), "Plugin", npc.plugin.c_str());
        ini.SetValue(section.c_str(), "LocalFormID", std::format("{:08X}", npc.localFormID).c_str());
        ini.SetValue(section.c_str(), "Name", npc.name.c_str());
        ini.SetBoolValue(section.c_str(), "Enabled", npc.enabled);
        ini.SetDoubleValue(section.c_str(), "StartGrowingArousal", npc.startGrowingArousal);
        ini.SetDoubleValue(section.c_str(), "FullyExtendedArousal", npc.fullyExtendedArousal);
        ini.SetDoubleValue(section.c_str(), "ExtensionSeconds", npc.extensionSeconds);
        ini.SetDoubleValue(section.c_str(), "RetractionSeconds", npc.retractionSeconds);
        ini.SetLongValue(section.c_str(), "MorphCount", static_cast<long>(npc.morphs.size()));
        SaveMorphs(ini, section + ".Morph", npc.morphs);
    }
    if (ini.SaveFile(kSettingsPath) < 0) {
        logger::error("Could not save addon settings to {}", kSettingsPath);
        return false;
    }
    return true;
}
}
