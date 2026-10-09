module;

#include <pulse/pulseaudio.h>

export module wavsen.ffi.pulse;
export import rstd.core;

export namespace wavsen::ffi::pulse
{

using ::pa_buffer_attr;
using ::pa_channel_map;
using ::pa_context;
using ::pa_context_flags_t;
using ::pa_context_state_t;
using ::pa_mainloop_api;
using ::pa_operation;
using ::pa_proplist;
using ::pa_sample_format_t;
using ::pa_sample_spec;
using ::pa_seek_mode_t;
using ::pa_server_info;
using ::pa_sink_info;
using ::pa_stream;
using ::pa_stream_flags_t;
using ::pa_stream_state_t;
using ::pa_subscription_event_type_t;
using ::pa_subscription_mask_t;
using ::pa_threaded_mainloop;
using ::pa_timing_info;
using ::pa_usec_t;

inline constexpr auto context_noflags    = ::PA_CONTEXT_NOFLAGS;
inline constexpr auto context_ready      = ::PA_CONTEXT_READY;
inline constexpr auto context_failed     = ::PA_CONTEXT_FAILED;
inline constexpr auto context_terminated = ::PA_CONTEXT_TERMINATED;

inline constexpr auto stream_ready      = ::PA_STREAM_READY;
inline constexpr auto stream_failed     = ::PA_STREAM_FAILED;
inline constexpr auto stream_terminated = ::PA_STREAM_TERMINATED;

inline constexpr auto sample_float32le          = ::PA_SAMPLE_FLOAT32LE;
inline constexpr auto stream_adjust_latency     = ::PA_STREAM_ADJUST_LATENCY;
inline constexpr auto stream_auto_timing_update = ::PA_STREAM_AUTO_TIMING_UPDATE;
inline constexpr auto stream_interpolate_timing = ::PA_STREAM_INTERPOLATE_TIMING;
inline constexpr auto stream_start_corked       = ::PA_STREAM_START_CORKED;
inline constexpr auto seek_relative             = ::PA_SEEK_RELATIVE;
inline constexpr auto subscription_mask_sink    = ::PA_SUBSCRIPTION_MASK_SINK;
inline constexpr auto subscription_mask_server  = ::PA_SUBSCRIPTION_MASK_SERVER;

inline constexpr const char* prop_application_name = PA_PROP_APPLICATION_NAME;
inline constexpr const char* prop_application_id   = PA_PROP_APPLICATION_ID;
inline constexpr const char* prop_media_name       = PA_PROP_MEDIA_NAME;
inline constexpr const char* prop_media_role       = PA_PROP_MEDIA_ROLE;

struct Api {
    decltype(&::pa_mainloop_api_once)           pa_mainloop_api_once {};
    decltype(&::pa_threaded_mainloop_free)      pa_threaded_mainloop_free {};
    decltype(&::pa_threaded_mainloop_get_api)   pa_threaded_mainloop_get_api {};
    decltype(&::pa_threaded_mainloop_in_thread) pa_threaded_mainloop_in_thread {};
    decltype(&::pa_threaded_mainloop_lock)      pa_threaded_mainloop_lock {};
    decltype(&::pa_threaded_mainloop_new)       pa_threaded_mainloop_new {};
    decltype(&::pa_threaded_mainloop_signal)    pa_threaded_mainloop_signal {};
    decltype(&::pa_threaded_mainloop_start)     pa_threaded_mainloop_start {};
    decltype(&::pa_threaded_mainloop_stop)      pa_threaded_mainloop_stop {};
    decltype(&::pa_threaded_mainloop_unlock)    pa_threaded_mainloop_unlock {};
    decltype(&::pa_threaded_mainloop_wait)      pa_threaded_mainloop_wait {};

    decltype(&::pa_context_connect)                pa_context_connect {};
    decltype(&::pa_context_disconnect)             pa_context_disconnect {};
    decltype(&::pa_context_errno)                  pa_context_errno {};
    decltype(&::pa_context_get_server_info)        pa_context_get_server_info {};
    decltype(&::pa_context_get_sink_info_by_name)  pa_context_get_sink_info_by_name {};
    decltype(&::pa_context_get_state)              pa_context_get_state {};
    decltype(&::pa_context_new)                    pa_context_new {};
    decltype(&::pa_context_new_with_proplist)      pa_context_new_with_proplist {};
    decltype(&::pa_context_set_state_callback)     pa_context_set_state_callback {};
    decltype(&::pa_context_set_subscribe_callback) pa_context_set_subscribe_callback {};
    decltype(&::pa_context_subscribe)              pa_context_subscribe {};
    decltype(&::pa_context_unref)                  pa_context_unref {};

    decltype(&::pa_operation_unref) pa_operation_unref {};
    decltype(&::pa_proplist_free)   pa_proplist_free {};
    decltype(&::pa_proplist_new)    pa_proplist_new {};
    decltype(&::pa_proplist_sets)   pa_proplist_sets {};

    decltype(&::pa_stream_begin_write)        pa_stream_begin_write {};
    decltype(&::pa_stream_cancel_write)       pa_stream_cancel_write {};
    decltype(&::pa_stream_connect_playback)   pa_stream_connect_playback {};
    decltype(&::pa_stream_connect_record)     pa_stream_connect_record {};
    decltype(&::pa_stream_cork)               pa_stream_cork {};
    decltype(&::pa_stream_disconnect)         pa_stream_disconnect {};
    decltype(&::pa_stream_drop)               pa_stream_drop {};
    decltype(&::pa_stream_get_latency)        pa_stream_get_latency {};
    decltype(&::pa_stream_get_state)          pa_stream_get_state {};
    decltype(&::pa_stream_get_time)           pa_stream_get_time {};
    decltype(&::pa_stream_get_timing_info)    pa_stream_get_timing_info {};
    decltype(&::pa_stream_is_corked)          pa_stream_is_corked {};
    decltype(&::pa_stream_flush)              pa_stream_flush {};
    decltype(&::pa_stream_new)                pa_stream_new {};
    decltype(&::pa_stream_new_with_proplist)  pa_stream_new_with_proplist {};
    decltype(&::pa_stream_peek)               pa_stream_peek {};
    decltype(&::pa_stream_readable_size)      pa_stream_readable_size {};
    decltype(&::pa_stream_set_buffer_attr)    pa_stream_set_buffer_attr {};
    decltype(&::pa_stream_set_read_callback)  pa_stream_set_read_callback {};
    decltype(&::pa_stream_set_state_callback) pa_stream_set_state_callback {};
    decltype(&::pa_stream_set_write_callback) pa_stream_set_write_callback {};
    decltype(&::pa_stream_unref)              pa_stream_unref {};
    decltype(&::pa_stream_update_timing_info) pa_stream_update_timing_info {};
    decltype(&::pa_stream_writable_size)      pa_stream_writable_size {};
    decltype(&::pa_stream_write)              pa_stream_write {};

    decltype(&::pa_channel_map_init_stereo) pa_channel_map_init_stereo {};
    decltype(&::pa_strerror)                pa_strerror {};
};

auto load() noexcept -> const Api*;
auto load_error() noexcept -> rstd::ref<rstd::str>;
auto context_is_good(pa_context_state_t state) noexcept -> bool;
auto stream_is_good(pa_stream_state_t state) noexcept -> bool;

} // namespace wavsen::ffi::pulse
