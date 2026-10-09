export module wavsen.audio.capture_window;

import rstd;
import wavsen.audio.capture;

using namespace rstd::prelude;
using rstd::time::Instant;

export namespace wavsen::audio::capture
{

class PcmWindowPublisher {
public:
    void restart() {
        generation_ += 1;
        sequence_         = 0;
        end_sample_frame_ = 0;
        head_             = 0;
        filled_           = 0;
        for (auto& sample : ring_) sample = f32();
        windows_[write_slot_].clear();
        publish_slot();
    }

    void ingest(const float* source, rstd::uint32_t frame_count, rstd::uint32_t channels) {
        if (! source || channels == 0) return;
        for (rstd::uint32_t frame = 0; frame < frame_count; ++frame) {
            const auto  source_offset     = frame * channels;
            const auto  destination       = head_ * kAudioChannels;
            const float left              = finite(source[source_offset]);
            const float right             = channels > 1 ? finite(source[source_offset + 1]) : left;
            ring_[usize(destination)]     = f32(left);
            ring_[usize(destination + 1)] = f32(right);
            head_                         = (head_ + 1) % kAudioWindowFrames;
            filled_                       = rstd::cmp::min(filled_ + 1, kAudioWindowFrames);
            end_sample_frame_ += 1;
        }
        if (filled_ == kAudioWindowFrames && frame_count > 0) publish();
    }

    bool snapshot(AudioPcmWindow& out) const {
        if (published_slot_.load(rstd::sync::atomic::Ordering::Acquire) & 4u) {
            read_slot_ =
                published_slot_.exchange(read_slot_, rstd::sync::atomic::Ordering::AcqRel) & 3u;
        }
        out = windows_[read_slot_];
        return out.frames == kAudioWindowFrames;
    }

private:
    static float finite(float sample) { return f32(sample).is_finite() ? sample : 0.0f; }

    void publish() {
        AudioPcmWindow window {};
        window.generation = generation_;
        window.sequence   = ++sequence_;
        window.captured_at_ns =
            rstd::try_from<u64>(Instant::now().duration_since_epoch().as_nanos())
                .unwrap()
                .to_primitive();
        window.end_sample_frame = end_sample_frame_;
        window.sample_rate_hz   = kAudioSampleRate;
        window.channels         = kAudioChannels;
        window.frames           = static_cast<rstd::uint32_t>(kAudioWindowFrames);
        for (rstd::size_t frame = 0; frame < kAudioWindowFrames; ++frame) {
            const auto source      = ((head_ + frame) % kAudioWindowFrames) * kAudioChannels;
            const auto destination = frame * kAudioChannels;
            window.samples[usize(destination)]     = ring_[usize(source)];
            window.samples[usize(destination + 1)] = ring_[usize(source + 1)];
        }
        windows_[write_slot_] = window;
        publish_slot();
    }

    void publish_slot() {
        // One producer and one snapshot reader exchange exclusive slots; a seqlock over
        // memcpy would still race on non-atomic PCM samples even if the read were retried.
        write_slot_ =
            published_slot_.exchange(write_slot_ | 4u, rstd::sync::atomic::Ordering::AcqRel) & 3u;
    }

    rstd::array<f32, kAudioSampleCount>                ring_ {};
    rstd::size_t                                       head_             = 0;
    rstd::size_t                                       filled_           = 0;
    rstd::uint64_t                                     generation_       = 0;
    rstd::uint64_t                                     sequence_         = 0;
    rstd::uint64_t                                     end_sample_frame_ = 0;
    mutable rstd::sync::atomic::Atomic<rstd::uint32_t> published_slot_ { 1 };
    rstd::uint32_t                                     write_slot_ { 0 };
    mutable rstd::uint32_t                             read_slot_ { 2 };
    AudioPcmWindow                                     windows_[3] {};
};

} // namespace wavsen::audio::capture
