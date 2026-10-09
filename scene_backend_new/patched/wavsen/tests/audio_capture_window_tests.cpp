import rstd;
import wavsen.audio.capture;
import wavsen.audio.capture_window;

using namespace rstd::prelude;

int main() {
    wavsen::audio::capture::PcmWindowPublisher publisher;
    publisher.restart();
    rstd::array<float, wavsen::audio::kAudioSampleCount> samples {};
    for (rstd::size_t frame = 0; frame < wavsen::audio::kAudioWindowFrames; ++frame) {
        samples[usize(frame * 2)]     = static_cast<float>(frame);
        samples[usize(frame * 2 + 1)] = -static_cast<float>(frame);
    }
    samples[usize(10)] = f32::NAN_.to_primitive();
    publisher.ingest(samples.data(),
                     static_cast<rstd::uint32_t>(wavsen::audio::kAudioWindowFrames - 1),
                     wavsen::audio::kAudioChannels);
    wavsen::audio::AudioPcmWindow window {};
    if (publisher.snapshot(window)) return 1;
    const auto before_capture = rstd::time::Instant::now().duration_since_epoch().as_nanos();
    publisher.ingest(samples.data() + (wavsen::audio::kAudioWindowFrames - 1) * 2,
                     1,
                     wavsen::audio::kAudioChannels);
    if (! publisher.snapshot(window)) return 2;
    const auto after_capture = rstd::time::Instant::now().duration_since_epoch().as_nanos();
    if (u128(window.captured_at_ns) < before_capture || u128(window.captured_at_ns) > after_capture)
        return 13;
    const auto first_timestamp = window.captured_at_ns;
    if (window.generation != 1 || window.sequence != 1 ||
        window.end_sample_frame != wavsen::audio::kAudioWindowFrames)
        return 3;
    if (window.samples[usize(10)].to_primitive() != 0.0f) return 4;

    publisher.restart();
    publisher.ingest(samples.data(),
                     static_cast<rstd::uint32_t>(wavsen::audio::kAudioWindowFrames),
                     wavsen::audio::kAudioChannels);
    if (! publisher.snapshot(window) || window.generation != 2 || window.sequence != 1) return 5;
    if (window.captured_at_ns < first_timestamp) return 14;

    constexpr rstd::size_t                                                     extra_frames = 17;
    rstd::array<float, (wavsen::audio::kAudioWindowFrames + extra_frames) * 2> clean {};
    for (rstd::size_t frame = 0; frame < wavsen::audio::kAudioWindowFrames + extra_frames;
         ++frame) {
        clean[usize(frame * 2)]     = static_cast<float>(frame);
        clean[usize(frame * 2 + 1)] = -static_cast<float>(frame);
    }
    wavsen::audio::capture::PcmWindowPublisher whole;
    whole.restart();
    whole.ingest(clean.data(),
                 static_cast<rstd::uint32_t>(wavsen::audio::kAudioWindowFrames + extra_frames),
                 wavsen::audio::kAudioChannels);
    wavsen::audio::AudioPcmWindow whole_window {};
    if (! whole.snapshot(whole_window)) return 6;

    wavsen::audio::capture::PcmWindowPublisher chunked;
    chunked.restart();
    rstd::size_t consumed = 0;
    while (consumed < wavsen::audio::kAudioWindowFrames + extra_frames) {
        const auto count = rstd::cmp::min(
            rstd::size_t(137), wavsen::audio::kAudioWindowFrames + extra_frames - consumed);
        chunked.ingest(clean.data() + consumed * 2,
                       static_cast<rstd::uint32_t>(count),
                       wavsen::audio::kAudioChannels);
        consumed += count;
    }
    wavsen::audio::AudioPcmWindow chunked_window {};
    if (! chunked.snapshot(chunked_window)) return 7;
    if (whole_window.end_sample_frame != chunked_window.end_sample_frame) return 8;
    for (rstd::size_t index = 0; index < wavsen::audio::kAudioSampleCount; ++index) {
        if (whole_window.samples[usize(index)] != chunked_window.samples[usize(index)]) return 9;
    }
    wavsen::audio::capture::PcmWindowPublisher concurrent;
    concurrent.restart();
    rstd::sync::atomic::Atomic<bool> done { false };
    auto                             writer = rstd::thread::spawn([&] {
        rstd::array<float, wavsen::audio::kAudioSampleCount> values {};
        for (unsigned sequence = 1; sequence <= 256; ++sequence) {
            for (auto& value : values) value = static_cast<float>(sequence);
            concurrent.ingest(values.data(), wavsen::audio::kAudioWindowFrames, 2);
        }
        done.store(true, rstd::sync::atomic::Ordering::Release);
    });
    if (writer.is_err()) return 10;
    while (! done.load(rstd::sync::atomic::Ordering::Acquire)) {
        wavsen::audio::AudioPcmWindow current {};
        if (! concurrent.snapshot(current)) continue;
        for (auto sample : current.samples)
            if (sample.to_primitive() != static_cast<float>(current.sequence)) return 11;
    }
    (void)rstd::move(writer).unwrap().join();
    wavsen::audio::AudioPcmWindow final {};
    if (! concurrent.snapshot(final) || final.sequence != 256) return 12;
    return 0;
}
