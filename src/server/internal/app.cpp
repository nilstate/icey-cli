#include "app.h"

#include "config.h"
#include "httpfactory.h"
#include "media.h"
#include "turnserver.h"

#include "icy/filesystem.h"
#include "icy/logger.h"

#include <fstream>
#include <iostream>


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

} // namespace


MediaServerApp::MediaServerApp(const Config& config)
    : _config(config)
    , _relay(std::make_unique<RelayController>())
{
}


MediaServerApp::~MediaServerApp() = default;


bool MediaServerApp::start()
{
    if (!fs::exists(fs::makePath(_config.webRoot, "index.html"))) {
        std::cerr << "Error: web UI not found under " << _config.webRoot << '\n';
        return false;
    }

    if (_config.mode == Config::Mode::Stream && _config.source.empty()) {
        std::cerr << "Error: stream mode requires --source or media.source in config.json\n";
        return false;
    }

    if (_config.mode == Config::Mode::Stream &&
        !_config.source.empty() &&
        !isRemoteStreamSource(_config.source)) {
        std::ifstream test(_config.source);
        if (!test.is_open()) {
            std::cerr << "Error: source file not found: " << _config.source << '\n';
            return false;
        }
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
                          ICEY_SERVER_VERSION,
                          Config::modeName(_config.mode),
                          [this]() { return statusJson(); }
                      }));

    _serverPeerId = "media-server";
    _serverAddress = "icey|" + _serverPeerId;

    smpl::Peer vpeer;
    vpeer.setID(_serverPeerId);
    vpeer.setUser("icey");
    vpeer.setName("icey");
    vpeer.setType("media-server");
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
                auto found = _sessions.find(peerAddress);
                if (found != _sessions.end())
                    session = found->second;
            }
            if (session)
                session->onSignallingMessage(msg);
        });

    std::cout << "Media server listening on "
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


void MediaServerApp::shutdown()
{
    _sessions.clear();
    _relay->clear();
    _symple.removeVirtualPeer(_serverPeerId);
    _symple.stop();
    if (_turn)
        _turn->stop();
}


json::Value MediaServerApp::statusJson() const
{
    size_t activeSessions = 0;
    for (const auto& [_, session] : _sessions) {
        if (session && session->active())
            ++activeSessions;
    }

    json::Value j;
    j["status"] = "ok";
    j["service"] = "icey-server";
    j["version"] = ICEY_SERVER_VERSION;
    j["mode"] = Config::modeName(_config.mode);
    j["host"] = _config.host;
    j["port"] = _config.port;
    j["turn"]["enabled"] = _config.enableTurn;
    j["turn"]["port"] = _config.turnPort;
    j["sessions"]["total"] = static_cast<std::uint64_t>(_sessions.size());
    j["sessions"]["active"] = static_cast<std::uint64_t>(activeSessions);
    j["stream"]["source"] = _config.source;
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
    auto it = _sessions.find(peerAddress);
    if (it != _sessions.end())
        return it->second;

    LInfo("Creating session for ", peerAddress);
    auto session = std::make_shared<MediaSession>(
        peerAddress, _symple, _serverAddress, _config, _relay.get());
    _relay->registerSession(session);
    _sessions[peerAddress] = session;
    return session;
}


} // namespace media_server
} // namespace icy
