#include "visionartifacts.h"

#include "icy/filesystem.h"
#include "icy/logger.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>


namespace icy {
namespace media_server {
namespace {

double fpsFromWindow(uint64_t frames, int64_t firstUsec, int64_t lastUsec)
{
    if (frames < 2 || firstUsec <= 0 || lastUsec <= firstUsec)
        return 0.0;

    const auto durationUsec = static_cast<long double>(lastUsec - firstUsec);
    return static_cast<double>(
        (static_cast<long double>(frames - 1) * 1000000.0L) / durationUsec);
}


int64_t eventFrameTimeUsec(const vision::VisionEvent& event)
{
    if (event.frame.ptsUsec > 0)
        return event.frame.ptsUsec;
    return event.emittedAtUsec;
}

} // namespace


struct VisionArtifacts::ClipSession
{
    std::string path;
    std::string url;
    int64_t deadlineUsec = 0;
    bool wroteFrame = false;
    std::unique_ptr<av::MultiplexPacketEncoder> mux;

    void openIfNeeded(const av::PlanarVideoPacket& frame, int videoFps)
    {
        if (mux)
            return;

        av::EncoderOptions options;
        options.ofile = path;
        options.iformat = av::Format("Raw Video", "rawvideo",
            av::VideoCodec("decoded", frame.width, frame.height,
                           videoFps, 0, 0, frame.pixelFmt));
        options.oformat = av::Format("MP4", "mp4",
            av::VideoCodec("H264", "libx264", frame.width, frame.height,
                           videoFps));

        mux = std::make_unique<av::MultiplexPacketEncoder>(options);
        mux->init();
    }

    void encode(const av::PlanarVideoPacket& frame, int videoFps)
    {
        openIfNeeded(frame, videoFps);
        mux->encode(const_cast<av::PlanarVideoPacket&>(frame));
        wroteFrame = true;
    }

    void close()
    {
        if (!mux)
            return;
        mux->flush();
        mux->uninit();
        mux.reset();
    }
};


VisionArtifacts::VisionArtifacts(std::string sourceLabel,
                                 VisionArtifactConfig config)
    : _sourceLabel(std::move(sourceLabel))
    , _config(std::move(config))
{
    if (_config.snapshotsEnabled && !_config.snapshotsDir.empty())
        fs::mkdirr(_config.snapshotsDir);
    if (_config.clipsEnabled && !_config.clipsDir.empty())
        fs::mkdirr(_config.clipsDir);
}


VisionArtifacts::~VisionArtifacts()
{
    close();
}


void VisionArtifacts::reset()
{
    std::lock_guard lock(_mutex);
    finishClipLocked();
    _preRoll.clear();
    _arrivalHistory.clear();
    _lastFrame.reset();
    _sourceFramesSeen = 0;
    _firstSeenUsec = 0;
    _lastSeenUsec = 0;
    _lastSnapshotTimeUsec = 0;
    _lastLatencyUsec = 0;
    _snapshotsWritten = 0;
    _clipsWritten = 0;
    _lastSnapshotPath.clear();
    _lastSnapshotUrl.clear();
    _lastClipPath.clear();
    _lastClipUrl.clear();
}


void VisionArtifacts::close()
{
    std::lock_guard lock(_mutex);
    finishClipLocked();
    _preRoll.clear();
    _arrivalHistory.clear();
    _lastFrame.reset();
}


void VisionArtifacts::onFrame(const av::PlanarVideoPacket& packet)
{
    const int64_t nowUsec = steadyNowUsec();

    std::lock_guard lock(_mutex);
    ++_sourceFramesSeen;
    if (_firstSeenUsec == 0)
        _firstSeenUsec = nowUsec;
    _lastSeenUsec = nowUsec;

    _arrivalHistory.emplace_back(packet.time, nowUsec);
    trimArrivalHistoryLocked(nowUsec);

    _lastFrame = cloneFrame(packet);

    if (_config.clipsEnabled) {
        _preRoll.emplace_back(cloneFrame(packet));
        trimPreRollLocked(packet.time);

        if (_clip) {
            if (packet.time <= _clip->deadlineUsec) {
                pushClipFrameLocked(packet);
            } else {
                finishClipLocked();
            }
        }
    }
}


VisionArtifactResult VisionArtifacts::onEvent(const vision::VisionEvent& event)
{
    std::lock_guard lock(_mutex);
    const int64_t frameTimeUsec = eventFrameTimeUsec(event);

    VisionArtifactResult result;
    result.latencyUsec = latencyForFrameLocked(frameTimeUsec);
    _lastLatencyUsec = result.latencyUsec;

    if (_config.snapshotsEnabled) {
        const int64_t sinceLastSnapshot = frameTimeUsec - _lastSnapshotTimeUsec;
        if (_lastSnapshotTimeUsec == 0 ||
            sinceLastSnapshot >= _config.snapshotMinIntervalUsec) {
            if (auto* frame = bestFrameLocked(frameTimeUsec)) {
                const auto path = makeSnapshotPathLocked(frameTimeUsec);
                if (writeSnapshotLocked(*frame, path)) {
                    result.snapshotPath = path;
                    result.snapshotUrl = artifactUrlFor(
                        fs::makePath("snapshots", fs::filename(path)));
                    _lastSnapshotTimeUsec = frameTimeUsec;
                    _lastSnapshotPath = result.snapshotPath;
                    _lastSnapshotUrl = result.snapshotUrl;
                    ++_snapshotsWritten;
                }
            }
        } else {
            result.snapshotPath = _lastSnapshotPath;
            result.snapshotUrl = _lastSnapshotUrl;
        }
    }

    if (_config.clipsEnabled) {
        if (_clip && frameTimeUsec <= _clip->deadlineUsec) {
            _clip->deadlineUsec = std::max(
                _clip->deadlineUsec,
                frameTimeUsec + _config.clipPostRollUsec);
        } else {
            finishClipLocked();
            startClipLocked(frameTimeUsec);
            flushBufferedFramesLocked(frameTimeUsec - _config.clipPreRollUsec);
        }

        if (_clip) {
            result.clipPath = _clip->path;
            result.clipUrl = _clip->url;
        }
    }

    return result;
}


VisionArtifactStatus VisionArtifacts::status() const
{
    std::lock_guard lock(_mutex);
    return {
        .sourceFramesSeen = _sourceFramesSeen,
        .sourceFps = fpsFromWindow(_sourceFramesSeen, _firstSeenUsec, _lastSeenUsec),
        .snapshotsWritten = _snapshotsWritten,
        .clipsWritten = _clipsWritten,
        .clipActive = static_cast<bool>(_clip),
        .lastLatencyUsec = _lastLatencyUsec,
        .lastSnapshotPath = _lastSnapshotPath,
        .lastSnapshotUrl = _lastSnapshotUrl,
        .lastClipPath = _lastClipPath,
        .lastClipUrl = _lastClipUrl,
    };
}


int64_t VisionArtifacts::steadyNowUsec()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}


std::string VisionArtifacts::sanitizePathComponent(std::string_view input)
{
    std::string result;
    result.reserve(input.size());
    for (unsigned char ch : input) {
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_')
            result.push_back(static_cast<char>(ch));
        else
            result.push_back('_');
    }
    if (result.empty())
        result = "artifact";
    return result;
}


std::string VisionArtifacts::artifactUrlFor(const std::string& relativePath)
{
    std::string url = relativePath;
    for (char& ch : url) {
        if (ch == '\\')
            ch = '/';
    }
    return "/artifacts/" + url;
}


std::unique_ptr<av::PlanarVideoPacket>
VisionArtifacts::cloneFrame(const av::PlanarVideoPacket& packet) const
{
    return std::unique_ptr<av::PlanarVideoPacket>(
        static_cast<av::PlanarVideoPacket*>(packet.clone().release()));
}


void VisionArtifacts::trimPreRollLocked(int64_t currentFrameTimeUsec)
{
    if (!_config.clipsEnabled || _config.clipPreRollUsec <= 0)
        return;

    const int64_t keepAfter = currentFrameTimeUsec - _config.clipPreRollUsec;
    while (!_preRoll.empty() && _preRoll.front()->time < keepAfter)
        _preRoll.pop_front();
}


void VisionArtifacts::trimArrivalHistoryLocked(int64_t currentSeenUsec)
{
    static constexpr int64_t kRetentionUsec = 10000000;
    const int64_t keepAfter = currentSeenUsec - kRetentionUsec;
    while (!_arrivalHistory.empty() && _arrivalHistory.front().second < keepAfter)
        _arrivalHistory.pop_front();
}


int64_t VisionArtifacts::latencyForFrameLocked(int64_t frameTimeUsec) const
{
    if (_arrivalHistory.empty())
        return 0;

    const auto nowUsec = steadyNowUsec();
    int64_t bestDelta = std::numeric_limits<int64_t>::max();
    int64_t bestSeenUsec = 0;
    for (const auto& [seenFrameTimeUsec, seenAtUsec] : _arrivalHistory) {
        const int64_t delta = std::llabs(seenFrameTimeUsec - frameTimeUsec);
        if (delta < bestDelta) {
            bestDelta = delta;
            bestSeenUsec = seenAtUsec;
        }
    }

    if (bestSeenUsec == 0)
        return 0;
    return std::max<int64_t>(0, nowUsec - bestSeenUsec);
}


av::PlanarVideoPacket* VisionArtifacts::bestFrameLocked(int64_t frameTimeUsec) const
{
    if (!_preRoll.empty()) {
        av::PlanarVideoPacket* best = nullptr;
        int64_t bestDelta = std::numeric_limits<int64_t>::max();
        for (const auto& frame : _preRoll) {
            const int64_t delta = std::llabs(frame->time - frameTimeUsec);
            if (delta < bestDelta) {
                best = frame.get();
                bestDelta = delta;
            }
        }
        if (best)
            return best;
    }

    return _lastFrame.get();
}


std::string VisionArtifacts::makeSnapshotPathLocked(int64_t frameTimeUsec) const
{
    return fs::makePath(
        _config.snapshotsDir,
        sanitizePathComponent(_sourceLabel) + "-" + std::to_string(frameTimeUsec) + ".png");
}


std::string VisionArtifacts::makeClipPathLocked(int64_t frameTimeUsec) const
{
    return fs::makePath(
        _config.clipsDir,
        sanitizePathComponent(_sourceLabel) + "-" + std::to_string(frameTimeUsec) + ".mp4");
}


bool VisionArtifacts::writeSnapshotLocked(const av::PlanarVideoPacket& frame,
                                          const std::string& path) const
{
    try {
        fs::mkdirr(fs::dirname(path));

        av::EncoderOptions options;
        options.ofile = path;
        options.iformat = av::Format("Raw Video", "rawvideo",
            av::VideoCodec("decoded", frame.width, frame.height, 1.0, 0, 0,
                           frame.pixelFmt));
        options.oformat = av::Format("PNG", "image2",
            av::VideoCodec("PNG", "png", frame.width, frame.height, 1.0));

        av::MultiplexPacketEncoder encoder(options);
        encoder.init();
        encoder.encode(const_cast<av::PlanarVideoPacket&>(frame));
        encoder.flush();
        encoder.uninit();

        return fs::exists(path) && fs::filesize(path) > 0;
    }
    catch (const std::exception& exc) {
        LError("VisionArtifacts snapshot failed: ", exc.what());
        if (std::filesystem::exists(path))
            std::filesystem::remove(path);
        return false;
    }
}


void VisionArtifacts::startClipLocked(int64_t eventFrameTimeUsec)
{
    fs::mkdirr(_config.clipsDir);

    auto clip = std::make_unique<ClipSession>();
    clip->path = makeClipPathLocked(eventFrameTimeUsec);
    clip->url = artifactUrlFor(fs::makePath("clips", fs::filename(clip->path)));
    clip->deadlineUsec = eventFrameTimeUsec + _config.clipPostRollUsec;
    _lastClipPath = clip->path;
    _lastClipUrl = clip->url;
    _clip = std::move(clip);
}


void VisionArtifacts::flushBufferedFramesLocked(int64_t minFrameTimeUsec)
{
    if (!_clip)
        return;

    for (const auto& frame : _preRoll) {
        if (frame->time >= minFrameTimeUsec)
            _clip->encode(*frame, _config.videoFps);
    }
}


void VisionArtifacts::pushClipFrameLocked(const av::PlanarVideoPacket& frame)
{
    if (_clip)
        _clip->encode(frame, _config.videoFps);
}


void VisionArtifacts::finishClipLocked()
{
    if (!_clip)
        return;

    try {
        _clip->close();
        if (_clip->wroteFrame &&
            fs::exists(_clip->path) &&
            fs::filesize(_clip->path) > 0) {
            ++_clipsWritten;
        } else if (std::filesystem::exists(_clip->path)) {
            std::filesystem::remove(_clip->path);
        }
    }
    catch (const std::exception& exc) {
        LError("VisionArtifacts clip finalize failed: ", exc.what());
    }

    _clip.reset();
}


} // namespace media_server
} // namespace icy
