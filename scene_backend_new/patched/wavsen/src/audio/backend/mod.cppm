export module wavsen.audio.backend;

export import wavsen.audio.core;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace wavsen::audio::backend
{

enum class ErrorKind
{
    Unavailable,
    UnsupportedSystem,
    NativeFailure,
    InvalidFormat
};

struct Error {
    ErrorKind kind { ErrorKind::NativeFailure };
    i32       native_code {};
};

auto error_name(ErrorKind kind) -> ref<str> {
    switch (kind) {
    case ErrorKind::Unavailable: return "backend unavailable"_str;
    case ErrorKind::UnsupportedSystem: return "CoreAudio capture requires macOS 14.2 or newer"_str;
    case ErrorKind::InvalidFormat: return "unsupported PCM format (requires 48 kHz float32)"_str;
    case ErrorKind::NativeFailure: return "native audio operation failed"_str;
    }
    return "native audio operation failed"_str;
}

struct CaptureSink {
    void* context {};
    void (*ingest)(void*, const float*, rstd::uint32_t, rstd::uint32_t) noexcept {};
    void (*discontinuity)(void*) noexcept {};
};

struct Capture {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Capture;
        auto start() -> Result<DeviceDesc, Error> { return rstd::trait_call<0>(this); }
        void stop() { rstd::trait_call<1>(this); }
        auto is_running() const -> bool { return rstd::trait_call<2>(this); }
    };
    template<typename T>
    using Funcs = rstd::TraitFuncs<&T::start, &T::stop, &T::is_running>;
};

enum class OutputState
{
    Ready,
    Playing,
    Paused,
    Flushed,
    Failed
};

struct OutputSink {
    void* context {};
    void (*render)(void*, float*, rstd::uint32_t) noexcept {};
    void (*changed)(void*, OutputState, Error) {};
    void (*position)(void*, u64) noexcept {};
};

using ControlFunction = void (*)(void*);

struct Output {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Output;
        auto dispatch(ControlFunction function, void* context) -> bool {
            return rstd::trait_call<0>(this, function, context);
        }
        auto open(const AudioClientIdentity& identity) -> Result<DeviceDesc, Error> {
            return rstd::trait_call<1>(this, identity);
        }
        void set_playing(bool playing) { rstd::trait_call<2>(this, playing); }
        void flush() { rstd::trait_call<3>(this); }
        void close() { rstd::trait_call<4>(this); }
    };
    template<typename T>
    using Funcs = rstd::TraitFuncs<&T::dispatch, &T::open, &T::set_playing, &T::flush, &T::close>;
};

} // namespace wavsen::audio::backend
