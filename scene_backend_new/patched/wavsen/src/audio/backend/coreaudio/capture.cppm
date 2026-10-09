export module wavsen.audio.backend.coreaudio.capture;

export import wavsen.audio.backend;
import rstd;

using namespace rstd::prelude;

export namespace wavsen::audio::backend
{
class CoreAudioCapture {
public:
    explicit CoreAudioCapture(CaptureSink sink);
    ~CoreAudioCapture();
    auto start() -> Result<DeviceDesc, Error>;
    void stop();
    auto is_running() const -> bool;

private:
    class Impl;
    Box<Impl> impl_;
};
static_assert(rstd::mtp::check_trait<Capture, CoreAudioCapture>());
} // namespace wavsen::audio::backend
