#include "turnserver.h"

#include "icy/base64.h"
#include "icy/crypto/hash.h"
#include "icy/crypto/hmac.h"
#include "icy/logger.h"

#include <charconv>
#include <ctime>
#include <string_view>


namespace icy {
namespace media_server {
namespace {

constexpr const char* kDemoTurnUsername = "icey";
constexpr const char* kDemoTurnCredential = "icey";

bool parseTurnExpiry(std::string_view username,
                     std::time_t now,
                     const std::string& expectedSuffix)
{
    auto expiryPart = username;
    const auto colon = username.find(':');
    if (colon != std::string_view::npos) {
        expiryPart = username.substr(0, colon);
        if (!expectedSuffix.empty() &&
            username.substr(colon + 1) != expectedSuffix) {
            return false;
        }
    }
    else if (!expectedSuffix.empty()) {
        return false;
    }

    std::time_t expiry = 0;
    auto [ptr, ec] = std::from_chars(
        expiryPart.data(),
        expiryPart.data() + expiryPart.size(),
        expiry);
    return ec == std::errc{} &&
           ptr == expiryPart.data() + expiryPart.size() &&
           expiry > now;
}

std::string makeTurnCredential(std::string_view username,
                               std::string_view secret)
{
    return base64::encode(crypto::computeHMAC(username, secret), 0);
}

} // namespace


TurnCredentials makeTurnCredentials(const Config& config)
{
    if (config.turnSecret.empty())
        return {kDemoTurnUsername, kDemoTurnCredential};

    const auto ttl = config.turnCredentialTtlSeconds > 0
        ? config.turnCredentialTtlSeconds
        : 3600;
    std::string username = std::to_string(std::time(nullptr) + ttl);
    if (!config.turnUsername.empty()) {
        username += ":";
        username += config.turnUsername;
    }
    return {username, makeTurnCredential(username, config.turnSecret)};
}


EmbeddedTurn::EmbeddedTurn(const Config& config)
    : _realm(config.turnRealm)
    , _turnUsername(config.turnUsername)
    , _turnSecret(config.turnSecret)
{
    turn::ServerOptions opts;
    opts.software = "icey Media Server TURN [rfc5766]";
    opts.realm = _realm;
    opts.listenAddr = net::Address(config.host, config.turnPort);
    opts.externalIP = config.turnExternalIP;
    opts.enableTCP = true;
    opts.enableUDP = true;
    opts.enableLocalIPPermissions = config.turnAllowLocalRelay;

    _server = std::make_unique<turn::Server>(*this, opts);
}


void EmbeddedTurn::start()
{
    _server->start();
}


void EmbeddedTurn::stop()
{
    _server->stop();
}


turn::AuthenticationState EmbeddedTurn::authenticateRequest(
    turn::Server*, turn::Request& request)
{
    if (request.methodType() == stun::Message::Binding ||
        request.methodType() == stun::Message::SendIndication) {
        return turn::AuthenticationState::Authorized;
    }

    auto usernameAttr = request.get<stun::Username>();
    auto realmAttr = request.get<stun::Realm>();
    auto nonceAttr = request.get<stun::Nonce>();
    auto integrityAttr = request.get<stun::MessageIntegrity>();
    if (!usernameAttr || !realmAttr || !nonceAttr || !integrityAttr) {
        LDebug("TURN auth challenge: method=",
               request.methodString(),
               " user=",
               usernameAttr ? usernameAttr->asString() : "<none>",
               " realm=",
               realmAttr ? realmAttr->asString() : "<none>",
               " nonce=",
               nonceAttr ? "<present>" : "<none>",
               " integrity=",
               integrityAttr ? "<present>" : "<none>");
        return turn::AuthenticationState::NotAuthorized;
    }

    const std::string username = usernameAttr->asString();
    const std::string realm = realmAttr->asString();
    if (realm != _realm) {
        LWarn("TURN auth mismatch: user=", username, " realm=", realm);
        return turn::AuthenticationState::NotAuthorized;
    }

    std::string credential;
    if (_turnSecret.empty()) {
        if (username != kDemoTurnUsername) {
            LWarn("TURN auth mismatch: user=", username, " realm=", realm);
            return turn::AuthenticationState::NotAuthorized;
        }
        credential = kDemoTurnCredential;
    }
    else {
        if (!parseTurnExpiry(username, std::time(nullptr), _turnUsername)) {
            LWarn("TURN auth expired or malformed: user=", username);
            return turn::AuthenticationState::NotAuthorized;
        }
        credential = makeTurnCredential(username, _turnSecret);
    }

    crypto::Hash engine("md5");
    engine.update(username + ":" + realm + ":" + credential);
    request.hash = engine.digestStr();

    const bool ok = integrityAttr->verifyHmac(request.hash);
    if (!ok)
        LWarn("TURN integrity check failed for user=", username);
    return ok ? turn::AuthenticationState::Authorized
              : turn::AuthenticationState::NotAuthorized;
}


void EmbeddedTurn::onServerAllocationCreated(
    turn::Server*, turn::IAllocation*)
{
    LInfo("TURN allocation created");
}


void EmbeddedTurn::onServerAllocationRemoved(
    turn::Server*, turn::IAllocation*)
{
    LDebug("TURN allocation removed");
}


} // namespace media_server
} // namespace icy
