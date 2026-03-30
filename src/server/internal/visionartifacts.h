#pragma once


#include "icy/av/multiplexpacketencoder.h"
#include "icy/av/packet.h"
#include "icy/vision/event.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>


namespace icy {
namespace media_server {


struct VisionArtifactConfig
{
    bool snapshotsEnabled = false;
    std::string snapshotsDir;
    int64_t snapshotMinIntervalUsec = 1000000;

    bool clipsEnabled = false;
    std::string clipsDir;
    int64_t clipPreRollUsec = 1000000;
    int64_t clipPostRollUsec = 3000000;

    int videoFps = 30;
};


struct VisionArtifactResult
{
    int64_t latencyUsec = 0;
    std::string snapshotPath;
    std::string snapshotUrl;
    std::string clipPath;
    std::string clipUrl;
};


struct VisionArtifactStatus
{
    uint64_t sourceFramesSeen = 0;
    double sourceFps = 0.0;
    uint64_t snapshotsWritten = 0;
    uint64_t clipsWritten = 0;
    bool clipActive = false;
    int64_t lastLatencyUsec = 0;
    std::string lastSnapshotPath;
    std::string lastSnapshotUrl;
    std::string lastClipPath;
    std::string lastClipUrl;
};


class VisionArtifacts
{
public:
    explicit VisionArtifacts(std::string sourceLabel,
                             VisionArtifactConfig config);
    ~VisionArtifacts();

    void reset();
    void close();
    void onFrame(const av::PlanarVideoPacket& packet);
    [[nodiscard]] VisionArtifactResult onEvent(const vision::VisionEvent& event);
    [[nodiscard]] VisionArtifactStatus status() const;

    static int64_t steadyNowUsec();

private:
    struct ClipSession;

    static std::string sanitizePathComponent(std::string_view input);
    static std::string artifactUrlFor(const std::string& relativePath);

    std::unique_ptr<av::PlanarVideoPacket> cloneFrame(
        const av::PlanarVideoPacket& packet) const;
    void trimPreRollLocked(int64_t currentFrameTimeUsec);
    void trimArrivalHistoryLocked(int64_t currentSeenUsec);
    [[nodiscard]] int64_t latencyForFrameLocked(int64_t frameTimeUsec) const;
    [[nodiscard]] av::PlanarVideoPacket* bestFrameLocked(int64_t frameTimeUsec) const;
    [[nodiscard]] std::string makeSnapshotPathLocked(int64_t frameTimeUsec) const;
    [[nodiscard]] std::string makeClipPathLocked(int64_t frameTimeUsec) const;
    [[nodiscard]] bool writeSnapshotLocked(const av::PlanarVideoPacket& frame,
                                           const std::string& path) const;
    void startClipLocked(int64_t eventFrameTimeUsec);
    void flushBufferedFramesLocked(int64_t minFrameTimeUsec);
    void pushClipFrameLocked(const av::PlanarVideoPacket& frame);
    void finishClipLocked();

    std::string _sourceLabel;
    VisionArtifactConfig _config;
    mutable std::mutex _mutex;
    std::deque<std::unique_ptr<av::PlanarVideoPacket>> _preRoll;
    std::deque<std::pair<int64_t, int64_t>> _arrivalHistory;
    std::unique_ptr<av::PlanarVideoPacket> _lastFrame;
    std::unique_ptr<ClipSession> _clip;
    uint64_t _sourceFramesSeen = 0;
    int64_t _firstSeenUsec = 0;
    int64_t _lastSeenUsec = 0;
    int64_t _lastSnapshotTimeUsec = 0;
    int64_t _lastLatencyUsec = 0;
    uint64_t _snapshotsWritten = 0;
    uint64_t _clipsWritten = 0;
    std::string _lastSnapshotPath;
    std::string _lastSnapshotUrl;
    std::string _lastClipPath;
    std::string _lastClipUrl;
};


} // namespace media_server
} // namespace icy
