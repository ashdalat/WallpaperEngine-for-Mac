import rstd;
import wavsen.audio.core;
import wavsen.audio.capture;

using namespace rstd::prelude;
using namespace wavsen::audio;

void pause_test() { rstd::thread::sleep(rstd::time::Duration::from_millis(u64(20))); }

class SilentChannel : public IPullChannel {
public:
    auto next_pcm(void* output, u32 frames) -> u64 override {
        rstd::mem::memset(output, u8(), usize(frames.to_primitive()) * usize(2 * sizeof(float)));
        return u64(frames.to_primitive());
    }
    void pass_desc(const DeviceDesc&) override {}
};

auto wait_for(AudioDevice& device, AudioDeviceState expected) -> bool {
    for (int i = 0; i < 150; ++i) {
        if (device.state() == expected) return true;
        if (device.state() == AudioDeviceState::Failed) return false;
        pause_test();
    }
    return false;
}

int main(int argc, char**) {
    rstd::sync::atomic::Atomic<u64> generation { u64() };
    AudioDevice                     device;
    device.set_event_sink(Some(AudioDeviceEventSink::make([&](AudioDeviceEvent event) {
        generation.store(event.generation, rstd::sync::atomic::Ordering::Release);
    })));
    AudioDeviceDesiredState desired;
    desired.generation = u64(1);
    auto channel       = Box<SilentChannel>::make();
    if (! device.mount(Box<dyn<PullChannelObject>>::from_raw(
                           dyn<PullChannelObject>::from_ptr(rstd::move(channel).into_raw())),
                       u64(1)))
        return 1;
    if (! device.apply(desired.clone())) return 2;
    for (int i = 0; i < 150 && generation.load(rstd::sync::atomic::Ordering::Acquire) != u64(1);
         ++i)
        pause_test();
    if (generation.load(rstd::sync::atomic::Ordering::Acquire) != u64(1)) return 3;
    if (device.state() != AudioDeviceState::Idle) return 4;

    if (argc > 1) {
        desired.active  = true;
        desired.playing = true;
        if (! device.apply(desired.clone()) || ! wait_for(device, AudioDeviceState::ReadyPlaying))
            return 10;
        for (int i = 0; i < 150 && device.stream_position_frames() == u64(); ++i) pause_test();
        if (device.stream_position_frames() == u64()) return 11;
        const auto position = device.stream_position_frames();
        pause_test();
        if (device.stream_position_frames() < position) return 12;
        desired.playing = false;
        if (! device.apply(desired.clone()) || ! wait_for(device, AudioDeviceState::ReadyPaused))
            return 13;
        desired.playback_buffer_revision = u64(1);
        desired.playing                  = true;
        if (! device.apply(desired.clone()) || ! wait_for(device, AudioDeviceState::ReadyPlaying))
            return 14;

        AudioCapture capture;
        if (! capture.init()) return 20;
        AudioPcmWindow window {};
        bool           received = false;
        for (int i = 0; i < 150 && ! received; ++i) {
            pause_test();
            received = capture.snapshot(window);
        }
        capture.uninit();
        if (capture.is_inited()) return 21;
        if (! received || window.channels != 2 || window.sample_rate_hz != 48000) return 22;
    }
    if (! device.unmount_all(u64(2))) return 5;
    device.shutdown();
    device.wait_stopped();
    if (device.state() != AudioDeviceState::Stopped) return 6;
    if (device.apply(desired.clone())) return 7;
    device.shutdown();
    device.wait_stopped();
    return 0;
}
