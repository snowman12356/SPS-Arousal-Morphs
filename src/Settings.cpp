#include "PCH.h"

#include "Settings.h"

#include <SimpleIni.h>

namespace SPSArousalMorphs {
namespace {
constexpr auto kSettingsPath = "Data/SKSE/Plugins/SPSArousalMorphs.ini";
// Settings file used before the rename. Read only when the new file is missing,
// then saved under the new name; the old file is left untouched.
constexpr auto kLegacySettingsPath = "Data/SKSE/Plugins/SPSRetractableAddon.ini";
// Local form IDs hold 24 bits; anything larger would add into the load-order
// index in TESDataHandler::LookupFormID and find a form in another plugin.
constexpr std::uint32_t kMaximumLocalFormID = 0x00FFFFFF;
constexpr std::uint32_t kFirstRuntimeFormID = 0xFF000000;
// SKSE cosave: unique plugin ID, record type and version.
constexpr std::uint32_t kRuntimeNpcRecord = 'RNPC';
constexpr std::uint32_t kRuntimeNpcVersion = 1;
constexpr std::uint32_t kMaximumCosaveString = 256;

// SimpleIni parses with strtod, which accepts "nan"; std::clamp passes NaN through.
float Finite(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

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
    const Settings defaults;
    start = std::clamp(Finite(start, defaults.startGrowingArousal), 0.0F, 99.0F);
    full = std::clamp(Finite(full, defaults.fullyExtendedArousal), start + 1.0F, 100.0F);
    extension = std::clamp(Finite(extension, defaults.extensionSeconds), 0.1F, 30.0F);
    retraction = std::clamp(Finite(retraction, defaults.retractionSeconds), 0.1F, 30.0F);
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
        const MorphSetting defaults;
        morph.retractedValue = std::clamp(Finite(morph.retractedValue, defaults.retractedValue), -3.0F, 3.0F);
        morph.extendedValue = std::clamp(Finite(morph.extendedValue, defaults.extendedValue), -3.0F, 3.0F);
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
        if (npc.runtime) {
            if (npc.localFormID < kFirstRuntimeFormID) {
                continue;
            }
        } else if (npc.plugin.empty() || npc.localFormID == 0 || npc.localFormID > kMaximumLocalFormID) {
            if (npc.localFormID > kMaximumLocalFormID) {
                logger::warn("Ignoring NPC {} ({}:{:08X}): local form ID is out of range",
                    npc.name, npc.plugin, npc.localFormID);
            }
            continue;
        }
        if (!keys.insert(npc.Key()).second) {
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
        loaded.npcReadSeconds = std::clamp(static_cast<float>(
            ini.GetDoubleValue("General", "NpcReadSeconds", loaded.npcReadSeconds)), 0.1F, 10.0F);
        loaded.playerUpdateSeconds = std::clamp(static_cast<float>(
            ini.GetDoubleValue("General", "PlayerUpdateSeconds", loaded.playerUpdateSeconds)), 0.02F, 0.25F);
        loaded.sceneMode = std::clamp(
            static_cast<int>(ini.GetLongValue("General", "SceneMode", loaded.sceneMode)),
            static_cast<int>(kSceneFollowRole), static_cast<int>(kSceneKeepLength));
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
        // Runtime NPCs come from the save's cosave, not the INI; a reload keeps them.
        for (const auto& npc : value_.npcs) {
            if (npc.runtime && loaded.npcs.size() < kMaximumNpcs) {
                loaded.npcs.push_back(npc);
            }
        }
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
    ini.SetLongValue("General", "SceneMode", value.sceneMode);
    ini.SetDoubleValue("General", "NpcReadSeconds", value.npcReadSeconds);
    ini.SetDoubleValue("General", "PlayerUpdateSeconds", value.playerUpdateSeconds);
    ini.SetDoubleValue("Length", "StartGrowingArousal", value.startGrowingArousal);
    ini.SetDoubleValue("Length", "FullyExtendedArousal", value.fullyExtendedArousal);
    ini.SetDoubleValue("Timing", "ExtensionSeconds", value.extensionSeconds);
    ini.SetDoubleValue("Timing", "RetractionSeconds", value.retractionSeconds);
    ini.SetLongValue("Morphs", "Count", static_cast<long>(value.morphs.size()));
    SaveMorphs(ini, "Morph", value.morphs);
    std::vector<const NpcSetting*> shared;
    for (const auto& npc : value.npcs) {
        if (!npc.runtime) {
            shared.push_back(&npc);
        }
    }
    ini.SetLongValue("NPCs", "Count", static_cast<long>(shared.size()));
    for (std::size_t index = 0; index < shared.size(); ++index) {
        const auto section = std::format("NPC{}", index + 1);
        const auto& npc = *shared[index];
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

namespace {
void WriteString(const SKSE::SerializationInterface* intfc, const std::string& text)
{
    const auto length = static_cast<std::uint32_t>(std::min<std::size_t>(text.size(), kMaximumCosaveString));
    intfc->WriteRecordData(length);
    intfc->WriteRecordData(text.data(), length);
}

bool ReadString(const SKSE::SerializationInterface* intfc, std::string& text)
{
    std::uint32_t length = 0;
    if (intfc->ReadRecordData(length) != sizeof(length) || length > kMaximumCosaveString) {
        return false;
    }
    text.assign(length, '\0');
    return length == 0 || intfc->ReadRecordData(text.data(), length) == length;
}
}

void SettingsStore::SaveCosave(const SKSE::SerializationInterface* intfc) const
{
    std::vector<NpcSetting> runtime;
    for (auto& npc : Snapshot().npcs) {
        if (npc.runtime) {
            runtime.push_back(std::move(npc));
        }
    }
    if (runtime.empty() || !intfc->OpenRecord(kRuntimeNpcRecord, kRuntimeNpcVersion)) {
        return;
    }
    intfc->WriteRecordData(static_cast<std::uint32_t>(runtime.size()));
    for (const auto& npc : runtime) {
        intfc->WriteRecordData(npc.localFormID);
        WriteString(intfc, npc.name);
        intfc->WriteRecordData(static_cast<std::uint8_t>(npc.enabled ? 1 : 0));
        intfc->WriteRecordData(npc.startGrowingArousal);
        intfc->WriteRecordData(npc.fullyExtendedArousal);
        intfc->WriteRecordData(npc.extensionSeconds);
        intfc->WriteRecordData(npc.retractionSeconds);
        intfc->WriteRecordData(static_cast<std::uint32_t>(npc.morphs.size()));
        for (const auto& morph : npc.morphs) {
            WriteString(intfc, morph.name);
            intfc->WriteRecordData(static_cast<std::uint8_t>(morph.enabled ? 1 : 0));
            intfc->WriteRecordData(morph.retractedValue);
            intfc->WriteRecordData(morph.extendedValue);
        }
    }
    logger::info("Saved {} runtime-created NPC(s) to the cosave", runtime.size());
}

void SettingsStore::RevertCosave()
{
    std::scoped_lock guard(lock_);
    std::erase_if(value_.npcs, [](const NpcSetting& npc) { return npc.runtime; });
}

void SettingsStore::LoadCosave(const SKSE::SerializationInterface* intfc)
{
    std::vector<NpcSetting> restored;
    std::uint32_t type = 0;
    std::uint32_t version = 0;
    std::uint32_t length = 0;
    while (intfc->GetNextRecordInfo(type, version, length)) {
        if (type != kRuntimeNpcRecord || version != kRuntimeNpcVersion) {
            continue;
        }
        std::uint32_t count = 0;
        if (intfc->ReadRecordData(count) != sizeof(count)) {
            continue;
        }
        for (std::uint32_t index = 0; index < count && index < kMaximumNpcs; ++index) {
            NpcSetting npc;
            npc.runtime = true;
            std::uint8_t flag = 0;
            std::uint32_t morphCount = 0;
            bool ok = intfc->ReadRecordData(npc.localFormID) == sizeof(npc.localFormID) &&
                ReadString(intfc, npc.name) &&
                intfc->ReadRecordData(flag) == sizeof(flag) &&
                intfc->ReadRecordData(npc.startGrowingArousal) == sizeof(float) &&
                intfc->ReadRecordData(npc.fullyExtendedArousal) == sizeof(float) &&
                intfc->ReadRecordData(npc.extensionSeconds) == sizeof(float) &&
                intfc->ReadRecordData(npc.retractionSeconds) == sizeof(float) &&
                intfc->ReadRecordData(morphCount) == sizeof(morphCount) &&
                morphCount <= kMaximumMorphs;
            npc.enabled = flag != 0;
            for (std::uint32_t m = 0; ok && m < morphCount; ++m) {
                MorphSetting morph;
                std::uint8_t morphFlag = 0;
                ok = ReadString(intfc, morph.name) &&
                    intfc->ReadRecordData(morphFlag) == sizeof(morphFlag) &&
                    intfc->ReadRecordData(morph.retractedValue) == sizeof(float) &&
                    intfc->ReadRecordData(morph.extendedValue) == sizeof(float);
                morph.enabled = morphFlag != 0;
                npc.morphs.push_back(std::move(morph));
            }
            if (!ok) {
                logger::warn("Runtime NPC cosave record is damaged; ignoring the rest of it");
                break;
            }
            restored.push_back(std::move(npc));
        }
    }
    if (restored.empty()) {
        return;
    }
    std::scoped_lock guard(lock_);
    std::erase_if(value_.npcs, [](const NpcSetting& npc) { return npc.runtime; });
    for (auto& npc : restored) {
        if (value_.npcs.size() >= kMaximumNpcs) {
            break;
        }
        value_.npcs.push_back(std::move(npc));
    }
    value_ = Normalize(std::move(value_));
    logger::info("Restored {} runtime-created NPC(s) from the cosave", restored.size());
}
}
