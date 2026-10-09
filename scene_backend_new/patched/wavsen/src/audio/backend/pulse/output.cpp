module wavsen.audio.backend.pulse.output;
import rstd;
import rstd.log;
import wavsen.ffi.pulse;
using namespace rstd::prelude;
namespace pulse_ffi = wavsen::ffi::pulse;
namespace wavsen::audio::backend
{
constexpr rstd::uint32_t kDefaultRate = 48000, kDefaultChannels = 2, kQuantum = 1024;
struct PulseControl {
    ControlFunction function;
    void*           context;
};
class PulseOutput::Impl {
public:
    explicit Impl(OutputSink sink): sink_(sink) {
        api_ = pulse_ffi::load();
        if (! api_) return;
        loop_ = api_->pa_threaded_mainloop_new();
        if (loop_ && api_->pa_threaded_mainloop_start(loop_) < 0) {
            api_->pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
        }
    }
    ~Impl() {
        if (! loop_) return;
        api_->pa_threaded_mainloop_lock(loop_);
        close();
        api_->pa_threaded_mainloop_unlock(loop_);
        api_->pa_threaded_mainloop_stop(loop_);
        api_->pa_threaded_mainloop_free(loop_);
    }
    bool dispatch(ControlFunction function, void* context) {
        if (! loop_) return false;
        api_->pa_threaded_mainloop_lock(loop_);
        const bool schedule = controls_.is_empty();
        controls_.push(PulseControl { function, context });
        if (schedule)
            api_->pa_mainloop_api_once(
                api_->pa_threaded_mainloop_get_api(loop_), &on_control, this);
        api_->pa_threaded_mainloop_unlock(loop_);
        return true;
    }
    auto open(const AudioClientIdentity& identity) -> Result<DeviceDesc, Error> {
        if (! loop_) return Err(Error { ErrorKind::Unavailable });
        if (identity.playback_stream_name().is_none()) return Err(Error {});
        identity_ = identity.clone();
        start_context();
        return Ok(DeviceDesc { u32(2), u32(48000) });
    }
    void set_playing(bool playing) {
        if (! stream_) return;
        requested_playing_ = playing;
        auto* operation    = api_->pa_stream_cork(stream_, playing ? 0 : 1, &on_cork, this);
        if (operation)
            api_->pa_operation_unref(operation);
        else
            fail("pa_stream_cork failed");
    }
    void flush() {
        if (! stream_) return;
        auto* operation = api_->pa_stream_flush(stream_, &on_flush, this);
        if (operation)
            api_->pa_operation_unref(operation);
        else
            fail("pa_stream_flush failed");
    }
    void close() { cleanup_device(); }

private:
    static void on_control(pulse_ffi::pa_mainloop_api*, void* user) {
        auto& self     = *static_cast<Impl*>(user);
        auto  batch    = rstd::move(self.controls_);
        self.controls_ = Vec<PulseControl>();
        for (const auto& task : batch) task.function(task.context);
    }
    void start_context() {
        const auto stream_name = identity_.playback_stream_name();
        if (! stream_name) {
            fail("invalid audio client component");
            return;
        }

        auto application_name =
            rstd::ffi::CString::make(identity_.application_name.clone()).unwrap();
        auto  application_id = rstd::ffi::CString::make(identity_.application_id.clone()).unwrap();
        auto* properties     = api_->pa_proplist_new();
        if (! properties ||
            api_->pa_proplist_sets(
                properties, pulse_ffi::prop_application_name, application_name.as_ptr()) < 0 ||
            api_->pa_proplist_sets(
                properties, pulse_ffi::prop_application_id, application_id.as_ptr()) < 0) {
            if (properties) api_->pa_proplist_free(properties);
            fail("failed to build PulseAudio context properties");
            return;
        }

        ctx_ = api_->pa_context_new_with_proplist(
            api_->pa_threaded_mainloop_get_api(loop_), application_name.as_ptr(), properties);
        api_->pa_proplist_free(properties);
        if (! ctx_) {
            fail("pa_context_new failed");
            return;
        }
        api_->pa_context_set_state_callback(ctx_, &Impl::on_context_state, this);
        if (api_->pa_context_connect(ctx_, nullptr, pulse_ffi::context_noflags, nullptr) < 0) {
            auto error = rstd::format("pa_context_connect failed: {}",
                                      api_->pa_strerror(api_->pa_context_errno(ctx_)));
            cleanup_device();
            fail(rstd::move(error));
            return;
        }
    }

    void start_stream() {
        if (stream_ || ! ctx_) return;
        const auto stream_name = identity_.playback_stream_name();
        if (! stream_name) {
            fail("invalid audio client component");
            return;
        }

        pulse_ffi::pa_sample_spec sample_spec {};
        sample_spec.format   = pulse_ffi::sample_float32le;
        sample_spec.rate     = kDefaultRate;
        sample_spec.channels = static_cast<rstd::uint8_t>(kDefaultChannels);

        pulse_ffi::pa_channel_map channel_map {};
        api_->pa_channel_map_init_stereo(&channel_map);

        auto application_name =
            rstd::ffi::CString::make(identity_.application_name.clone()).unwrap();
        auto  application_id = rstd::ffi::CString::make(identity_.application_id.clone()).unwrap();
        auto  media_name     = rstd::ffi::CString::make(identity_.media_name.clone()).unwrap();
        auto  media_role     = rstd::ffi::CString::make(identity_.media_role.clone()).unwrap();
        auto  stream_name_c  = rstd::ffi::CString::make(stream_name->clone()).unwrap();
        auto* properties     = api_->pa_proplist_new();
        if (! properties ||
            api_->pa_proplist_sets(
                properties, pulse_ffi::prop_application_name, application_name.as_ptr()) < 0 ||
            api_->pa_proplist_sets(
                properties, pulse_ffi::prop_application_id, application_id.as_ptr()) < 0 ||
            api_->pa_proplist_sets(properties, pulse_ffi::prop_media_name, media_name.as_ptr()) <
                0 ||
            api_->pa_proplist_sets(properties, pulse_ffi::prop_media_role, media_role.as_ptr()) <
                0) {
            if (properties) api_->pa_proplist_free(properties);
            fail("failed to build PulseAudio stream properties");
            return;
        }

        stream_ = api_->pa_stream_new_with_proplist(
            ctx_, stream_name_c.as_ptr(), &sample_spec, &channel_map, properties);
        api_->pa_proplist_free(properties);
        if (! stream_) {
            fail(rstd::format("pa_stream_new failed: {}",
                              api_->pa_strerror(api_->pa_context_errno(ctx_))));
            return;
        }

        api_->pa_stream_set_state_callback(stream_, &Impl::on_stream_state, this);
        api_->pa_stream_set_write_callback(stream_, &Impl::on_write, this);

        const auto frame_bytes = kDefaultChannels * static_cast<rstd::uint32_t>(sizeof(float));
        pulse_ffi::pa_buffer_attr buffer_attr {};
        buffer_attr.maxlength = static_cast<rstd::uint32_t>(-1);
        buffer_attr.tlength   = kQuantum * frame_bytes * 4;
        buffer_attr.prebuf    = static_cast<rstd::uint32_t>(-1);
        buffer_attr.minreq    = kQuantum * frame_bytes;
        buffer_attr.fragsize  = static_cast<rstd::uint32_t>(-1);

        const auto flags = static_cast<pulse_ffi::pa_stream_flags_t>(
            pulse_ffi::stream_adjust_latency | pulse_ffi::stream_auto_timing_update |
            pulse_ffi::stream_interpolate_timing | pulse_ffi::stream_start_corked);
        if (api_->pa_stream_connect_playback(
                stream_, nullptr, &buffer_attr, flags, nullptr, nullptr) < 0) {
            auto error = rstd::format("pa_stream_connect_playback failed: {}",
                                      api_->pa_strerror(api_->pa_context_errno(ctx_)));
            cleanup_stream();
            fail(rstd::move(error));
        }
    }

    void cleanup_stream() {
        if (! stream_) return;
        api_->pa_stream_set_state_callback(stream_, nullptr, nullptr);
        api_->pa_stream_set_write_callback(stream_, nullptr, nullptr);
        api_->pa_stream_disconnect(stream_);
        api_->pa_stream_unref(stream_);
        stream_ = nullptr;
    }
    void cleanup_device() {
        cleanup_stream();
        if (! ctx_) return;
        api_->pa_context_set_state_callback(ctx_, nullptr, nullptr);
        api_->pa_context_disconnect(ctx_);
        api_->pa_context_unref(ctx_);
        ctx_ = nullptr;
    }
    void fail(String error) {
        rstd::log::error("wavsen::audio: {}", error);
        sink_.changed(sink_.context, OutputState::Failed, Error {});
    }
    void fail(const char* message) {
        fail(String::make(rstd::ffi::CStr::from_ptr(message).to_str().unwrap()));
    }
    static void on_context_state(pulse_ffi::pa_context* context, void* user) {
        auto& self = *static_cast<Impl*>(user);
        if (context != self.ctx_) return;
        auto state = self.api_->pa_context_get_state(context);
        if (state == pulse_ffi::context_ready)
            self.start_stream();
        else if (state == pulse_ffi::context_failed || state == pulse_ffi::context_terminated)
            self.fail("PulseAudio context failed");
    }
    static void on_stream_state(pulse_ffi::pa_stream* stream, void* user) {
        auto& self = *static_cast<Impl*>(user);
        if (stream != self.stream_) return;
        auto state = self.api_->pa_stream_get_state(stream);
        if (state == pulse_ffi::stream_ready)
            self.sink_.changed(self.sink_.context, OutputState::Ready, Error {});
        else if (state == pulse_ffi::stream_failed || state == pulse_ffi::stream_terminated)
            self.fail("PulseAudio stream failed");
    }
    static void on_cork(pulse_ffi::pa_stream* stream, int success, void* user) {
        auto& self = *static_cast<Impl*>(user);
        if (stream != self.stream_) return;
        if (! success) {
            self.fail("PulseAudio cork failed");
            return;
        }
        self.sink_.changed(self.sink_.context,
                           self.requested_playing_ ? OutputState::Playing : OutputState::Paused,
                           Error {});
    }
    static void on_flush(pulse_ffi::pa_stream* stream, int success, void* user) {
        auto& self = *static_cast<Impl*>(user);
        if (stream != self.stream_) return;
        if (! success) {
            self.fail("PulseAudio flush failed");
            return;
        }
        self.sink_.changed(self.sink_.context, OutputState::Flushed, Error {});
    }
    static void on_write(pulse_ffi::pa_stream* stream, size_t bytes, void* user) {
        auto&                self = *static_cast<Impl*>(user);
        pulse_ffi::pa_usec_t usec = 0;
        if (self.api_->pa_stream_get_time(stream, &usec) >= 0)
            self.sink_.position(self.sink_.context, u64(usec) * u64(48000) / u64(1'000'000));
        while (bytes >= 2 * sizeof(float)) {
            void*  buffer = nullptr;
            size_t wanted = bytes;
            if (self.api_->pa_stream_begin_write(stream, &buffer, &wanted) < 0 || ! buffer) return;
            auto frames = rstd::uint32_t(wanted / (2 * sizeof(float)));
            if (! frames) {
                self.api_->pa_stream_cancel_write(stream);
                return;
            }
            self.sink_.render(self.sink_.context, static_cast<float*>(buffer), frames);
            size_t written = size_t(frames) * 2 * sizeof(float);
            if (self.api_->pa_stream_write(
                    stream, buffer, written, nullptr, 0, pulse_ffi::seek_relative) < 0)
                return;
            bytes = bytes > written ? bytes - written : 0;
        }
    }
    OutputSink                       sink_;
    const pulse_ffi::Api*            api_ {};
    pulse_ffi::pa_threaded_mainloop* loop_ {};
    pulse_ffi::pa_context*           ctx_ {};
    pulse_ffi::pa_stream*            stream_ {};
    Vec<PulseControl>                controls_;
    AudioClientIdentity              identity_;
    bool                             requested_playing_ {};
};
PulseOutput::PulseOutput(OutputSink sink): impl_(Box<Impl>::make(sink)) {}
PulseOutput::~PulseOutput() = default;
auto PulseOutput::dispatch(ControlFunction function, void* context) -> bool {
    return impl_->dispatch(function, context);
}
auto PulseOutput::open(const AudioClientIdentity& identity) -> Result<DeviceDesc, Error> {
    return impl_->open(identity);
}
void PulseOutput::set_playing(bool playing) { impl_->set_playing(playing); }
void PulseOutput::flush() { impl_->flush(); }
void PulseOutput::close() { impl_->close(); }
} // namespace wavsen::audio::backend
