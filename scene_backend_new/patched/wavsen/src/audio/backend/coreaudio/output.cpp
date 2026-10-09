module wavsen.audio.backend.coreaudio.output;
import rstd;
import wavsen.ffi.coreaudio;

using namespace rstd::prelude;
using namespace wavsen::ffi::coreaudio;
namespace wavsen::audio::backend
{
struct CoreAudioControl {
    ControlFunction function;
    void*           context;
};
struct CoreAudioQueue {
    Vec<CoreAudioControl> pending;
    bool                  stopping {};
};

class CoreAudioOutput::Impl {
public:
    explicit Impl(OutputSink sink)
        : sink_(sink), queue_(CoreAudioQueue {}), cv_(rstd::sync::Condvar::make()) {
        auto thread = rstd::thread::spawn([this]() {
            run();
            return empty {};
        });
        if (thread.is_ok()) worker_ = Some(rstd::move(thread).unwrap());
    }
    ~Impl() {
        {
            auto queue      = queue_.lock().unwrap_unchecked();
            queue->stopping = true;
            cv_.notify_all();
        }
        if (worker_.is_some()) (void)rstd::move(worker_.take().unwrap()).join();
        close();
    }
    bool dispatch(ControlFunction function, void* context) {
        if (worker_.is_none()) return false;
        auto queue = queue_.lock().unwrap_unchecked();
        if (queue->stopping) return false;
        queue->pending.push(CoreAudioControl { function, context });
        cv_.notify_one();
        return true;
    }
    auto open(const AudioClientIdentity& identity) -> Result<DeviceDesc, Error> {
        if (identity.playback_stream_name().is_none()) return Err(Error {});
        close();
        AudioObjectPropertyAddress address { kAudioHardwarePropertyDefaultOutputDevice,
                                             kAudioObjectPropertyScopeGlobal,
                                             kAudioObjectPropertyElementMain };
        AudioObjectID              device        = kAudioObjectUnknown;
        UInt32                     device_size   = sizeof(device);
        auto                       device_status = AudioObjectGetPropertyData(
            kAudioObjectSystemObject, &address, 0, nullptr, &device_size, &device);
        if (device_status != 0 || device == kAudioObjectUnknown)
            return Err(Error { ErrorKind::Unavailable, i32(device_status) });
        AudioComponentDescription component {};
        component.componentType         = kAudioUnitType_Output;
        component.componentSubType      = kAudioUnitSubType_DefaultOutput;
        component.componentManufacturer = kAudioUnitManufacturer_Apple;
        auto found                      = AudioComponentFindNext(nullptr, &component);
        auto status                     = found ? AudioComponentInstanceNew(found, &unit_) : -1;
        if (status != 0) return Err(Error { ErrorKind::Unavailable, i32(status) });
        AudioStreamBasicDescription format {};
        format.mSampleRate     = 48000;
        format.mFormatID       = kAudioFormatLinearPCM;
        format.mFormatFlags    = kAudioFormatFlagsNativeFloatPacked;
        format.mBytesPerPacket = format.mBytesPerFrame = 2 * sizeof(float);
        format.mFramesPerPacket                        = 1;
        format.mChannelsPerFrame                       = 2;
        format.mBitsPerChannel                         = 32;
        status = AudioUnitSetProperty(unit_,
                                      kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Input,
                                      0,
                                      &format,
                                      sizeof(format));
        AURenderCallbackStruct callback { &on_audio, this };
        if (status == 0)
            status = AudioUnitSetProperty(unit_,
                                          kAudioUnitProperty_SetRenderCallback,
                                          kAudioUnitScope_Input,
                                          0,
                                          &callback,
                                          sizeof(callback));
        if (status == 0) status = AudioUnitInitialize(unit_);
        if (status != 0) {
            close();
            return Err(Error { ErrorKind::NativeFailure, i32(status) });
        }
        sink_.changed(sink_.context, OutputState::Ready, Error {});
        return Ok(DeviceDesc { u32(2), u32(48000) });
    }
    void set_playing(bool playing) {
        playing_ = playing;
        sink_.changed(
            sink_.context, playing ? OutputState::Playing : OutputState::Paused, Error {});
    }
    void flush() {
        quiesce();
        sink_.changed(sink_.context, OutputState::Flushed, Error {});
    }
    void close() {
        quiesce();
        playing_ = false;
        if (unit_) {
            AudioUnitUninitialize(unit_);
            AudioComponentInstanceDispose(unit_);
            unit_ = nullptr;
        }
        position_ = 0;
    }

private:
    void quiesce() {
        enabled_.store(false, rstd::sync::atomic::Ordering::SeqCst);
        if (unit_ && running_) AudioOutputUnitStop(unit_);
        running_ = false;
        while (callbacks_.load(rstd::sync::atomic::Ordering::SeqCst) != u32())
            rstd::thread::yield_now();
    }
    void run() {
        for (;;) {
            Vec<CoreAudioControl> batch;
            {
                auto queue = queue_.lock().unwrap_unchecked();
                cv_.wait_while(queue, [](const CoreAudioQueue& value) {
                    return value.pending.is_empty() && ! value.stopping;
                });
                if (queue->stopping && queue->pending.is_empty()) break;
                batch          = rstd::move(queue->pending);
                queue->pending = Vec<CoreAudioControl>();
            }
            // Controls own mutable channel state only after all render callbacks have left it.
            quiesce();
            for (auto& task : batch) task.function(task.context);
            if (unit_ && playing_) {
                enabled_.store(true, rstd::sync::atomic::Ordering::SeqCst);
                auto status = AudioOutputUnitStart(unit_);
                if (status == 0)
                    running_ = true;
                else {
                    enabled_.store(false, rstd::sync::atomic::Ordering::SeqCst);
                    sink_.changed(sink_.context,
                                  OutputState::Failed,
                                  Error { ErrorKind::NativeFailure, i32(status) });
                }
            }
        }
        quiesce();
    }
    static OSStatus on_audio(void* user, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32,
                             UInt32 frames, AudioBufferList* output) {
        if (! output) return 0;
        auto& self = *static_cast<Impl*>(user);
        for (UInt32 i = 0; i < output->mNumberBuffers; ++i) {
            auto& buffer = output->mBuffers[i];
            if (buffer.mData) rstd::mem::memset(buffer.mData, u8(), usize(buffer.mDataByteSize));
        }
        self.callbacks_.fetch_add(u32(1), rstd::sync::atomic::Ordering::SeqCst);
        if (self.enabled_.load(rstd::sync::atomic::Ordering::SeqCst) &&
            output->mNumberBuffers == 1) {
            auto& buffer = output->mBuffers[0];
            if (buffer.mData && buffer.mNumberChannels == 2) {
                auto count =
                    rstd::cmp::min(frames, UInt32(buffer.mDataByteSize / (2 * sizeof(float))));
                self.sink_.render(self.sink_.context, static_cast<float*>(buffer.mData), count);
                self.position_ += count;
                self.sink_.position(self.sink_.context, u64(self.position_));
            }
        }
        self.callbacks_.fetch_sub(u32(1), rstd::sync::atomic::Ordering::SeqCst);
        return 0;
    }
    OutputSink                              sink_;
    rstd::sync::Mutex<CoreAudioQueue>       queue_;
    rstd::sync::Condvar                     cv_;
    Option<rstd::thread::JoinHandle<empty>> worker_;
    AudioUnit                               unit_ {};
    bool                                    playing_ {}, running_ {};
    rstd::uint64_t                          position_ {};
    rstd::sync::atomic::Atomic<bool>        enabled_ { false };
    rstd::sync::atomic::Atomic<u32>         callbacks_ { u32() };
};
CoreAudioOutput::CoreAudioOutput(OutputSink sink): impl_(Box<Impl>::make(sink)) {}
CoreAudioOutput::~CoreAudioOutput() = default;
auto CoreAudioOutput::dispatch(ControlFunction function, void* context) -> bool {
    return impl_->dispatch(function, context);
}
auto CoreAudioOutput::open(const AudioClientIdentity& identity) -> Result<DeviceDesc, Error> {
    return impl_->open(identity);
}
void CoreAudioOutput::set_playing(bool playing) { impl_->set_playing(playing); }
void CoreAudioOutput::flush() { impl_->flush(); }
void CoreAudioOutput::close() { impl_->close(); }
} // namespace wavsen::audio::backend
