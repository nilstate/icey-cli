#include "httpfactory.h"

#include "icy/json/json.h"
#include "turnserver.h"

#include <openssl/crypto.h>

#include <algorithm>
#include <fstream>
#include <string_view>


namespace icy {
namespace media_server {
namespace {

bool constantTimeEqual(std::string_view a, std::string_view b)
{
    return a.size() == b.size() &&
           CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

bool authorizedRequest(const http::Request& request,
                       const std::string& token)
{
    if (token.empty())
        return true;

    const std::string auth = request.get("Authorization", std::string());
    constexpr std::string_view prefix = "Bearer ";
    if (auth.size() > prefix.size() &&
        std::equal(prefix.begin(), prefix.end(), auth.begin()) &&
        constantTimeEqual(std::string_view(auth).substr(prefix.size()), token)) {
        return true;
    }

    const std::string headerToken = request.get("X-Icey-Token", std::string());
    if (!headerToken.empty() && constantTimeEqual(headerToken, token))
        return true;

    // Plain links (artifact downloads in the web UI) cannot carry headers,
    // so a `token` query parameter is accepted as a last resort.
    NVCollection params;
    request.getURIParameters(params);
    const std::string queryToken = params.get("token", std::string());
    return !queryToken.empty() && constantTimeEqual(queryToken, token);
}

std::string stripUserInfo(std::string value)
{
    const auto scheme = value.find("://");
    if (scheme == std::string::npos)
        return value;

    const auto authority = scheme + 3;
    const auto slash = value.find('/', authority);
    const auto at = value.find('@', authority);
    if (at != std::string::npos &&
        (slash == std::string::npos || at < slash)) {
        value.erase(authority, at - authority + 1);
    }
    return value;
}

std::string publicPath(std::string value)
{
    value = stripUserInfo(std::move(value));
    if (value.find("://") != std::string::npos)
        return value;

    const auto pos = value.find_last_of("/\\");
    if (pos == std::string::npos)
        return value;
    return value.substr(pos + 1);
}

void scrubStatus(json::Value& j)
{
    if (j.contains("source") && j["source"].contains("value"))
        j["source"]["value"] = publicPath(j["source"]["value"].get<std::string>());
    if (j.contains("stream") && j["stream"].contains("source"))
        j["stream"]["source"] = publicPath(j["stream"]["source"].get<std::string>());
    if (j.contains("record") && j["record"].contains("dir"))
        j["record"]["dir"] = publicPath(j["record"]["dir"].get<std::string>());
    if (j.contains("web") && j["web"].contains("root"))
        j["web"]["root"] = publicPath(j["web"]["root"].get<std::string>());
    if (j.contains("config") && j["config"].contains("path"))
        j["config"]["path"] = publicPath(j["config"]["path"].get<std::string>());
    if (j.contains("tls") && j["tls"].contains("certFile"))
        j["tls"]["certFile"] = publicPath(j["tls"]["certFile"].get<std::string>());
    if (j.contains("checks") && j["checks"].is_array()) {
        for (auto& check : j["checks"]) {
            if (check.is_object() &&
                check.contains("detail") &&
                check["detail"].is_string()) {
                check["detail"] = publicPath(check["detail"].get<std::string>());
            }
        }
    }
    if (j.contains("intelligence")) {
        auto& intelligence = j["intelligence"];
        if (intelligence.contains("vision")) {
            auto& vision = intelligence["vision"];
            if (vision.contains("snapshotDir"))
                vision["snapshotDir"] = publicPath(vision["snapshotDir"].get<std::string>());
            if (vision.contains("clipDir"))
                vision["clipDir"] = publicPath(vision["clipDir"].get<std::string>());
        }
    }
}

TurnCredentials runtimeTurnCredentials(const HttpFactory::RuntimeConfig& runtime)
{
    Config config;
    config.turnUsername = runtime.turnUsername;
    config.turnSecret = runtime.turnSecret;
    config.turnCredentialTtlSeconds = runtime.turnCredentialTtlSeconds;
    return makeTurnCredentials(config);
}


class StaticFileResponder : public http::ServerResponder
{
public:
    StaticFileResponder(http::ServerConnection& conn,
                        const std::string& webRoot,
                        const std::string& artifactRoot,
                        const std::string& authToken)
        : http::ServerResponder(conn)
        , _webRoot(webRoot)
        , _artifactRoot(artifactRoot)
        , _authToken(authToken)
    {
    }

    void onRequest(http::Request& request, http::Response& response) override
    {
        std::string path = request.getURI();

        // Route and resolve files on the path only; tokens and cache
        // busters arrive as query parameters.
        const auto cut = path.find_first_of("?#");
        if (cut != std::string::npos)
            path.resize(cut);

        if (path == "/" || path.empty())
            path = "/index.html";

        if (path.find("..") != std::string::npos) {
            response.setStatus(http::StatusCode::Forbidden);
            connection().sendHeader();
            return;
        }

        std::string basePath = _webRoot;
        std::string localPath = path;
        if (path.rfind("/artifacts/", 0) == 0) {
            // Recordings, snapshots, and motion clips are operator data;
            // they get the same protection as the API.
            if (!authorizedRequest(request, _authToken)) {
                response.setStatus(http::StatusCode::Unauthorized);
                connection().sendHeader();
                return;
            }
            basePath = _artifactRoot;
            localPath = path.substr(std::string("/artifacts").size());
        }

        std::string filePath = basePath + localPath;
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            response.setStatus(http::StatusCode::NotFound);
            connection().sendHeader();
            return;
        }

        auto size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::string body(static_cast<size_t>(size), '\0');
        file.read(body.data(), size);

        response.setContentType(mimeType(path));
        response.setContentLength(body.size());
        connection().sendHeader();
        connection().send(body.data(), body.size());
    }

private:
    static std::string mimeType(const std::string& path)
    {
        auto dot = path.rfind('.');
        if (dot == std::string::npos)
            return "application/octet-stream";
        auto ext = path.substr(dot + 1);
        if (ext == "html") return "text/html; charset=utf-8";
        if (ext == "js") return "application/javascript; charset=utf-8";
        if (ext == "css") return "text/css; charset=utf-8";
        if (ext == "json") return "application/json";
        if (ext == "png") return "image/png";
        if (ext == "svg") return "image/svg+xml";
        if (ext == "ico") return "image/x-icon";
        if (ext == "woff2") return "font/woff2";
        if (ext == "woff") return "font/woff";
        return "application/octet-stream";
    }

    std::string _webRoot;
    std::string _artifactRoot;
    std::string _authToken;
};

} // namespace


HttpFactory::HttpFactory(const std::string& webRoot, RuntimeConfig runtimeConfig)
    : _webRoot(webRoot)
    , _runtimeConfig(std::move(runtimeConfig))
{
}


std::unique_ptr<http::ServerResponder> HttpFactory::createResponder(
    http::ServerConnection& conn)
{
    auto& uri = conn.request().getURI();

    if (uri.substr(0, 5) == "/api/")
        return createApiResponder(conn);

    return std::make_unique<StaticFileResponder>(
        conn, _webRoot, _runtimeConfig.artifactRoot, _runtimeConfig.authToken);
}


std::unique_ptr<http::ServerResponder> HttpFactory::createApiResponder(
    http::ServerConnection& conn)
{
    class ApiResponder : public http::ServerResponder
    {
    public:
        ApiResponder(http::ServerConnection& conn,
                     RuntimeConfig runtimeConfig)
            : http::ServerResponder(conn)
            , _runtimeConfig(std::move(runtimeConfig))
        {
        }

        void onRequest(http::Request& request, http::Response& response) override
        {
            json::Value j;
            if (request.getURI() != "/api/health" &&
                !authorizedRequest(request, _runtimeConfig.authToken)) {
                response.setStatus(http::StatusCode::Unauthorized);
                j["status"] = "error";
                j["error"] = "unauthorized";
                sendJson(response, j);
                return;
            }

            if (request.getURI() == "/api/config") {
                j["status"] = "ok";
                j["product"] = _runtimeConfig.product;
                j["service"] = _runtimeConfig.service;
                j["version"] = _runtimeConfig.version;
                j["mode"] = _runtimeConfig.mode;
                j["source"]["value"] = publicPath(_runtimeConfig.source);
                j["source"]["kind"] = _runtimeConfig.sourceKind;
                j["source"]["remote"] = _runtimeConfig.sourceRemote;
                j["http"]["host"] = _runtimeConfig.host;
                j["http"]["scheme"] = _runtimeConfig.enableTls ? "https" : "http";
                j["http"]["tls"] = _runtimeConfig.enableTls;
                j["peer"]["id"] = kServerPeerId;
                j["peer"]["name"] = kServerPeerName;
                j["peer"]["type"] = kServerPeerType;
                j["turn"]["enabled"] = _runtimeConfig.enableTurn;
                j["turn"]["host"] = _runtimeConfig.turnExternalIP.empty()
                    ? _runtimeConfig.host
                    : _runtimeConfig.turnExternalIP;
                j["turn"]["port"] = _runtimeConfig.turnPort;
                auto creds = runtimeTurnCredentials(_runtimeConfig);
                j["turn"]["username"] = creds.username;
                j["turn"]["credential"] = creds.credential;
                j["stun"]["urls"] = json::Value::array({
                    "stun:stun.l.google.com:19302"
                });
            }
            else if (request.getURI() == "/api/health") {
                j["status"] = "ok";
                j["product"] = _runtimeConfig.product;
                j["service"] = _runtimeConfig.service;
                j["version"] = _runtimeConfig.version;
            }
            else if (request.getURI() == "/api/ready") {
                if (_runtimeConfig.statusProvider)
                    j = _runtimeConfig.statusProvider();
                else
                    j["ready"] = false;
                scrubStatus(j);
                if (!j.value("ready", false))
                    response.setStatus(http::StatusCode::Unavailable);
            }
            else if (request.getURI() == "/api/status") {
                if (_runtimeConfig.statusProvider)
                    j = _runtimeConfig.statusProvider();
                else
                    j["status"] = "ok";
                scrubStatus(j);
            }
            else {
                response.setStatus(http::StatusCode::NotFound);
                j["status"] = "error";
                j["error"] = "not_found";
            }
            sendJson(response, j);
        }

    private:
        void sendJson(http::Response& response, const json::Value& j)
        {
            auto body = j.dump();
            response.setContentType("application/json");
            response.setContentLength(body.size());
            connection().sendHeader();
            connection().send(body.data(), body.size());
        }

        RuntimeConfig _runtimeConfig;
    };

    return std::make_unique<ApiResponder>(conn, _runtimeConfig);
}


} // namespace media_server
} // namespace icy
