module;

#include <pulse/pulseaudio.h>

module wavsen.ffi.pulse;

import rstd;
import rstd.dlopn;

namespace wavsen::ffi::pulse
{
namespace
{

constexpr const char* kSoname = "libpulse.so.0";

struct LoadState {
    bool                               loaded {};
    rstd::Option<rstd::dlopn::Library> library {};
    Api                                api {};
    rstd::string::String               error {};
};

constinit rstd::sync::OnceLock<LoadState> g_state = rstd::sync::OnceLock<LoadState>::make();

void load_once(LoadState& state) {
    auto library = rstd::dlopn::Library::open(rstd::ffi::CStr::from_ptr(kSoname));
    if (library.is_err()) {
        auto error  = rstd::move(library).unwrap_err_unchecked();
        state.error = rstd::string::String::make(error.message());
        return;
    }
    state.library.insert(rstd::move(library).unwrap_unchecked());

#define WAVSEN_LOAD(name)                                                                      \
    do {                                                                                       \
        auto loaded =                                                                          \
            state.library->symbol<decltype(state.api.name)>(rstd::ffi::CStr::from_ptr(#name)); \
        if (loaded.is_err()) {                                                                 \
            auto error = rstd::move(loaded).unwrap_err_unchecked();                            \
            state.error =                                                                      \
                rstd::format("missing symbol {} in {}: {}", #name, kSoname, error.message());  \
            return;                                                                            \
        }                                                                                      \
        state.api.name = rstd::move(loaded).unwrap_unchecked();                                \
    } while (false)

    WAVSEN_LOAD(pa_mainloop_api_once);
    WAVSEN_LOAD(pa_threaded_mainloop_free);
    WAVSEN_LOAD(pa_threaded_mainloop_get_api);
    WAVSEN_LOAD(pa_threaded_mainloop_in_thread);
    WAVSEN_LOAD(pa_threaded_mainloop_lock);
    WAVSEN_LOAD(pa_threaded_mainloop_new);
    WAVSEN_LOAD(pa_threaded_mainloop_signal);
    WAVSEN_LOAD(pa_threaded_mainloop_start);
    WAVSEN_LOAD(pa_threaded_mainloop_stop);
    WAVSEN_LOAD(pa_threaded_mainloop_unlock);
    WAVSEN_LOAD(pa_threaded_mainloop_wait);
    WAVSEN_LOAD(pa_context_connect);
    WAVSEN_LOAD(pa_context_disconnect);
    WAVSEN_LOAD(pa_context_errno);
    WAVSEN_LOAD(pa_context_get_server_info);
    WAVSEN_LOAD(pa_context_get_sink_info_by_name);
    WAVSEN_LOAD(pa_context_get_state);
    WAVSEN_LOAD(pa_context_new);
    WAVSEN_LOAD(pa_context_new_with_proplist);
    WAVSEN_LOAD(pa_context_set_state_callback);
    WAVSEN_LOAD(pa_context_set_subscribe_callback);
    WAVSEN_LOAD(pa_context_subscribe);
    WAVSEN_LOAD(pa_context_unref);
    WAVSEN_LOAD(pa_operation_unref);
    WAVSEN_LOAD(pa_proplist_free);
    WAVSEN_LOAD(pa_proplist_new);
    WAVSEN_LOAD(pa_proplist_sets);
    WAVSEN_LOAD(pa_stream_begin_write);
    WAVSEN_LOAD(pa_stream_cancel_write);
    WAVSEN_LOAD(pa_stream_connect_playback);
    WAVSEN_LOAD(pa_stream_connect_record);
    WAVSEN_LOAD(pa_stream_cork);
    WAVSEN_LOAD(pa_stream_disconnect);
    WAVSEN_LOAD(pa_stream_drop);
    WAVSEN_LOAD(pa_stream_get_latency);
    WAVSEN_LOAD(pa_stream_get_state);
    WAVSEN_LOAD(pa_stream_get_time);
    WAVSEN_LOAD(pa_stream_get_timing_info);
    WAVSEN_LOAD(pa_stream_is_corked);
    WAVSEN_LOAD(pa_stream_flush);
    WAVSEN_LOAD(pa_stream_new);
    WAVSEN_LOAD(pa_stream_new_with_proplist);
    WAVSEN_LOAD(pa_stream_peek);
    WAVSEN_LOAD(pa_stream_readable_size);
    WAVSEN_LOAD(pa_stream_set_buffer_attr);
    WAVSEN_LOAD(pa_stream_set_read_callback);
    WAVSEN_LOAD(pa_stream_set_state_callback);
    WAVSEN_LOAD(pa_stream_set_write_callback);
    WAVSEN_LOAD(pa_stream_unref);
    WAVSEN_LOAD(pa_stream_update_timing_info);
    WAVSEN_LOAD(pa_stream_writable_size);
    WAVSEN_LOAD(pa_stream_write);
    WAVSEN_LOAD(pa_channel_map_init_stereo);
    WAVSEN_LOAD(pa_strerror);

#undef WAVSEN_LOAD
    state.loaded = true;
}

auto load_state() -> rstd::ref<LoadState> {
    return g_state.get_or_init([] {
        auto state = LoadState {};
        load_once(state);
        return state;
    });
}

} // namespace

auto load() noexcept -> const Api* {
    auto state = load_state();
    return state->loaded ? &state->api : nullptr;
}

auto load_error() noexcept -> rstd::ref<rstd::str> {
    auto state = load_state();
    return state->error.as_str();
}

auto context_is_good(pa_context_state_t state) noexcept -> bool {
    return PA_CONTEXT_IS_GOOD(state);
}

auto stream_is_good(pa_stream_state_t state) noexcept -> bool { return PA_STREAM_IS_GOOD(state); }

} // namespace wavsen::ffi::pulse
