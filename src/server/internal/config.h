#pragma once


#include <cstdint>
#include <string>
#include <string_view>
#include <vector>


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
        struct NormalizerConfig
        {
            int width = 0;
            int height = 0;
            std::string pixelFmt;
        };

        struct SnapshotConfig
        {
            bool enabled = false;
            std::string dir;
            int64_t minIntervalUsec = 1000000;
        };

        struct ClipConfig
        {
            bool enabled = false;
            std::string dir;
            int64_t preRollUsec = 1000000;
            int64_t postRollUsec = 3000000;
        };

        bool enabled = false;
        uint32_t everyNthFrame = 6;
        int64_t minIntervalUsec = 200000;
        int queueDepth = 8;
        NormalizerConfig normalize;
        uint32_t motionGridWidth = 32;
        uint32_t motionGridHeight = 18;
        uint32_t motionWarmupFrames = 2;
        float motionThreshold = 0.08f;
        int64_t motionCooldownUsec = 500000;
        SnapshotConfig snapshots;
        ClipConfig clips;
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
    std::string authToken;
    std::vector<std::string> allowedOrigins;

    enum class Mode { Stream, Record, Relay };
    Mode mode = Mode::Stream;
    std::string source;
    std::string recordDir = "./recordings";
    bool loop = true;
    // When true, skip the icey-side decode + re-encode for video and forward
    // the input H.264 packets directly to the WebRTC track. Trades intelligence
    // (motion regions, vision overlays) for ~30-100ms of latency. Only honoured
    // when intelligence is fully disabled and the source is browser-compatible
    // H.264.
    bool passthroughVideo = false;

    // "h264" (the codec alias) is the auto-pick sentinel: the codec
    // registry resolves it to the best available H.264 encoder for this
    // platform (h264_videotoolbox on macOS, h264_vaapi / h264_nvenc /
    // h264_qsv on Linux, h264_qsv / h264_nvenc / h264_amf on Windows,
    // libx264 fallback). Lookup is case-insensitive. Set to an explicit
    // ffmpeg encoder name ("libx264", "h264_videotoolbox", ...) to
    // override.
    std::string videoCodec = "h264";
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
    std::string turnUsername;
    std::string turnSecret;
    int turnCredentialTtlSeconds = 3600;
    bool turnAllowLocalRelay = false;

    static Mode parseMode(const std::string& s);
    /// Strict variant: returns false for unknown mode strings instead of
    /// defaulting to Stream, so config typos fail loudly.
    static bool tryParseMode(const std::string& s, Mode& out);
    static const char* modeName(Mode mode);
};


struct ConfigLoadResult
{
    Config config;
    std::string path;
    bool exists = false;
    bool valid = true;
    bool usedDefaults = true;
    /// True when the config file set webRoot explicitly; the packaged
    /// web-root fallback only applies when it did not.
    bool webRootExplicit = false;
    std::string error;
    /// Non-fatal issues (unknown keys, clamped values) surfaced at startup
    /// and in --doctor.
    std::vector<std::string> warnings;
};


ConfigLoadResult loadConfigResult(const std::string& path);
Config loadConfig(const std::string& path);
std::string makeRecordingPath(const std::string& recordDir, std::string_view peerId);


} // namespace media_server
} // namespace icy
