#include "PCH.h"

#include "DebugMenu.h"
#include "LengthController.h"
#include "MorphController.h"
#include "NpcController.h"
#include "SKEEAPI.h"
#include "SPSBridge.h"
#include "Settings.h"

namespace SPSArousalMorphs {
namespace {
SettingsStore g_settings;
SPSBridge g_sps;
MorphController g_morphs;
NpcController g_npcs(g_sps, g_morphs, g_settings);
LengthController g_controller(g_sps, g_morphs, g_npcs, g_settings);
DebugMenu g_menu(g_settings, g_controller, g_npcs);
SKEE::IInterfaceMap* g_skeeInterfaces{ nullptr };

bool ConnectSKEE()
{
    if (!g_skeeInterfaces) {
        if (!::GetModuleHandleW(L"skee64.dll")) {
            logger::error("RaceMenu/SKEE is unavailable: skee64.dll is not loaded");
            return false;
        }

        SKEE::InterfaceExchangeMessage exchange{};
        if (!SKSE::GetMessagingInterface()->Dispatch(
                SKEE::InterfaceExchangeMessage::kMessage_ExchangeInterface,
                &exchange, sizeof(exchange), "skee") ||
            !exchange.interfaceMap) {
            logger::error("RaceMenu/SKEE interface exchange failed");
            return false;
        }
        g_skeeInterfaces = exchange.interfaceMap;
    }

    return g_morphs.Connect(g_skeeInterfaces);
}

void ConnectDependencies()
{
    g_sps.Connect();
    ConnectSKEE();
}

void OnMessage(SKSE::MessagingInterface::Message* message)
{
    if (!message) {
        return;
    }

    switch (message->type) {
    case SKSE::MessagingInterface::kPostPostLoad:
        ConnectDependencies();
        break;
    case SKSE::MessagingInterface::kDataLoaded:
        ConnectDependencies();
        g_menu.Register();
        g_controller.StartWorker();
        break;
    case SKSE::MessagingInterface::kPreLoadGame:
        g_controller.EndGameSession();
        break;
    case SKSE::MessagingInterface::kPostLoadGame:
    case SKSE::MessagingInterface::kNewGame:
        ConnectDependencies();
        g_controller.BeginGameSession();
        break;
    default:
        break;
    }
}
}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    const auto runtimeVersion = skse->RuntimeVersion().string(".");
    // CommonLib's default log setup and spdlog's file sink both convert the log
    // path with path::string(), which throws during plugin load when the
    // Documents path has characters outside the system code page. CommonLib's
    // logger is disabled and the file is opened here through a wide path.
    SKSE::Init(skse, SKSE::InitInfo{ .log = false });

    static std::ofstream logStream;
    if (const auto directory = SKSE::log::log_directory()) {
        std::error_code error;
        std::filesystem::create_directories(*directory, error);
        logStream.open(*directory / L"SPSArousalMorphs.log",
            std::ios::out | std::ios::trunc | std::ios::binary);
    }
    std::shared_ptr<spdlog::sinks::sink> sink = logStream.is_open()
        ? std::shared_ptr<spdlog::sinks::sink>(
              std::make_shared<spdlog::sinks::ostream_sink_mt>(logStream, true))
        : std::shared_ptr<spdlog::sinks::sink>(std::make_shared<spdlog::sinks::null_sink_mt>());
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("global", std::move(sink)));
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::info);

    SPSArousalMorphs::g_settings.Load();
    if (!SKSE::GetMessagingInterface()->RegisterListener(SPSArousalMorphs::OnMessage)) {
        logger::critical("Failed to register SKSE message listener");
        return false;
    }

    logger::info("SPS Arousal Morphs 1.2.1 loaded");
    logger::info("Skyrim runtime detected: {}", runtimeVersion);
    logger::info("SPS remains the physics owner; the addon only requests a temporary SMP hold while extension finishes");
    return true;
}
