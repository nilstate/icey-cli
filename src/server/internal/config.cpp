#include "config.h"

#include "icy/filesystem.h"
#include "icy/json/json.h"

#include <cctype>
#include <chrono>
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

} // namespace


Config::Mode Config::parseMode(const std::string& s)
{
    if (s == "record")
        return Mode::Record;
    if (s == "relay")
        return Mode::Relay;
    return Mode::Stream;
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
        result.config = std::move(c);
        return result;
    }
    if (!file.is_open()) {
        result.valid = false;
        result.usedDefaults = false;
        result.error = "Cannot open config file";
        result.config = std::move(c);
        return result;
    }

    try {
        json::Value j;
        file >> j;

        if (j.contains("http")) {
            auto& h = j["http"];
            c.host = h.value("host", c.host);
            c.port = h.value("port", c.port);
        }
        if (j.contains("tls")) {
            auto& t = j["tls"];
            c.tls.certFile = t.value("cert", c.tls.certFile);
            c.tls.keyFile = t.value("key", c.tls.keyFile);
        }
        if (j.contains("media")) {
            auto& m = j["media"];
            c.mode = Config::parseMode(m.value("mode", "stream"));
            c.source = m.value("source", c.source);
            c.recordDir = m.value("recordDir", c.recordDir);
            c.loop = m.value("loop", c.loop);
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
            c.turnPort = t.value("port", c.turnPort);
            c.turnRealm = t.value("realm", c.turnRealm);
            c.turnExternalIP = t.value("externalIp", c.turnExternalIP);
        }
        if (j.contains("webRoot"))
            c.webRoot = j["webRoot"].get<std::string>();

        c.source = resolvePathFromConfig(path, c.source, true);
        c.recordDir = resolvePathFromConfig(path, c.recordDir);
        if (c.vision.snapshots.dir.empty())
            c.vision.snapshots.dir = fs::makePath(c.recordDir, "snapshots");
        if (c.vision.clips.dir.empty())
            c.vision.clips.dir = fs::makePath(c.recordDir, "clips");
        c.vision.snapshots.dir = resolvePathFromConfig(path, c.vision.snapshots.dir);
        c.vision.clips.dir = resolvePathFromConfig(path, c.vision.clips.dir);
        c.webRoot = resolvePathFromConfig(path, c.webRoot);
        c.tls.certFile = resolvePathFromConfig(path, c.tls.certFile);
        c.tls.keyFile = resolvePathFromConfig(path, c.tls.keyFile);

        result.config = std::move(c);
        result.usedDefaults = false;
    }
    catch (const std::exception& e) {
        result.valid = false;
        result.usedDefaults = false;
        result.error = e.what();
        result.config = std::move(c);
    }

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
