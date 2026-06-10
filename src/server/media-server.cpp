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


#include "icy/filesystem.h"
#include "icy/logger.h"
#include "icy/loop.h"
#include "icy/platform.h"
#include "internal/app.h"
#include "internal/config.h"
#include "internal/runtimeinfo.h"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>


using namespace icy;

namespace {

constexpr const char* kDefaultConfigPath = "./config.json";

// Exit codes (documented in --help and the README):
//   0 success / doctor ready
//   1 runtime failure / doctor not ready
//   2 usage error (bad flag or flag value)
//   3 config error (missing or invalid config file)
//   4 startup preflight failure
constexpr int kExitOk = 0;
constexpr int kExitRuntime = 1;
constexpr int kExitUsage = 2;
constexpr int kExitConfig = 3;
constexpr int kExitPreflight = 4;

void printVersion()
{
    std::cout << media_server::kServiceName << " " << ICEY_SERVER_VERSION << '\n';
}

void printUsage(const char* argv0)
{
    std::cout
        << "Usage: " << argv0 << " [options]\n"
        << "\nOptions:\n"
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
        << "  --web-root <path>             Built web UI directory (defaults to ./web/dist,\n"
        << "                                falls back to packaged share/icey-server/web)\n"
        << "  --log-level <level>           trace|debug|info|warn|error (default: info)\n"
        << "  --loop                        Enable looping in stream mode\n"
        << "  --no-loop                     Disable looping in stream mode\n"
        << "  --no-turn                     Disable embedded TURN server\n"
        << "  --doctor                      Print preflight diagnostics and exit\n"
        << "  --version                     Print version and exit\n"
        << "  -h, --help                    Show this help and exit\n"
        << "\nEnvironment variables (precedence: defaults < config file < env < flags):\n"
        << "  ICEY_CONFIG, ICEY_HOST, ICEY_PORT, ICEY_TURN_PORT,\n"
        << "  ICEY_TURN_EXTERNAL_IP, ICEY_MODE, ICEY_SOURCE, ICEY_RECORD_DIR,\n"
        << "  ICEY_WEB_ROOT, ICEY_TLS_CERT, ICEY_TLS_KEY, ICEY_LOG_LEVEL,\n"
        << "  ICEY_LOOP (true|false), ICEY_TURN (true|false),\n"
        << "  ICEY_AUTH_TOKEN, ICEY_TURN_SECRET\n"
        << "\nExit codes:\n"
        << "  0 success (or --doctor ready), 1 runtime failure (or --doctor not\n"
        << "  ready), 2 usage error, 3 config error, 4 startup preflight failure\n";
}

bool parseModeArg(const std::string& value, media_server::Config::Mode& out)
{
    return media_server::Config::tryParseMode(value, out);
}

bool parsePortArg(const std::string& value, uint16_t& out)
{
    int port = 0;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), port);
    if (ec != std::errc{} || ptr != value.data() + value.size() ||
        port < 1 || port > 65535) {
        return false;
    }
    out = static_cast<uint16_t>(port);
    return true;
}

bool parseLogLevelArg(const std::string& value, Level& out)
{
    if (value == "trace")
        out = Level::Trace;
    else if (value == "debug")
        out = Level::Debug;
    else if (value == "info")
        out = Level::Info;
    else if (value == "warn")
        out = Level::Warn;
    else if (value == "error")
        out = Level::Error;
    else
        return false;
    return true;
}

bool parseBoolArg(const std::string& value, bool& out)
{
    if (value == "1" || value == "true" || value == "yes" || value == "on")
        out = true;
    else if (value == "0" || value == "false" || value == "no" || value == "off")
        out = false;
    else
        return false;
    return true;
}

void resolvePackagedWebRoot(media_server::Config& config, bool explicitWebRoot)
{
    // An operator-chosen web root (flag, env, or config file) is never
    // second-guessed; the packaged fallback only covers the built-in default.
    if (explicitWebRoot)
        return;

    const auto defaultIndex = fs::makePath(config.webRoot, "index.html");
    if (fs::exists(defaultIndex))
        return;

    const auto exeDir = fs::dirname(getExePath());
    const auto packagedWebRoot = fs::normalize(
        fs::makePath(fs::makePath(exeDir, ".."), "share/icey-server/web"));
    const auto packagedIndex = fs::makePath(packagedWebRoot, "index.html");
    if (fs::exists(packagedIndex))
        config.webRoot = packagedWebRoot;
}

/// Applies ICEY_* environment overrides between the config file and CLI
/// flags. Returns false (with a message on stderr) for unparseable values.
bool applyEnvOverrides(media_server::Config& config, bool& webRootExplicit)
{
    const auto env = [](const char* name) -> const char* {
        const char* value = std::getenv(name);
        return (value && *value) ? value : nullptr;
    };

    if (const char* host = env("ICEY_HOST"))
        config.host = host;
    if (const char* port = env("ICEY_PORT")) {
        if (!parsePortArg(port, config.port)) {
            std::cerr << "Error: invalid ICEY_PORT '" << port
                      << "'. Expected 1..65535.\n";
            return false;
        }
    }
    if (const char* port = env("ICEY_TURN_PORT")) {
        if (!parsePortArg(port, config.turnPort)) {
            std::cerr << "Error: invalid ICEY_TURN_PORT '" << port
                      << "'. Expected 1..65535.\n";
            return false;
        }
    }
    if (const char* ip = env("ICEY_TURN_EXTERNAL_IP"))
        config.turnExternalIP = ip;
    if (const char* mode = env("ICEY_MODE")) {
        if (!parseModeArg(mode, config.mode)) {
            std::cerr << "Error: invalid ICEY_MODE '" << mode
                      << "'. Expected stream, record, or relay.\n";
            return false;
        }
    }
    if (const char* source = env("ICEY_SOURCE"))
        config.source = source;
    if (const char* dir = env("ICEY_RECORD_DIR"))
        config.recordDir = dir;
    if (const char* root = env("ICEY_WEB_ROOT")) {
        config.webRoot = root;
        webRootExplicit = true;
    }
    if (const char* cert = env("ICEY_TLS_CERT"))
        config.tls.certFile = cert;
    if (const char* key = env("ICEY_TLS_KEY"))
        config.tls.keyFile = key;
    // "auto" means "no override" for the boolean toggles; the Docker
    // entrypoint historically used it as the ICEY_LOOP default.
    if (const char* loop = env("ICEY_LOOP")) {
        if (std::string_view(loop) != "auto" &&
            !parseBoolArg(loop, config.loop)) {
            std::cerr << "Error: invalid ICEY_LOOP '" << loop
                      << "'. Expected true, false, or auto.\n";
            return false;
        }
    }
    if (const char* turn = env("ICEY_TURN")) {
        if (std::string_view(turn) != "auto" &&
            !parseBoolArg(turn, config.enableTurn)) {
            std::cerr << "Error: invalid ICEY_TURN '" << turn
                      << "'. Expected true, false, or auto.\n";
            return false;
        }
    }
    return true;
}

/// Runs the event loop until SIGINT or SIGTERM, then shuts the app down.
/// SIGTERM matters as much as SIGINT: it is what `docker stop` and systemd
/// send, and without it every deploy-time stop is an unclean kill.
void runUntilShutdown(media_server::MediaServerApp& app)
{
    struct ShutdownContext
    {
        media_server::MediaServerApp* app;
        uv_signal_t sigint{};
        uv_signal_t sigterm{};
        bool fired = false;
    } context;
    context.app = &app;

    const auto handler = [](uv_signal_t* req, int /*signum*/) {
        auto* ctx = static_cast<ShutdownContext*>(req->data);
        if (ctx->fired)
            return;
        ctx->fired = true;
        ctx->app->shutdown();
        uv_close(reinterpret_cast<uv_handle_t*>(&ctx->sigint), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&ctx->sigterm), nullptr);
    };

    auto* loop = uv::defaultLoop();
    uv_signal_init(loop, &context.sigint);
    context.sigint.data = &context;
    uv_signal_start(&context.sigint, handler, SIGINT);
    uv_signal_init(loop, &context.sigterm);
    context.sigterm.data = &context;
    uv_signal_start(&context.sigterm, handler, SIGTERM);

    uv_run(loop, UV_RUN_DEFAULT);
}

} // namespace


int main(int argc, char** argv)
{
    std::string configPath = kDefaultConfigPath;
    bool configExplicit = false;
    std::string logLevelValue = "info";
    bool doctorMode = false;

    if (const char* envConfig = std::getenv("ICEY_CONFIG")) {
        if (*envConfig) {
            configPath = envConfig;
            configExplicit = true;
        }
    }
    if (const char* envLogLevel = std::getenv("ICEY_LOG_LEVEL")) {
        if (*envLogLevel)
            logLevelValue = envLogLevel;
    }

    // First pass: short-circuit flags plus everything needed before the
    // config file is read (config path, log level).
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return kExitOk;
        }
        if (arg == "--version") {
            printVersion();
            return kExitOk;
        }
        if (arg == "--doctor") {
            doctorMode = true;
        }
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            configPath = argv[++i];
            configExplicit = true;
        }
        if (arg == "--log-level" && i + 1 < argc) {
            logLevelValue = argv[++i];
        }
    }

    Level logLevel = Level::Info;
    if (!parseLogLevelArg(logLevelValue, logLevel)) {
        std::cerr << "Error: invalid log level '" << logLevelValue
                  << "'. Expected trace, debug, info, warn, or error.\n";
        return kExitUsage;
    }
    Logger::instance().add(std::make_unique<ConsoleChannel>("console", logLevel));

    auto configLoad = media_server::loadConfigResult(configPath);
    if (configExplicit && !configLoad.exists) {
        // A typo'd --config silently running on built-in defaults is the
        // worst outcome; only the implicit ./config.json may be absent.
        std::cerr << "Error: config file not found: " << configPath << '\n';
        Logger::destroy();
        return kExitConfig;
    }
    media_server::Config config = configLoad.config;
    bool webRootExplicit = configLoad.webRootExplicit;

    if (!applyEnvOverrides(config, webRootExplicit)) {
        Logger::destroy();
        return kExitUsage;
    }

    // Second pass: CLI flags (highest precedence).
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        // A following token that looks like a flag is a missing value, not a
        // value; blaming the next token would point at the wrong argument.
        const bool hasValue =
            (i + 1 < argc) && std::string_view(argv[i + 1]).rfind("--", 0) != 0;
        std::string val = hasValue ? argv[i + 1] : "";

        const auto missingValue = [&](const char* flag) {
            std::cerr << "Error: flag '" << flag << "' requires a value\n";
            printUsage(argv[0]);
            Logger::destroy();
        };

        if (arg == "-c" || arg == "--config") {
            if (!hasValue) {
                missingValue(arg.c_str());
                return kExitUsage;
            }
            ++i;
        }
        else if (arg == "--log-level") {
            if (!hasValue) {
                missingValue(arg.c_str());
                return kExitUsage;
            }
            ++i; // validated in the first pass
        }
        else if ((arg == "-h" || arg == "--help") || arg == "--version" || arg == "--doctor") {
        }
        else if (arg == "--host") {
            if (!hasValue) {
                missingValue("--host");
                return kExitUsage;
            }
            config.host = val;
            ++i;
        }
        else if (arg == "--port") {
            if (!hasValue) {
                missingValue("--port");
                return kExitUsage;
            }
            if (!parsePortArg(val, config.port)) {
                std::cerr << "Error: invalid port '" << val << "'. Expected 1..65535.\n";
                Logger::destroy();
                return kExitUsage;
            }
            ++i;
        }
        else if (arg == "--tls-cert") {
            if (!hasValue) {
                missingValue("--tls-cert");
                return kExitUsage;
            }
            config.tls.certFile = val;
            ++i;
        }
        else if (arg == "--tls-key") {
            if (!hasValue) {
                missingValue("--tls-key");
                return kExitUsage;
            }
            config.tls.keyFile = val;
            ++i;
        }
        else if (arg == "--turn-port") {
            if (!hasValue) {
                missingValue("--turn-port");
                return kExitUsage;
            }
            if (!parsePortArg(val, config.turnPort)) {
                std::cerr << "Error: invalid turn port '" << val << "'. Expected 1..65535.\n";
                Logger::destroy();
                return kExitUsage;
            }
            ++i;
        }
        else if (arg == "--turn-external-ip") {
            if (!hasValue) {
                missingValue("--turn-external-ip");
                return kExitUsage;
            }
            config.turnExternalIP = val;
            ++i;
        }
        else if (arg == "--mode") {
            if (!hasValue) {
                missingValue("--mode");
                return kExitUsage;
            }
            if (!parseModeArg(val, config.mode)) {
                std::cerr << "Error: invalid mode '" << val << "'. Expected stream, record, or relay.\n";
                Logger::destroy();
                return kExitUsage;
            }
            ++i;
        }
        else if (arg == "--source") {
            if (!hasValue) {
                missingValue("--source");
                return kExitUsage;
            }
            config.source = val;
            ++i;
        }
        else if (arg == "--record-dir") {
            if (!hasValue) {
                missingValue("--record-dir");
                return kExitUsage;
            }
            config.recordDir = val;
            ++i;
        }
        else if (arg == "--web-root") {
            if (!hasValue) {
                missingValue("--web-root");
                return kExitUsage;
            }
            config.webRoot = val;
            webRootExplicit = true;
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
            std::cerr << "Error: unknown argument '" << arg << "'\n";
            printUsage(argv[0]);
            Logger::destroy();
            return kExitUsage;
        }
    }

    resolvePackagedWebRoot(config, webRootExplicit);

    media_server::MediaServerApp app(config, configLoad);
    if (doctorMode) {
        const auto report = app.doctorJson();
        std::cout << report.dump(2) << '\n';
        Logger::destroy();
        return report.value("ready", false) ? kExitOk : kExitRuntime;
    }
    if (!configLoad.valid) {
        std::cerr << "Error: invalid config file '" << configPath
                  << "': " << configLoad.error << '\n';
        Logger::destroy();
        return kExitConfig;
    }
    for (const auto& warning : configLoad.warnings)
        std::cerr << "Warning: " << warning << '\n';
    if (!app.start()) {
        Logger::destroy();
        return kExitPreflight;
    }

    runUntilShutdown(app);

    Logger::destroy();
    return kExitOk;
}

/// @}
