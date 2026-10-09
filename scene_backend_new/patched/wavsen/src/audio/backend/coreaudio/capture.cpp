module;
#include "process_tap.hpp"

module wavsen.audio.backend.coreaudio.capture;
import rstd;
import wavsen.ffi.coreaudio;

using namespace rstd::prelude;
namespace ca = wavsen::ffi::coreaudio;
using namespace ca;

auto capture_dictionary(const char* const* names, const void** values, rstd::size_t count)
    -> CFDictionaryRef {
    const void* keys[5] {};
    for (rstd::size_t i = 0; i < count; ++i) {
        keys[i] = CFStringCreateWithCString(kCFAllocatorDefault, names[i], kCFStringEncodingUTF8);
        if (! keys[i]) {
            for (rstd::size_t j = 0; j < i; ++j) CFRelease(keys[j]);
            return nullptr;
        }
    }
    auto result = CFDictionaryCreate(kCFAllocatorDefault,
                                     keys,
                                     values,
                                     count,
                                     &kCFTypeDictionaryKeyCallBacks,
                                     &kCFTypeDictionaryValueCallBacks);
    for (rstd::size_t i = 0; i < count; ++i) CFRelease(keys[i]);
    return result;
}

namespace wavsen::audio::backend
{
class CoreAudioCapture::Impl {
public:
    explicit Impl(CaptureSink sink): sink_(sink) {}
    ~Impl() { stop(); }

    auto start() -> Result<DeviceDesc, Error> {
        if (is_running()) return Ok(DeviceDesc { u32(2), u32(48000) });
        stop();
        const void* uid    = nullptr;
        auto        status = create_process_tap(&tap_, &uid);
        if (status != 0)
            return Err(
                Error { status == -2 ? ErrorKind::UnsupportedSystem : ErrorKind::NativeFailure,
                        i32(status) });
        AudioObjectPropertyAddress  address { kAudioTapPropertyFormat,
                                              kAudioObjectPropertyScopeGlobal,
                                              kAudioObjectPropertyElementMain };
        AudioStreamBasicDescription format {};
        UInt32                      size = sizeof(format);
        status = AudioObjectGetPropertyData(tap_, &address, 0, nullptr, &size, &format);
        if (status != 0 || format.mFormatID != kAudioFormatLinearPCM ||
            ! (format.mFormatFlags & kAudioFormatFlagIsFloat) || format.mBitsPerChannel != 32 ||
            format.mSampleRate != 48000) {
            CFRelease(uid);
            stop();
            return Err(Error { ErrorKind::InvalidFormat, i32(status) });
        }
        status = create_aggregate(static_cast<CFStringRef>(uid));
        CFRelease(uid);
        if (status == 0) status = AudioDeviceCreateIOProcID(aggregate_, &on_audio, this, &io_);
        if (status == 0) {
            sink_.discontinuity(sink_.context);
            running_.store(true, rstd::sync::atomic::Ordering::Release);
            status = AudioDeviceStart(aggregate_, io_);
        }
        if (status != 0) {
            stop();
            return Err(Error { ErrorKind::NativeFailure, i32(status) });
        }
        return Ok(DeviceDesc { u32(2), u32(48000) });
    }

    void stop() {
        running_.store(false, rstd::sync::atomic::Ordering::Release);
        if (io_) {
            AudioDeviceStop(aggregate_, io_);
            AudioDeviceDestroyIOProcID(aggregate_, io_);
            io_ = nullptr;
        }
        if (aggregate_ != kAudioObjectUnknown) {
            AudioHardwareDestroyAggregateDevice(aggregate_);
            aggregate_ = kAudioObjectUnknown;
        }
        destroy_process_tap(tap_);
        tap_ = kAudioObjectUnknown;
    }

    auto is_running() const -> bool { return running_.load(rstd::sync::atomic::Ordering::Acquire); }

private:
    auto create_aggregate(CFStringRef uid) -> OSStatus {
        SInt32 quality = 0x40;
        auto   number  = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &quality);
        if (! number) return -1;
        const char* tap_names[]  = { "uid", "drift", "drift quality" };
        const void* tap_values[] = { uid, kCFBooleanTrue, number };
        auto        entry        = capture_dictionary(tap_names, tap_values, 3);
        CFRelease(number);
        if (! entry) return -1;
        const void* entries[] = { entry };
        auto        taps = CFArrayCreate(kCFAllocatorDefault, entries, 1, &kCFTypeArrayCallBacks);
        CFRelease(entry);
        if (! taps) return -1;
        auto uuid       = CFUUIDCreate(kCFAllocatorDefault);
        auto identifier = uuid ? CFUUIDCreateString(kCFAllocatorDefault, uuid) : nullptr;
        if (uuid) CFRelease(uuid);
        auto name = CFStringCreateWithCString(
            kCFAllocatorDefault, "wavsen system audio capture", kCFStringEncodingUTF8);
        if (! identifier || ! name) {
            if (identifier) CFRelease(identifier);
            if (name) CFRelease(name);
            CFRelease(taps);
            return -1;
        }
        const char* names[]     = { aggregate_name_key,
                                    aggregate_uid_key,
                                    aggregate_private_key,
                                    aggregate_taps_key,
                                    aggregate_autostart_key };
        const void* values[]    = { name, identifier, kCFBooleanTrue, taps, kCFBooleanFalse };
        auto        description = capture_dictionary(names, values, 5);
        auto        status =
            description ? AudioHardwareCreateAggregateDevice(description, &aggregate_) : -1;
        if (description) CFRelease(description);
        CFRelease(name);
        CFRelease(identifier);
        CFRelease(taps);
        return status;
    }

    static OSStatus on_audio(AudioObjectID, const AudioTimeStamp*, const AudioBufferList* input,
                             const AudioTimeStamp*, AudioBufferList*, const AudioTimeStamp*,
                             void* user) {
        auto& self = *static_cast<Impl*>(user);
        if (! self.is_running() || ! input || input->mNumberBuffers == 0) return 0;
        if (input->mNumberBuffers == 1) {
            const auto& buffer = input->mBuffers[0];
            if (! buffer.mData || buffer.mNumberChannels == 0) return 0;
            const auto frames = buffer.mDataByteSize / (sizeof(float) * buffer.mNumberChannels);
            self.sink_.ingest(self.sink_.context,
                              static_cast<const float*>(buffer.mData),
                              static_cast<rstd::uint32_t>(frames),
                              buffer.mNumberChannels);
            return 0;
        }
        const auto channels = rstd::cmp::min(input->mNumberBuffers, UInt32(2));
        UInt32     frames   = ~UInt32(0);
        for (UInt32 c = 0; c < channels; ++c) {
            if (! input->mBuffers[c].mData || input->mBuffers[c].mNumberChannels != 1) return 0;
            frames =
                rstd::cmp::min(frames, UInt32(input->mBuffers[c].mDataByteSize / sizeof(float)));
        }
        for (UInt32 offset = 0; offset < frames; offset += 8192) {
            auto count = rstd::cmp::min(UInt32(8192), frames - offset);
            for (UInt32 frame = 0; frame < count; ++frame)
                for (UInt32 c = 0; c < channels; ++c)
                    self.scratch_[frame * channels + c] =
                        static_cast<const float*>(input->mBuffers[c].mData)[offset + frame];
            self.sink_.ingest(self.sink_.context, self.scratch_, count, channels);
        }
        return 0;
    }

    CaptureSink                      sink_;
    AudioObjectID                    tap_ { kAudioObjectUnknown };
    AudioObjectID                    aggregate_ { kAudioObjectUnknown };
    AudioDeviceIOProcID              io_ {};
    rstd::sync::atomic::Atomic<bool> running_ { false };
    float                            scratch_[8192 * 2] {};
};

CoreAudioCapture::CoreAudioCapture(CaptureSink sink): impl_(Box<Impl>::make(sink)) {}
CoreAudioCapture::~CoreAudioCapture() = default;
auto CoreAudioCapture::start() -> Result<DeviceDesc, Error> { return impl_->start(); }
void CoreAudioCapture::stop() { impl_->stop(); }
auto CoreAudioCapture::is_running() const -> bool { return impl_->is_running(); }
} // namespace wavsen::audio::backend
