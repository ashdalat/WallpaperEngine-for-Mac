module wavsen.audio.backend.pulse.capture;

import rstd;
import rstd.log;
import wavsen.ffi.pulse;

using namespace rstd::prelude;

namespace wavsen::audio::backend
{

namespace pulse_ffi = wavsen::ffi::pulse;

namespace
{

constexpr rstd::uint32_t kDefaultRate     = 48000;
constexpr rstd::uint32_t kDefaultChannels = 2;
constexpr rstd::uint32_t kQuantum         = 1024;

} // namespace

class PulseCapture::Impl {
public:
    explicit Impl(CaptureSink sink): sink_(sink) {}
    ~Impl() { uninit(); }

    bool init() {
        if (is_inited()) return true;
        if (loop_) uninit();

        api_ = pulse_ffi::load();
        if (! api_) {
            rstd::log::error("wavsen::audio: capture failed to load PulseAudio: {}",
                             pulse_ffi::load_error());
            return false;
        }

        loop_ = api_->pa_threaded_mainloop_new();
        if (! loop_) {
            rstd::log::error("wavsen::audio: capture pa_threaded_mainloop_new failed");
            return false;
        }
        if (api_->pa_threaded_mainloop_start(loop_) < 0) {
            rstd::log::error("wavsen::audio: capture pa_threaded_mainloop_start failed");
            api_->pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
            return false;
        }

        api_->pa_threaded_mainloop_lock(loop_);

        ctx_ = api_->pa_context_new(api_->pa_threaded_mainloop_get_api(loop_), "wavsen-capture");
        if (! ctx_) {
            api_->pa_threaded_mainloop_unlock(loop_);
            rstd::log::error("wavsen::audio: capture pa_context_new failed");
            api_->pa_threaded_mainloop_stop(loop_);
            api_->pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
            return false;
        }
        api_->pa_context_set_state_callback(ctx_, &Impl::on_context_state, this);

        if (api_->pa_context_connect(ctx_, nullptr, pulse_ffi::context_noflags, nullptr) < 0) {
            rstd::log::error("wavsen::audio: capture pa_context_connect failed: {}",
                             api_->pa_strerror(api_->pa_context_errno(ctx_)));
            destroy_locked();
            api_->pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
            return false;
        }

        for (;;) {
            const auto st = api_->pa_context_get_state(ctx_);
            if (st == pulse_ffi::context_ready) break;
            if (! pulse_ffi::context_is_good(st)) {
                rstd::log::error("wavsen::audio: capture pa_context failed: {}",
                                 api_->pa_strerror(api_->pa_context_errno(ctx_)));
                destroy_locked();
                api_->pa_threaded_mainloop_unlock(loop_);
                shutdown_loop();
                return false;
            }
            api_->pa_threaded_mainloop_wait(loop_);
        }

        default_sink_.clear();
        server_info_done_ = false;
        auto* op          = api_->pa_context_get_server_info(ctx_, &Impl::on_server_info, this);
        if (! op) {
            rstd::log::error("wavsen::audio: capture pa_context_get_server_info failed");
            destroy_locked();
            api_->pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
            return false;
        }
        api_->pa_operation_unref(op);
        while (! server_info_done_) {
            api_->pa_threaded_mainloop_wait(loop_);
        }
        if (default_sink_.is_empty()) {
            rstd::log::error("wavsen::audio: capture could not resolve default sink");
            destroy_locked();
            api_->pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
            return false;
        }
        monitor_source_.clear();
        sink_info_done_ = false;
        auto sink_name  = rstd::ffi::CString::make(default_sink_.clone()).unwrap();
        op              = api_->pa_context_get_sink_info_by_name(
            ctx_, sink_name.as_ptr(), &Impl::on_sink_info, this);
        if (! op) {
            rstd::log::error("wavsen::audio: capture pa_context_get_sink_info_by_name failed");
            destroy_locked();
            api_->pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
            return false;
        }
        api_->pa_operation_unref(op);
        while (! sink_info_done_) api_->pa_threaded_mainloop_wait(loop_);
        if (monitor_source_.is_empty()) {
            rstd::log::error("wavsen::audio: default sink has no monitor source");
            destroy_locked();
            api_->pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
            return false;
        }
        auto monitor_name_c = rstd::ffi::CString::make(monitor_source_.clone()).unwrap();
        if (! connect_stream_locked(monitor_name_c.as_ptr())) {
            destroy_locked();
            api_->pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
            return false;
        }

        for (;;) {
            const auto st = api_->pa_stream_get_state(stream_);
            if (st == pulse_ffi::stream_ready) break;
            if (! pulse_ffi::stream_is_good(st)) {
                rstd::log::error("wavsen::audio: capture pa_stream failed: {}",
                                 api_->pa_strerror(api_->pa_context_errno(ctx_)));
                destroy_locked();
                api_->pa_threaded_mainloop_unlock(loop_);
                shutdown_loop();
                return false;
            }
            api_->pa_threaded_mainloop_wait(loop_);
        }

        api_->pa_context_set_subscribe_callback(ctx_, &Impl::on_subscription, this);
        if (auto* subscribe = api_->pa_context_subscribe(
                ctx_,
                static_cast<pulse_ffi::pa_subscription_mask_t>(pulse_ffi::subscription_mask_sink |
                                                               pulse_ffi::subscription_mask_server),
                nullptr,
                nullptr)) {
            api_->pa_operation_unref(subscribe);
        }

        api_->pa_threaded_mainloop_unlock(loop_);

        rstd::log::info("wavsen::audio: capture inited (pulse monitor '{}', "
                        "{} ch @ {} Hz)",
                        monitor_source_,
                        kDefaultChannels,
                        kDefaultRate);
        return true;
    }

    void uninit() {
        if (loop_) {
            api_->pa_threaded_mainloop_lock(loop_);
            destroy_locked();
            api_->pa_threaded_mainloop_unlock(loop_);
            shutdown_loop();
        }
    }

    bool is_inited() const {
        return loop_ != nullptr && stream_ != nullptr &&
               ! capture_failed_.load(rstd::sync::atomic::Ordering::Acquire);
    }

private:
    bool connect_stream_locked(const char* monitor) {
        destroy_stream_locked();
        pulse_ffi::pa_sample_spec sample_spec {};
        sample_spec.format   = pulse_ffi::sample_float32le;
        sample_spec.rate     = kDefaultRate;
        sample_spec.channels = static_cast<rstd::uint8_t>(kDefaultChannels);
        pulse_ffi::pa_channel_map channel_map {};
        api_->pa_channel_map_init_stereo(&channel_map);
        stream_ = api_->pa_stream_new(ctx_, "wavsen-capture", &sample_spec, &channel_map);
        if (! stream_) {
            rstd::log::error("wavsen::audio: capture pa_stream_new failed: {}",
                             api_->pa_strerror(api_->pa_context_errno(ctx_)));
            return false;
        }
        api_->pa_stream_set_state_callback(stream_, &Impl::on_stream_state, this);
        api_->pa_stream_set_read_callback(stream_, &Impl::on_read, this);
        const auto frame_bytes = kDefaultChannels * static_cast<rstd::uint32_t>(sizeof(float));
        pulse_ffi::pa_buffer_attr attr {};
        attr.maxlength = static_cast<rstd::uint32_t>(-1);
        attr.tlength   = static_cast<rstd::uint32_t>(-1);
        attr.prebuf    = static_cast<rstd::uint32_t>(-1);
        attr.minreq    = static_cast<rstd::uint32_t>(-1);
        attr.fragsize  = kQuantum * frame_bytes;
        const auto flags =
            static_cast<pulse_ffi::pa_stream_flags_t>(pulse_ffi::stream_adjust_latency);
        sink_.discontinuity(sink_.context);
        if (api_->pa_stream_connect_record(stream_, monitor, &attr, flags) < 0) {
            rstd::log::error("wavsen::audio: capture pa_stream_connect_record failed: {}",
                             api_->pa_strerror(api_->pa_context_errno(ctx_)));
            destroy_stream_locked();
            return false;
        }
        return true;
    }

    void destroy_stream_locked() {
        if (! stream_) return;
        api_->pa_stream_set_state_callback(stream_, nullptr, nullptr);
        api_->pa_stream_set_read_callback(stream_, nullptr, nullptr);
        api_->pa_stream_disconnect(stream_);
        api_->pa_stream_unref(stream_);
        stream_ = nullptr;
    }

    void destroy_locked() {
        destroy_stream_locked();
        if (ctx_) {
            api_->pa_context_set_state_callback(ctx_, nullptr, nullptr);
            api_->pa_context_set_subscribe_callback(ctx_, nullptr, nullptr);
            api_->pa_context_disconnect(ctx_);
            api_->pa_context_unref(ctx_);
            ctx_ = nullptr;
        }
    }
    void shutdown_loop() {
        if (loop_) {
            api_->pa_threaded_mainloop_stop(loop_);
            api_->pa_threaded_mainloop_free(loop_);
            loop_ = nullptr;
        }
    }

    static void on_context_state(pulse_ffi::pa_context* /*c*/, void* user) {
        auto* self = static_cast<Impl*>(user);
        if (! pulse_ffi::context_is_good(self->api_->pa_context_get_state(self->ctx_))) {
            self->capture_failed_.store(true, rstd::sync::atomic::Ordering::Release);
            self->sink_.discontinuity(self->sink_.context);
        }
        self->api_->pa_threaded_mainloop_signal(self->loop_, 0);
    }

    static void on_stream_state(pulse_ffi::pa_stream* stream, void* user) {
        auto*      self  = static_cast<Impl*>(user);
        const auto state = self->api_->pa_stream_get_state(stream);
        if (state == pulse_ffi::stream_ready) {
            self->capture_failed_.store(false, rstd::sync::atomic::Ordering::Release);
        } else if (! pulse_ffi::stream_is_good(state)) {
            self->capture_failed_.store(true, rstd::sync::atomic::Ordering::Release);
            self->sink_.discontinuity(self->sink_.context);
        }
        self->api_->pa_threaded_mainloop_signal(self->loop_, 0);
    }

    static void on_server_info(pulse_ffi::pa_context* /*c*/, const pulse_ffi::pa_server_info* info,
                               void* user) {
        auto* self = static_cast<Impl*>(user);
        if (info && info->default_sink_name) {
            self->default_sink_ =
                String::make(rstd::ffi::CStr::from_ptr(info->default_sink_name).to_str().unwrap());
        }
        self->server_info_done_ = true;
        self->api_->pa_threaded_mainloop_signal(self->loop_, 0);
    }

    static void on_sink_info(pulse_ffi::pa_context* /*c*/, const pulse_ffi::pa_sink_info* info,
                             int eol, void* user) {
        auto* self = static_cast<Impl*>(user);
        if (info && info->monitor_source_name) {
            self->monitor_source_ = String::make(
                rstd::ffi::CStr::from_ptr(info->monitor_source_name).to_str().unwrap());
        }
        if (eol != 0) {
            self->sink_info_done_ = true;
            self->api_->pa_threaded_mainloop_signal(self->loop_, 0);
        }
    }

    static void on_subscription(pulse_ffi::pa_context* context,
                                pulse_ffi::pa_subscription_event_type_t, rstd::uint32_t,
                                void* user) {
        auto* self = static_cast<Impl*>(user);
        if (self->refresh_pending_) return;
        self->refresh_pending_ = true;
        auto* operation =
            self->api_->pa_context_get_server_info(context, &Impl::on_refresh_server_info, self);
        if (! operation) {
            self->refresh_pending_ = false;
            return;
        }
        self->api_->pa_operation_unref(operation);
    }

    static void on_refresh_server_info(pulse_ffi::pa_context*           context,
                                       const pulse_ffi::pa_server_info* info, void* user) {
        auto* self = static_cast<Impl*>(user);
        if (! info || ! info->default_sink_name) {
            self->refresh_pending_ = false;
            return;
        }
        self->refreshed_monitor_.clear();
        auto sink =
            String::make(rstd::ffi::CStr::from_ptr(info->default_sink_name).to_str().unwrap());
        auto  name      = rstd::ffi::CString::make(rstd::move(sink)).unwrap();
        auto* operation = self->api_->pa_context_get_sink_info_by_name(
            context, name.as_ptr(), &Impl::on_refresh_sink_info, self);
        if (! operation) {
            self->refresh_pending_ = false;
            return;
        }
        self->api_->pa_operation_unref(operation);
    }

    static void on_refresh_sink_info(pulse_ffi::pa_context*, const pulse_ffi::pa_sink_info* info,
                                     int eol, void* user) {
        auto* self = static_cast<Impl*>(user);
        if (info && info->monitor_source_name) {
            self->refreshed_monitor_ = String::make(
                rstd::ffi::CStr::from_ptr(info->monitor_source_name).to_str().unwrap());
        }
        if (eol == 0) return;
        self->refresh_pending_ = false;
        if (self->refreshed_monitor_.is_empty() ||
            self->refreshed_monitor_ == self->monitor_source_)
            return;
        self->monitor_source_ = self->refreshed_monitor_.clone();
        auto monitor          = rstd::ffi::CString::make(self->monitor_source_.clone()).unwrap();
        if (! self->connect_stream_locked(monitor.as_ptr())) {
            rstd::log::error("wavsen::audio: failed to follow default PulseAudio sink");
        }
    }

    static void on_read(pulse_ffi::pa_stream* s, size_t /*nbytes*/, void* user) {
        auto* self = static_cast<Impl*>(user);
        while (self->api_->pa_stream_readable_size(s) > 0) {
            const void* data = nullptr;
            size_t      sz   = 0;
            if (self->api_->pa_stream_peek(s, &data, &sz) < 0) return;
            if (sz == 0) return;
            // A hole: data==nullptr means dropped samples; advance and continue.
            if (! data) {
                self->sink_.discontinuity(self->sink_.context);
                self->api_->pa_stream_drop(s);
                continue;
            }
            constexpr rstd::uint32_t channels = kDefaultChannels;
            constexpr rstd::uint32_t stride   = channels * sizeof(float);
            const auto*              src      = static_cast<const float*>(data);
            const rstd::uint32_t     n_frames = static_cast<rstd::uint32_t>(sz / stride);
            self->ingest(src, n_frames, channels);
            self->api_->pa_stream_drop(s);
        }
    }

    void ingest(const float* src, rstd::uint32_t n_frames, rstd::uint32_t channels) {
        sink_.ingest(sink_.context, src, n_frames, channels);
    }

    const pulse_ffi::Api*            api_    = nullptr;
    pulse_ffi::pa_threaded_mainloop* loop_   = nullptr;
    pulse_ffi::pa_context*           ctx_    = nullptr;
    pulse_ffi::pa_stream*            stream_ = nullptr;
    String                           default_sink_;
    String                           monitor_source_;
    String                           refreshed_monitor_;
    bool                             server_info_done_ = false;
    bool                             sink_info_done_   = false;
    bool                             refresh_pending_  = false;
    rstd::sync::atomic::Atomic<bool> capture_failed_ { false };

    CaptureSink sink_;
};

PulseCapture::PulseCapture(CaptureSink sink): impl_(Box<Impl>::make(sink)) {}
PulseCapture::~PulseCapture() = default;
auto PulseCapture::start() -> Result<DeviceDesc, Error> {
    if (! impl_->init()) return Err(Error {});
    return Ok(DeviceDesc { u32(2), u32(48000) });
}
void PulseCapture::stop() { impl_->uninit(); }
auto PulseCapture::is_running() const -> bool { return impl_->is_inited(); }

} // namespace wavsen::audio::backend
