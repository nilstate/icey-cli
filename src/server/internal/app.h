#pragma once


#include "config.h"
#include "icy/symple/server.h"
#include "icy/json/json.h"

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>


namespace icy {
namespace media_server {


class EmbeddedTurn;
class MediaSession;
class RelayController;


class MediaServerApp
{
public:
    explicit MediaServerApp(const Config& config);
    ~MediaServerApp();

    bool start();
    void shutdown();
    json::Value statusJson() const;

private:
    std::shared_ptr<MediaSession> ensureSession(const std::string& peerAddress);

    Config _config;
    smpl::Server _symple;
    std::unique_ptr<EmbeddedTurn> _turn;
    std::string _serverPeerId;
    std::string _serverAddress;
    std::unique_ptr<RelayController> _relay;
    std::unordered_map<std::string, std::shared_ptr<MediaSession>> _sessions;
    std::chrono::steady_clock::time_point _startedAt{};
};


} // namespace media_server
} // namespace icy
