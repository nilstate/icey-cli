///
//
// icey
// Copyright (c) 2005, icey <https://0state.com>
//
// SPDX-License-Identifier: LGPL-2.1+
//
// icey-server
//
// Single binary: WebRTC media streaming + Symple signalling + TURN relay
// + web UI. No Node.js, no third-party services. One binary, two ports.
//
// The server registers as a virtual peer in the Symple network.
// Browsers discover it via presence, click Call, and receive video + audio.
//
// Pipeline (stream mode):
//   MediaCapture -> VideoPacketEncoder -> WebRtcTrackSender -> [browser]
//                -> AudioPacketEncoder -> WebRtcTrackSender -> [browser]
//
// Pipeline (record mode):
//   WebRtcTrackReceiver -> VideoDecoder -> MultiplexPacketEncoder -> MP4
//
// Pipeline (relay mode):
//   [browser source] -> WebRtcTrackReceiver -> relay controller -> WebRtcTrackSender -> [browser viewers]
//
// Usage:
//   icey-server --source video.mp4 --web-root web/dist
//
/// @addtogroup webrtc
/// @{


#include "icy/application.h"
#include "icy/logger.h"
#include "internal/app.h"
#include "internal/config.h"
#include "internal/runtimeinfo.h"

#include <memory>
#include <string>


using namespace icy;

namespace {

constexpr const char* kDefaultConfigPath = "./config.json";

void printVersion()
{
    std::cout << media_server::kServiceName << " " << ICEY_SERVER_VERSION << '\n';
}

void printUsage(const char* argv0)
{
    std::cout
        << "Usage: " << argv0 << " [options]\n"
        << "  -c, --config <path>           Config file (default: " << kDefaultConfigPath << ")\n"
        << "  --host <host>                 Bind host (default from config)\n"
        << "  --port <port>                 HTTP/WS port (default: 4500)\n"
        << "  --tls-cert <path>             TLS certificate for direct HTTPS/WSS serving\n"
        << "  --tls-key <path>              TLS private key for direct HTTPS/WSS serving\n"
        << "  --turn-port <port>            TURN port (default: 3478)\n"
        << "  --turn-external-ip <ip>       Public IP advertised by TURN\n"
        << "  --mode <mode>                 stream|record|relay\n"
        << "  --source <path-or-url>        Media source file, device, or RTSP URL\n"
        << "  --record-dir <path>           Output directory for record mode\n"
        << "  --web-root <path>             Built web UI directory\n"
        << "  --loop                        Enable looping in stream mode\n"
        << "  --no-loop                     Disable looping in stream mode\n"
        << "  --no-turn                     Disable embedded TURN server\n"
        << "  --doctor                      Print preflight diagnostics and exit\n"
        << "  --version                     Print version and exit\n"
        << "  -h, --help                    Show this help and exit\n";
}

bool parseModeArg(const std::string& value, media_server::Config::Mode& out)
{
    if (value == "stream") {
        out = media_server::Config::Mode::Stream;
        return true;
    }
    if (value == "record") {
        out = media_server::Config::Mode::Record;
        return true;
    }
    if (value == "relay") {
        out = media_server::Config::Mode::Relay;
        return true;
    }
    return false;
}

} // namespace


int main(int argc, char** argv)
{
    Logger::instance().add(std::make_unique<ConsoleChannel>("debug", Level::Debug));

    std::string configPath = kDefaultConfigPath;
    bool doctorMode = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            Logger::destroy();
            return 0;
        }
        if (arg == "--version") {
            printVersion();
            Logger::destroy();
            return 0;
        }
        if (arg == "--doctor") {
            doctorMode = true;
        }
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            configPath = argv[++i];
        }
    }

    auto configLoad = media_server::loadConfigResult(configPath);
    media_server::Config config = configLoad.config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        const bool hasValue = (i + 1 < argc);
        std::string val = hasValue ? argv[i + 1] : "";

        if ((arg == "-c" || arg == "--config") && !val.empty()) {
            ++i;
        }
        else if ((arg == "-h" || arg == "--help") || arg == "--version" || arg == "--doctor") {
        }
        else if (arg == "--host" && !val.empty()) {
            config.host = val;
            ++i;
        }
        else if (arg == "--port" && !val.empty()) {
            config.port = static_cast<uint16_t>(std::stoi(val));
            ++i;
        }
        else if (arg == "--tls-cert" && !val.empty()) {
            config.tls.certFile = val;
            ++i;
        }
        else if (arg == "--tls-key" && !val.empty()) {
            config.tls.keyFile = val;
            ++i;
        }
        else if (arg == "--turn-port" && !val.empty()) {
            config.turnPort = static_cast<uint16_t>(std::stoi(val));
            ++i;
        }
        else if (arg == "--turn-external-ip" && !val.empty()) {
            config.turnExternalIP = val;
            ++i;
        }
        else if (arg == "--mode" && !val.empty()) {
            if (!parseModeArg(val, config.mode)) {
                std::cerr << "Error: invalid mode '" << val << "'. Expected stream, record, or relay.\n";
                printUsage(argv[0]);
                Logger::destroy();
                return 1;
            }
            ++i;
        }
        else if (arg == "--source" && !val.empty()) {
            config.source = val;
            ++i;
        }
        else if (arg == "--record-dir" && !val.empty()) {
            config.recordDir = val;
            ++i;
        }
        else if (arg == "--web-root" && !val.empty()) {
            config.webRoot = val;
            ++i;
        }
        else if (arg == "--loop") {
            config.loop = true;
        }
        else if (arg == "--no-loop") {
            config.loop = false;
        }
        else if (arg == "--no-turn") {
            config.enableTurn = false;
        }
        else {
            std::cerr << "Error: unknown or incomplete argument '" << arg << "'\n";
            printUsage(argv[0]);
            Logger::destroy();
            return 1;
        }
    }

    media_server::MediaServerApp app(config, configLoad);
    if (doctorMode) {
        const auto report = app.doctorJson();
        std::cout << report.dump(2) << '\n';
        Logger::destroy();
        return report.value("ready", false) ? 0 : 1;
    }
    if (!configLoad.valid) {
        std::cerr << "Error: invalid config file '" << configPath
                  << "': " << configLoad.error << '\n';
        Logger::destroy();
        return 1;
    }
    if (!app.start()) {
        Logger::destroy();
        return 1;
    }

    waitForShutdown([](void* opaque) {
        reinterpret_cast<media_server::MediaServerApp*>(opaque)->shutdown();
    }, &app);

    Logger::destroy();
    return 0;
}

/// @}
