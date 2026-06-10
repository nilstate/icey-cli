#include "config.h"

#include "icy/filesystem.h"
#include "icy/json/json.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>


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

std::vector<std::string> stringArray(const json::Value& value)
{
    std::vector<std::string> result;
    if (!value.is_array())
        return result;
    for (const auto& item : value) {
        if (item.is_string())
            result.push_back(item.get<std::string>());
    }
    return result;
}

void applyEnvironment(Config& config)
{
    if (const char* token = std::getenv("ICEY_AUTH_TOKEN"))
        config.authToken = token;
    if (const char* secret = std::getenv("ICEY_TURN_SECRET"))
        config.turnSecret = secret;
}

std::string resolvePathFromConfig(const std::string& configPath,
                                  const std::string& value,
                                  bool allowRemote = false)
{
    if (value.empty())
        return value;
    if (allowRemote && isRemoteStreamSource(value))
        return value;
    if (value.rfind("/dev/", 0) == 0)
        return value;
    if (std::filesystem::path(value).is_absolute())
        return value;
    return fs::normalize(fs::makePath(fs::dirname(configPath), value));
}

/// Reads an integral port value with range validation; nlohmann's default
/// behaviour silently truncates out-of-range values into uint16_t (70000
/// would become 4464).
uint16_t readPort(const json::Value& obj,
                  const char* key,
                  uint16_t current,
                  const char* what)
{
    if (!obj.contains(key))
        return current;
    const auto value = obj[key].get<int64_t>();
    if (value < 1 || value > 65535) {
        throw std::runtime_error(std::string("invalid ") + what + " " +
                                 std::to_string(value) +
                                 ": expected 1..65535");
    }
    return static_cast<uint16_t>(value);
}

/// Known config keys; anything outside this shape draws a startup warning
/// so typos ("extenalIp") don't silently fall back to defaults.
const json::Value& configSchema()
{
    static const json::Value schema = json::Value::parse(R"({
        "http": {"host": true, "port": true},
        "tls": {"cert": true, "key": true},
        "auth": {"token": true, "allowedOrigins": true},
        "media": {
            "mode": true, "source": true, "recordDir": true, "loop": true,
            "passthroughVideo": true,
            "video": {"codec": true, "width": true, "height": true,
                       "fps": true, "bitrate": true},
            "audio": {"codec": true, "channels": true, "sampleRate": true,
                       "bitrate": true},
            "intelligence": {
                "vision": {
                    "enabled": true, "everyNthFrame": true,
                    "minIntervalUsec": true, "queueDepth": true,
                    "normalize": {"width": true, "height": true,
                                   "pixelFmt": true},
                    "motion": {"gridWidth": true, "gridHeight": true,
                                "warmupFrames": true, "threshold": true,
                                "cooldownUsec": true},
                    "snapshots": {"enabled": true, "dir": true,
                                   "minIntervalUsec": true},
                    "clips": {"enabled": true, "dir": true,
                               "preRollUsec": true, "postRollUsec": true}
                },
                "speech": {"enabled": true, "queueDepth": true,
                            "startThreshold": true, "stopThreshold": true,
                            "minSilenceUsec": true,
                            "updateIntervalUsec": true}
            }
        },
        "turn": {"enabled": true, "port": true, "realm": true,
                  "externalIp": true, "username": true, "secret": true,
                  "credentialTtlSeconds": true, "allowLocalRelay": true},
        "webRoot": true
    })");
    return schema;
}

void collectUnknownKeys(const json::Value& value,
                        const json::Value& schema,
                        const std::string& prefix,
                        std::vector<std::string>& unknown)
{
    if (!value.is_object())
        return;
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (!schema.contains(it.key())) {
            unknown.push_back(prefix + it.key());
        } else if (it->is_object() && schema[it.key()].is_object()) {
            collectUnknownKeys(*it, schema[it.key()],
                               prefix + it.key() + ".", unknown);
        }
    }
}

/// Strips nlohmann's "[json.exception.parse_error.101] " style prefix while
/// keeping the useful line/column detail.
std::string friendlyJsonError(const std::string& what)
{
    if (what.rfind("[json.exception.", 0) == 0) {
        const auto end = what.find("] ");
        if (end != std::string::npos)
            return what.substr(end + 2);
    }
    return what;
}

} // namespace


Config::Mode Config::parseMode(const std::string& s)
{
    if (s == "record")
        return Mode::Record;
    if (s == "relay")
        return Mode::Relay;
    return Mode::Stream;
}


bool Config::tryParseMode(const std::string& s, Mode& out)
{
    if (s == "stream")
        out = Mode::Stream;
    else if (s == "record")
        out = Mode::Record;
    else if (s == "relay")
        out = Mode::Relay;
    else
        return false;
    return true;
}


const char* Config::modeName(Mode mode)
{
    switch (mode) {
    case Mode::Stream:
        return "stream";
    case Mode::Record:
        return "record";
    case Mode::Relay:
        return "relay";
    }
    return "stream";
}


ConfigLoadResult loadConfigResult(const std::string& path)
{
    ConfigLoadResult result;
    result.path = path;

    Config c;
    result.exists = std::filesystem::exists(path);

    std::ifstream file(path);
    if (!result.exists) {
        applyEnvironment(c);
        result.config = std::move(c);
        return result;
    }
    if (!file.is_open()) {
        result.valid = false;
        result.usedDefaults = false;
        result.error = "Cannot open config file";
        applyEnvironment(c);
        result.config = std::move(c);
        return result;
    }

    try {
        json::Value j;
        file >> j;

        collectUnknownKeys(j, configSchema(), "", result.warnings);
        for (auto& warning : result.warnings)
            warning = "unknown config key '" + warning + "' (ignored)";

        if (j.contains("http")) {
            auto& h = j["http"];
            c.host = h.value("host", c.host);
            c.port = readPort(h, "port", c.port, "http.port");
        }
        if (j.contains("tls")) {
            auto& t = j["tls"];
            c.tls.certFile = t.value("cert", c.tls.certFile);
            c.tls.keyFile = t.value("key", c.tls.keyFile);
        }
        if (j.contains("auth")) {
            auto& a = j["auth"];
            c.authToken = a.value("token", c.authToken);
            if (a.contains("allowedOrigins"))
                c.allowedOrigins = stringArray(a["allowedOrigins"]);
        }
        if (j.contains("media")) {
            auto& m = j["media"];
            if (m.contains("mode")) {
                const auto modeStr = m["mode"].get<std::string>();
                if (!Config::tryParseMode(modeStr, c.mode)) {
                    throw std::runtime_error(
                        "invalid media.mode '" + modeStr +
                        "': expected stream, record, or relay");
                }
            }
            c.source = m.value("source", c.source);
            c.recordDir = m.value("recordDir", c.recordDir);
            c.loop = m.value("loop", c.loop);
            c.passthroughVideo = m.value("passthroughVideo", c.passthroughVideo);
            if (m.contains("video")) {
                auto& v = m["video"];
                c.videoCodec = v.value("codec", c.videoCodec);
                c.videoWidth = v.value("width", c.videoWidth);
                c.videoHeight = v.value("height", c.videoHeight);
                c.videoFps = v.value("fps", c.videoFps);
                c.videoBitRate = v.value("bitrate", c.videoBitRate);
            }
            if (m.contains("audio")) {
                auto& a = m["audio"];
                c.audioCodec = a.value("codec", c.audioCodec);
                c.audioChannels = a.value("channels", c.audioChannels);
                c.audioSampleRate = a.value("sampleRate", c.audioSampleRate);
                c.audioBitRate = a.value("bitrate", c.audioBitRate);
            }
            if (m.contains("intelligence")) {
                auto& intelligence = m["intelligence"];
                if (intelligence.contains("vision")) {
                    auto& v = intelligence["vision"];
                    c.vision.enabled = v.value("enabled", c.vision.enabled);
                    c.vision.everyNthFrame = v.value("everyNthFrame", c.vision.everyNthFrame);
                    c.vision.minIntervalUsec = v.value("minIntervalUsec", c.vision.minIntervalUsec);
                    c.vision.queueDepth = v.value("queueDepth", c.vision.queueDepth);
                    if (v.contains("normalize")) {
                        auto& normalize = v["normalize"];
                        c.vision.normalize.width =
                            normalize.value("width", c.vision.normalize.width);
                        c.vision.normalize.height =
                            normalize.value("height", c.vision.normalize.height);
                        c.vision.normalize.pixelFmt =
                            normalize.value("pixelFmt", c.vision.normalize.pixelFmt);
                    }
                    if (v.contains("motion")) {
                        auto& motion = v["motion"];
                        c.vision.motionGridWidth = motion.value("gridWidth", c.vision.motionGridWidth);
                        c.vision.motionGridHeight = motion.value("gridHeight", c.vision.motionGridHeight);
                        c.vision.motionWarmupFrames = motion.value("warmupFrames", c.vision.motionWarmupFrames);
                        c.vision.motionThreshold = motion.value("threshold", c.vision.motionThreshold);
                        c.vision.motionCooldownUsec = motion.value("cooldownUsec", c.vision.motionCooldownUsec);
                    }
                    if (v.contains("snapshots")) {
                        auto& snapshots = v["snapshots"];
                        c.vision.snapshots.enabled =
                            snapshots.value("enabled", c.vision.snapshots.enabled);
                        c.vision.snapshots.dir =
                            snapshots.value("dir", c.vision.snapshots.dir);
                        c.vision.snapshots.minIntervalUsec = snapshots.value(
                            "minIntervalUsec",
                            c.vision.snapshots.minIntervalUsec);
                    }
                    if (v.contains("clips")) {
                        auto& clips = v["clips"];
                        c.vision.clips.enabled =
                            clips.value("enabled", c.vision.clips.enabled);
                        c.vision.clips.dir =
                            clips.value("dir", c.vision.clips.dir);
                        c.vision.clips.preRollUsec = clips.value(
                            "preRollUsec",
                            c.vision.clips.preRollUsec);
                        c.vision.clips.postRollUsec = clips.value(
                            "postRollUsec",
                            c.vision.clips.postRollUsec);
                    }
                }
                if (intelligence.contains("speech")) {
                    auto& s = intelligence["speech"];
                    c.speech.enabled = s.value("enabled", c.speech.enabled);
                    c.speech.queueDepth = s.value("queueDepth", c.speech.queueDepth);
                    c.speech.startThreshold = s.value("startThreshold", c.speech.startThreshold);
                    c.speech.stopThreshold = s.value("stopThreshold", c.speech.stopThreshold);
                    c.speech.minSilenceUsec = s.value("minSilenceUsec", c.speech.minSilenceUsec);
                    c.speech.updateIntervalUsec = s.value("updateIntervalUsec", c.speech.updateIntervalUsec);
                }
            }
        }
        if (j.contains("turn")) {
            auto& t = j["turn"];
            c.enableTurn = t.value("enabled", c.enableTurn);
            c.turnPort = readPort(t, "port", c.turnPort, "turn.port");
            c.turnRealm = t.value("realm", c.turnRealm);
            c.turnExternalIP = t.value("externalIp", c.turnExternalIP);
            c.turnUsername = t.value("username", c.turnUsername);
            c.turnSecret = t.value("secret", c.turnSecret);
            c.turnCredentialTtlSeconds = t.value(
                "credentialTtlSeconds",
                c.turnCredentialTtlSeconds);
            // Time-boxing is the point of the HMAC credential scheme; an
            // absurd TTL (typo'd milliseconds, say) must not produce
            // effectively permanent relay credentials.
            constexpr int kMaxTurnTtlSeconds = 24 * 3600;
            if (c.turnCredentialTtlSeconds > kMaxTurnTtlSeconds) {
                result.warnings.push_back(
                    "turn.credentialTtlSeconds " +
                    std::to_string(c.turnCredentialTtlSeconds) +
                    " clamped to " + std::to_string(kMaxTurnTtlSeconds));
                c.turnCredentialTtlSeconds = kMaxTurnTtlSeconds;
            }
            c.turnAllowLocalRelay = t.value("allowLocalRelay", c.turnAllowLocalRelay);
        }
        if (j.contains("webRoot")) {
            c.webRoot = j["webRoot"].get<std::string>();
            result.webRootExplicit = true;
        }

        c.source = resolvePathFromConfig(path, c.source, true);
        c.recordDir = resolvePathFromConfig(path, c.recordDir);
        if (c.vision.snapshots.dir.empty())
            c.vision.snapshots.dir = fs::makePath(c.recordDir, "snapshots");
        if (c.vision.clips.dir.empty())
            c.vision.clips.dir = fs::makePath(c.recordDir, "clips");
        c.vision.snapshots.dir = resolvePathFromConfig(path, c.vision.snapshots.dir);
        c.vision.clips.dir = resolvePathFromConfig(path, c.vision.clips.dir);
        // Only resolve webRoot against the config dir when the file set it;
        // resolving the built-in default would defeat the packaged
        // share/icey-server/web fallback for installs with a config file.
        if (result.webRootExplicit)
            c.webRoot = resolvePathFromConfig(path, c.webRoot);
        c.tls.certFile = resolvePathFromConfig(path, c.tls.certFile);
        c.tls.keyFile = resolvePathFromConfig(path, c.tls.keyFile);

        result.config = std::move(c);
        result.usedDefaults = false;
    }
    catch (const std::exception& e) {
        result.valid = false;
        result.usedDefaults = false;
        result.error = friendlyJsonError(e.what());
        // Deliberately leave result.config default-constructed: a
        // half-populated config (fields parsed before the failure kept,
        // the rest defaulted) is worse than a clean failure.
        result.config = Config{};
    }

    applyEnvironment(result.config);
    return result;
}


Config loadConfig(const std::string& path)
{
    auto result = loadConfigResult(path);
    if (!result.valid) {
        throw std::runtime_error(
            "Invalid config file '" + path + "': " + result.error);
    }
    return result.config;
}


static std::string sanitizePathComponent(std::string_view input)
{
    std::string result;
    result.reserve(input.size());
    for (unsigned char ch : input) {
        if (std::isalnum(ch) || ch == '-' || ch == '_')
            result.push_back(static_cast<char>(ch));
        else
            result.push_back('_');
    }
    if (result.empty())
        result = "peer";
    return result;
}


std::string makeRecordingPath(const std::string& recordDir,
                              std::string_view peerId)
{
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return fs::makePath(
        recordDir,
        sanitizePathComponent(peerId) + "-" + std::to_string(stamp) + ".mp4");
}


} // namespace media_server
} // namespace icy
