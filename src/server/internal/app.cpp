#include "app.h"

#include "config.h"
#include "httpfactory.h"
#include "media.h"
#include "runtimeinfo.h"
#include "turnserver.h"

#include "icy/filesystem.h"
#include "icy/logger.h"

#include <fstream>
#include <iostream>
#include <vector>


namespace icy {
namespace media_server {
namespace {

bool isRemoteStreamSource(const std::string& source)
{
    return source.find("://") != std::string::npos
        || source.rfind("rtsp:", 0) == 0
        || source.rfind("udp:", 0) == 0
        || source.rfind("tcp:", 0) == 0
        || source.rfind("file:", 0) == 0;
}

bool isWildcardHost(const std::string& host)
{
    return host.empty() || host == "0.0.0.0" || host == "::";
}

std::string sourceKind(const std::string& source)
{
    if (source.empty())
        return "none";
    if (source.rfind("rtsp://", 0) == 0)
        return "rtsp";
    if (source.find("://") != std::string::npos)
        return "url";
    if (source.rfind("/dev/", 0) == 0)
        return "device";
    return "file";
}

json::Value makeCheck(const char* name,
                      const char* result,
                      const std::string& detail)
{
    json::Value check;
    check["name"] = name;
    check["result"] = result;
    check["detail"] = detail;
    return check;
}

} // namespace


MediaServerApp::MediaServerApp(const Config& config)
    : _config(config)
    , _relay(std::make_unique<RelayController>())
{
}


MediaServerApp::~MediaServerApp() = default;


bool MediaServerApp::start()
{
    const auto report = doctorJson();
    if (!report.value("ready", false)) {
        std::cerr << "Error: " << kServiceName << " preflight failed\n";
        if (report.contains("checks")) {
            for (const auto& check : report["checks"]) {
                if (check.value("result", "") == "fail") {
                    std::cerr << "  - " << check.value("name", "check")
                              << ": " << check.value("detail", "") << '\n';
                }
            }
        }
        std::cerr << "Run '" << kServiceName << " --doctor' for the full report.\n";
        return false;
    }

    if (report.contains("warnings")) {
        for (const auto& warning : report["warnings"])
            std::cerr << "Warning: " << warning.get<std::string>() << '\n';
    }

    if (_config.mode == Config::Mode::Record) {
        fs::mkdirr(_config.recordDir);
    }

    _startedAt = std::chrono::steady_clock::now();

    if (_config.enableTurn) {
        _turn = std::make_unique<EmbeddedTurn>(_config);
        _turn->start();
        std::cout << "TURN server listening on "
                  << _config.host << ":" << _config.turnPort << '\n';
    }

    smpl::Server::Options symOpts;
    symOpts.host = _config.host;
    symOpts.port = _config.port;
    symOpts.authentication = false;
    symOpts.dynamicRooms = true;

    _symple.Authenticate += [](smpl::ServerPeer&,
                               const json::Value&,
                               bool& allowed,
                               std::vector<std::string>& rooms) {
        allowed = true;
        rooms.push_back("public");
    };

    _symple.PeerConnected += [](smpl::ServerPeer& peer) {
        LInfo("Peer connected: ", peer.id(),
              " (", peer.peer().value("name", "?"), ")");
    };

    _symple.PeerDisconnected += [this](smpl::ServerPeer& peer) {
        const auto peerAddress = peer.peer().address().toString();
        LInfo("Peer disconnected: ", peerAddress);
        _relay->unregisterSession(peerAddress);
        std::lock_guard lock(_sessionMutex);
        _sessions.erase(peerAddress);
    };

    _symple.start(symOpts,
                  std::make_unique<HttpFactory>(
                      _config.webRoot,
                      HttpFactory::RuntimeConfig{
                          _config.enableTurn,
                          _config.turnPort,
                          _config.turnExternalIP,
                          _config.host,
                          kProductName,
                          kServiceName,
                          ICEY_SERVER_VERSION,
                          Config::modeName(_config.mode),
                          [this]() { return statusJson(); }
                      }));

    _serverPeerId = kServerPeerId;
    _serverAddress = "icey|" + _serverPeerId;

    smpl::Peer vpeer;
    vpeer.setID(_serverPeerId);
    vpeer.setUser(kProductName);
    vpeer.setName(kServerPeerName);
    vpeer.setType(kServerPeerType);
    vpeer["online"] = true;
    switch (_config.mode) {
    case Config::Mode::Stream:
        vpeer["mode"] = "stream";
        vpeer["capabilities"] = json::Value::array({"view"});
        break;
    case Config::Mode::Record:
        vpeer["mode"] = "record";
        vpeer["capabilities"] = json::Value::array({"publish"});
        break;
    case Config::Mode::Relay:
        vpeer["mode"] = "relay";
        vpeer["capabilities"] = json::Value::array({"publish", "view"});
        break;
    }

    _symple.addVirtualPeer(vpeer, {"public"},
        [this](const json::Value& msg) {
            auto it = msg.find("subtype");
            if (it == msg.end())
                return;

            smpl::Address from(msg.value("from", ""));
            if (!from.valid() || from.id.empty() || from.user.empty())
                return;
            auto peerAddress = from.toString();

            std::shared_ptr<MediaSession> session;
            const auto& subtype = it->get_ref<const std::string&>();
            if (subtype == "call:init") {
                session = ensureSession(peerAddress);
            }
            else {
                std::lock_guard lock(_sessionMutex);
                auto found = _sessions.find(peerAddress);
                if (found != _sessions.end())
                    session = found->second;
            }
            if (session)
                session->onSignallingMessage(msg);
        });

    std::cout << kServiceName << " listening on "
              << _config.host << ":" << _config.port << '\n';
    std::cout << "Web UI: http://localhost:" << _config.port << "/\n";
    if (_config.mode == Config::Mode::Stream && !_config.source.empty())
        std::cout << "Source: " << _config.source << '\n';
    if (_config.mode == Config::Mode::Record)
        std::cout << "Recordings: " << _config.recordDir << '\n';
    if (_config.mode == Config::Mode::Relay)
        std::cout << "Relay mode: first active caller becomes the live source; "
                     "other callers receive that feed\n";

    return true;
}


json::Value MediaServerApp::doctorJson() const
{
    json::Value j;
    json::Value checks = json::Value::array();
    json::Value warnings = json::Value::array();

    bool readyToRun = true;
    bool degraded = false;
    const auto sourceType = sourceKind(_config.source);
    const auto webIndex = fs::makePath(_config.webRoot, "index.html");

    j["product"] = kProductName;
    j["service"] = kServiceName;
    j["mode"] = Config::modeName(_config.mode);
    j["source"]["value"] = _config.source;
    j["source"]["kind"] = sourceType;
    j["source"]["remote"] = isRemoteStreamSource(_config.source);
    j["http"]["host"] = _config.host;
    j["http"]["port"] = _config.port;
    j["turn"]["enabled"] = _config.enableTurn;
    j["turn"]["port"] = _config.turnPort;
    j["turn"]["externalIp"] = _config.turnExternalIP;
    j["web"]["root"] = _config.webRoot;

    if (fs::exists(webIndex)) {
        checks.push_back(makeCheck("web_root", "pass",
            "Found bundled web UI at " + webIndex));
    }
    else {
        checks.push_back(makeCheck("web_root", "fail",
            "Missing bundled web UI at " + webIndex));
        readyToRun = false;
    }

    if (_config.port == _config.turnPort && _config.enableTurn) {
        checks.push_back(makeCheck("ports", "fail",
            "HTTP/WS port and TURN port must not be the same"));
        readyToRun = false;
    }
    else {
        checks.push_back(makeCheck("ports", "pass",
            "HTTP/WS on " + std::to_string(_config.port) +
            ", TURN on " + std::to_string(_config.turnPort)));
    }

    if (_config.mode == Config::Mode::Stream) {
        if (_config.source.empty()) {
            checks.push_back(makeCheck("source", "fail",
                "stream mode requires --source or media.source"));
            readyToRun = false;
        }
        else if (!isRemoteStreamSource(_config.source)) {
            std::ifstream test(_config.source);
            if (test.is_open()) {
                checks.push_back(makeCheck("source", "pass",
                    "Found local media source at " + _config.source));
            }
            else {
                checks.push_back(makeCheck("source", "fail",
                    "Local media source not found: " + _config.source));
                readyToRun = false;
            }
        }
        else {
            checks.push_back(makeCheck("source", "pass",
                "Using remote media source " + _config.source));
        }
    }
    else {
        checks.push_back(makeCheck("source", "pass",
            "No configured media source required for " +
            std::string(Config::modeName(_config.mode)) + " mode"));
    }

    if (_config.enableTurn) {
        if (isWildcardHost(_config.host) && _config.turnExternalIP.empty()) {
            warnings.push_back(
                "TURN externalIp is unset; remote NAT traversal may fail outside local or host-network testing.");
            degraded = true;
            checks.push_back(makeCheck("turn", "warn",
                "TURN is enabled without externalIp while binding to a wildcard host"));
        }
        else {
            checks.push_back(makeCheck("turn", "pass", "TURN relay is enabled"));
        }
    }
    else {
        warnings.push_back(
            "TURN is disabled; direct local/LAN calls may work, but remote NAT traversal will be limited.");
        degraded = true;
        checks.push_back(makeCheck("turn", "warn", "TURN relay disabled"));
    }

    if (_config.mode != Config::Mode::Stream &&
        (_config.vision.enabled || _config.speech.enabled)) {
        warnings.push_back(
            "Intelligence stages are only active in stream mode; current config leaves them configured but inactive.");
        degraded = true;
        checks.push_back(makeCheck("intelligence", "warn",
            "Vision/speech are configured but stream mode is not active"));
    }
    else {
        checks.push_back(makeCheck("intelligence", "pass",
            std::string("vision=") + (_config.vision.enabled ? "on" : "off") +
            ", speech=" + (_config.speech.enabled ? "on" : "off")));
    }

    j["checks"] = std::move(checks);
    j["warnings"] = std::move(warnings);
    j["ready"] = readyToRun;
    j["status"] = readyToRun ? (degraded ? "degraded" : "ok") : "error";
    return j;
}


bool MediaServerApp::ready() const
{
    return doctorJson().value("ready", false);
}


void MediaServerApp::shutdown()
{
    {
        std::lock_guard lock(_sessionMutex);
        _sessions.clear();
    }
    _relay->clear();
    _symple.removeVirtualPeer(_serverPeerId);
    _symple.stop();
    if (_turn)
        _turn->stop();
}


json::Value MediaServerApp::statusJson() const
{
    json::Value j = doctorJson();
    size_t activeSessions = 0;
    size_t totalSessions = 0;
    {
        std::lock_guard lock(_sessionMutex);
        totalSessions = _sessions.size();
        for (const auto& [_, session] : _sessions) {
            if (session && session->active())
                ++activeSessions;
        }
    }

    j["product"] = kProductName;
    j["service"] = kServiceName;
    j["version"] = ICEY_SERVER_VERSION;
    j["mode"] = Config::modeName(_config.mode);
    j["host"] = _config.host;
    j["port"] = _config.port;
    j["peer"]["id"] = _serverPeerId.empty() ? std::string(kServerPeerId) : _serverPeerId;
    j["peer"]["name"] = kServerPeerName;
    j["peer"]["type"] = kServerPeerType;
    j["runtime"]["state"] = "running";
    j["turn"]["enabled"] = _config.enableTurn;
    j["turn"]["port"] = _config.turnPort;
    j["turn"]["externalIp"] = _config.turnExternalIP;
    j["sessions"]["total"] = static_cast<std::uint64_t>(totalSessions);
    j["sessions"]["active"] = static_cast<std::uint64_t>(activeSessions);
    j["stream"]["source"] = _config.source;
    j["stream"]["sourceKind"] = sourceKind(_config.source);
    j["stream"]["loop"] = _config.loop;
    j["record"]["dir"] = _config.recordDir;
    j["intelligence"]["vision"] = _config.vision.enabled;
    j["intelligence"]["speech"] = _config.speech.enabled;
    if (_startedAt != std::chrono::steady_clock::time_point{}) {
        const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - _startedAt).count();
        j["uptimeSec"] = uptime;
    }
    return j;
}


std::shared_ptr<MediaSession> MediaServerApp::ensureSession(const std::string& peerAddress)
{
    {
        std::lock_guard lock(_sessionMutex);
        auto it = _sessions.find(peerAddress);
        if (it != _sessions.end())
            return it->second;
    }

    LInfo("Creating session for ", peerAddress);
    auto session = std::make_shared<MediaSession>(
        peerAddress, _symple, _serverAddress, _config, _relay.get());
    _relay->registerSession(session);
    {
        std::lock_guard lock(_sessionMutex);
        _sessions[peerAddress] = session;
    }
    return session;
}


} // namespace media_server
} // namespace icy
