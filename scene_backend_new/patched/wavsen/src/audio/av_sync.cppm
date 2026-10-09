export module wavsen.audio:av_sync;

import rstd;
import :byte_stream;
import wavsen.audio.core;

using namespace rstd::prelude;

export namespace wavsen::audio
{

// Error type for AvPlayer::open. Mirrors the small-string Error pattern
// used elsewhere in wavsen (no rich type — just a printable reason).
struct AvPlayerError {
    String message;
};

// Single-stream audio playback aimed at A/V sync. Owns its own
// AudioDevice (independent of any SoundManager) so it can expose a
// stable master clock published by the backend thread.
//
// Threading: state-changing methods are called from the main thread.
// Playback-rate and seek changes are handed to the audio callback before
// touching decoder state. current_time_seconds is lock-free and safe from any thread.
class AvPlayer {
    struct ConstructionKey {};

public:
    static auto open(ByteStream src) -> Result<Box<AvPlayer>, AvPlayerError>;
    static auto open(ByteStream src, bool open_device) -> Result<Box<AvPlayer>, AvPlayerError>;
    static auto open(ByteStream src, bool open_device, AudioClientIdentity identity)
        -> Result<Box<AvPlayer>, AvPlayerError>;

    explicit AvPlayer(ConstructionKey, AudioClientIdentity identity);
    ~AvPlayer();
    AvPlayer(const AvPlayer&)            = delete;
    AvPlayer& operator=(const AvPlayer&) = delete;

    bool open_device();
    void close_device();
    bool is_device_open() const;

    void play();
    void pause();
    bool is_paused() const;

    // Discard queued device audio and seek before the next data callback.
    // The clock re-anchors after the first new PCM batch.
    void seek_to_start();
    void seek_to(f64 seconds);

    // Publish a positive finite media rate for the audio callback.
    auto set_playback_rate(f64 rate) -> bool;
    auto playback_rate() const -> f64;

    // PTS in seconds of the audio sample currently being played by the
    // device. Returns NaN while the device clock cannot advance or before
    // it is primed; the caller should fall back to wall-clock pacing.
    auto current_time_seconds() const -> f64;

    // 0..1 linear gain. Atomic; safe from any thread.
    void set_volume(f32 value);
    void set_muted(bool m);
    auto volume_scale() const -> f32;
    void set_volume_scale(f32 value);
    void set_volume_scale(f32 value, u32 fade_ms);

    // True once the decoder reached EOF *and* the device has had time
    // to drain the last enqueued frames.
    bool is_eof() const;

private:
    class Impl;
    Box<Impl> impl_;
};

} // namespace wavsen::audio
