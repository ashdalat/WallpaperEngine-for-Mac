export module wavsen.audio:file;

import rstd;
import :byte_stream;
import wavsen.audio.core; // DeviceDesc
import :mixer;            // SoundStream (for make_stream factory)

using namespace rstd::prelude;

export namespace wavsen::audio
{

enum class MediaErrorKind
{
    None,
    InvalidInput,
    Open,
    Probe,
    NoAudio,
    Decode,
    Read,
    Seek,
    Resample,
    Cancelled
};
struct MediaError {
    MediaErrorKind kind { MediaErrorKind::None };
    i32            native_code {};
};
struct MediaTag {
    String key;
    String value;
};
struct MediaStreamInfo {
    i64    bitrate {};
    String mime;
};
struct MediaInfo {
    Option<f64>          duration_seconds;
    bool                 seekable {};
    Vec<MediaTag>        tags;
    Vec<MediaStreamInfo> streams;
};

// The cancellation context must outlive the opened media and its decoder.
struct MediaOpenOptions {
    void* context {};
    bool (*cancelled)(void*) noexcept {};
    u32  timeout_ms { 5000 };
    bool reconnect { true };
};

class OpenedMedia {
public:
    OpenedMedia();
    ~OpenedMedia();
    OpenedMedia(OpenedMedia&&) noexcept;
    auto operator=(OpenedMedia&&) noexcept -> OpenedMedia&;
    OpenedMedia(const OpenedMedia&)                    = delete;
    auto operator=(const OpenedMedia&) -> OpenedMedia& = delete;

    static auto open(ref<str> url, MediaOpenOptions options = {})
        -> Result<OpenedMedia, MediaError>;
    static auto open(ByteStream source, MediaOpenOptions options = {})
        -> Result<OpenedMedia, MediaError>;
    auto info() const -> const MediaInfo&;

private:
    friend class StreamDecoder;
    class Impl;
    Box<Impl> impl_;
};

class StreamDecoder {
public:
    StreamDecoder();
    ~StreamDecoder();
    StreamDecoder(const StreamDecoder&)            = delete;
    StreamDecoder& operator=(const StreamDecoder&) = delete;
    StreamDecoder(StreamDecoder&&) noexcept;
    StreamDecoder& operator=(StreamDecoder&&) noexcept;

    // Open the source. Returns false on parser/codec error; details logged
    // via rstd::log.
    auto open(ByteStream src, const DeviceDesc& target) -> bool;
    auto open(OpenedMedia media, const DeviceDesc& target) -> bool;
    auto error() const -> MediaError;
    auto info() const -> const MediaInfo&;

    // Update target descriptor (channels / sample rate). Caller invokes
    // this after the audio device negotiates a different format than what
    // was originally requested.
    void retarget(const DeviceDesc& target);

    // Change the decoded media rate without reopening the source. The
    // resampler runs the source clock faster or slower, so pitch follows
    // playback rate.
    auto set_playback_rate(f64 rate) -> bool;
    auto playback_rate() const -> f64;

    // Pull `frames` interleaved f32 frames into `dst`. Returns frames
    // actually produced. Inspect error() and is_eof() after a short read.
    auto next_pcm(void* dst, u32 frames) -> u64;

    // Seek to a stream-time offset. Re-baselines internal PTS tracking
    // and clears the EOF flag. Returns false if no source is open or
    // av_seek_frame fails.
    auto seek_to(f64 seconds) -> bool;

    // PTS in seconds of the most recently decoded frame, derived from
    // best_effort_timestamp * av_q2d(stream.time_base). Returns 0.0 before
    // the first frame is decoded.
    auto current_pts_seconds() const -> f64;
    auto pcm_position_seconds() const -> f64;

    // True only after decoder and resampler tails have both been consumed.
    auto is_eof() const -> bool;

    // Source stream characteristics. Both return 0 before a successful open.
    auto sample_rate() const -> u32;
    auto channels() const -> u32;

private:
    class Impl;
    Box<Impl> impl_;
};

// Construct a libav*-backed SoundStream from a byte source. Decodes any
// container/codec libavformat understands and resamples to `desc`.
auto make_stream(ByteStream source, const SoundStream::Desc& desc)
    -> Option<Box<dyn<SoundStreamObject>>>;

} // namespace wavsen::audio
