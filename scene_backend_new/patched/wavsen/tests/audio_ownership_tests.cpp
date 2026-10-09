#include <rstd/macro.hpp>

import rstd;
import wavsen.audio;
import wavsen.audio.core;

using namespace rstd::prelude;
using namespace wavsen::audio;
using rstd::sync::Arc;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;
using rstd::time::Duration;

namespace
{
struct Counts {
    Atomic<u32> described { u32() };
    Atomic<u32> dropped { u32() };
};

class Channel final : public IPullChannel {
public:
    explicit Channel(Counts& counts): counts_(counts) {}
    ~Channel() override { counts_.dropped.fetch_add(u32(1), Ordering::Release); }
    auto next_pcm(void*, u32) -> u64 override { return u64(); }
    void pass_desc(const DeviceDesc&) override {
        counts_.described.fetch_add(u32(1), Ordering::Release);
    }

private:
    Counts& counts_;
};

auto channel(Counts& counts) -> Box<dyn<PullChannelObject>> {
    auto value = Box<Channel>::make(counts);
    return Box<dyn<PullChannelObject>>::from_raw(
        dyn<PullChannelObject>::from_ptr(rstd::move(value).into_raw()));
}

class Stream final : public SoundStream {
public:
    explicit Stream(Counts& counts): counts_(counts) {}
    ~Stream() override { counts_.dropped.fetch_add(u32(1), Ordering::Release); }
    auto next_pcm(void*, u32) -> u64 override { return u64(); }
    void pass_desc(const Desc&) override { counts_.described.fetch_add(u32(1), Ordering::Release); }

private:
    Counts& counts_;
};

void wait_for(const Atomic<u32>& count, u32 expected) {
    for (int i = 0; i < 300 && count.load(Ordering::Acquire) != expected; ++i)
        rstd::thread::sleep(Duration::from_millis(u64(10)));
    rstd_assert(count.load(Ordering::Acquire) == expected);
}
} // namespace

int main() {
    Counts      mounted, stale, rejected, stopped;
    AudioDevice device;
    rstd_assert(device.mount(channel(mounted), u64(2)));
    wait_for(mounted.described, u32(1));
    rstd_assert(device.mount(channel(stale), u64(1)));
    wait_for(stale.dropped, u32(1));
    rstd_assert(stale.described.load(Ordering::Acquire) == u32());
    rstd_assert(device.unmount_all(u64(1)));
    rstd_assert(device.unmount_all(u64(3)));
    wait_for(mounted.dropped, u32(1));

    auto        capture = Arc<int>::make(7);
    auto        weak    = capture.downgrade();
    Atomic<u32> events { u32() };
    device.set_event_sink(Some(AudioDeviceEventSink::make(
        [capture = rstd::move(capture), &device, &events](AudioDeviceEvent) {
            device.set_event_sink(None());
            rstd_assert(*capture == 7);
            events.fetch_add(u32(1), Ordering::Release);
        })));
    rstd_assert(device.apply(AudioDeviceDesiredState {}));
    wait_for(events, u32(1));
    for (int i = 0; i < 300 && ! weak.expired(); ++i)
        rstd::thread::sleep(Duration::from_millis(u64(10)));
    rstd_assert(weak.expired());

    device.set_event_sink(None());
    rstd_assert(device.apply(AudioDeviceDesiredState {}));
    rstd_assert(device.mount(channel(stopped), u64(4)));
    device.shutdown();
    device.wait_stopped();
    rstd_assert(stopped.dropped.load(Ordering::Acquire) == u32(1));
    rstd_assert(! device.mount(channel(rejected), u64(5)));
    rstd_assert(rejected.dropped.load(Ordering::Acquire) == u32(1));

    Counts stream_counts;
    {
        SoundManager manager;
        auto         stream = Box<Stream>::make(stream_counts);
        manager.mount(Box<dyn<SoundStreamObject>>::from_raw(
            dyn<SoundStreamObject>::from_ptr(rstd::move(stream).into_raw())));
        wait_for(stream_counts.described, u32(1));
    }
    rstd_assert(stream_counts.dropped.load(Ordering::Acquire) == u32(1));
}
