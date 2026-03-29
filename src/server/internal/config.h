#pragma once


#include <cstdint>
#include <string>
#include <string_view>


namespace icy {
namespace media_server {


struct Config
{
    struct TlsConfig
    {
        std::string certFile;
        std::string keyFile;

        [[nodiscard]] bool configured() const
        {
            return !certFile.empty() || !keyFile.empty();
        }

        [[nodiscard]] bool enabled() const
        {
            return !certFile.empty() && !keyFile.empty();
        }
    };

    struct VisionConfig
    {
        bool enabled = false;
        uint32_t everyNthFrame = 6;
        int64_t minIntervalUsec = 200000;
        int queueDepth = 8;
        uint32_t motionGridWidth = 32;
        uint32_t motionGridHeight = 18;
        uint32_t motionWarmupFrames = 2;
        float motionThreshold = 0.08f;
        int64_t motionCooldownUsec = 500000;
    };

    struct SpeechConfig
    {
        bool enabled = false;
        int queueDepth = 32;
        float startThreshold = 0.045f;
        float stopThreshold = 0.020f;
        int64_t minSilenceUsec = 250000;
        int64_t updateIntervalUsec = 250000;
    };

    std::string host = "0.0.0.0";
    uint16_t port = 4500;
    std::string webRoot = "./web/dist";
    TlsConfig tls;

    enum class Mode { Stream, Record, Relay };
    Mode mode = Mode::Stream;
    std::string source;
    std::string recordDir = "./recordings";
    bool loop = true;

    std::string videoCodec = "libx264";
    int videoWidth = 1280;
    int videoHeight = 720;
    int videoFps = 30;
    int videoBitRate = 2000000;

    std::string audioCodec = "libopus";
    int audioChannels = 2;
    int audioSampleRate = 48000;
    int audioBitRate = 128000;
    VisionConfig vision;
    SpeechConfig speech;

    bool enableTurn = true;
    uint16_t turnPort = 3478;
    std::string turnRealm = "0state.com";
    std::string turnExternalIP;

    static Mode parseMode(const std::string& s);
    static const char* modeName(Mode mode);
};


struct ConfigLoadResult
{
    Config config;
    std::string path;
    bool exists = false;
    bool valid = true;
    bool usedDefaults = true;
    std::string error;
};


ConfigLoadResult loadConfigResult(const std::string& path);
Config loadConfig(const std::string& path);
std::string makeRecordingPath(const std::string& recordDir, std::string_view peerId);


} // namespace media_server
} // namespace icy
