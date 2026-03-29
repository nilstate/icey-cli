#pragma once


#include "icy/http/server.h"
#include "icy/json/json.h"
#include "runtimeinfo.h"

#include <functional>
#include <memory>
#include <string>


namespace icy {
namespace media_server {


class HttpFactory : public http::ServerConnectionFactory
{
public:
    struct RuntimeConfig
    {
        bool enableTurn = true;
        uint16_t turnPort = 3478;
        std::string turnExternalIP;
        std::string host;
        bool enableTls = false;
        std::string product = kProductName;
        std::string service = kServiceName;
        std::string version;
        std::string mode;
        std::function<json::Value()> statusProvider;
    };

    HttpFactory(const std::string& webRoot, RuntimeConfig runtimeConfig);

    std::unique_ptr<http::ServerResponder> createResponder(
        http::ServerConnection& conn) override;

private:
    std::unique_ptr<http::ServerResponder> createApiResponder(
        http::ServerConnection& conn);

    std::string _webRoot;
    RuntimeConfig _runtimeConfig;
};


} // namespace media_server
} // namespace icy
