export module wavsen.audio.backend.pulse.capture;

export import wavsen.audio.backend;
import rstd;

using namespace rstd::prelude;

export namespace wavsen::audio::backend
{
class PulseCapture {
public:
    explicit PulseCapture(CaptureSink sink);
    ~PulseCapture();
    auto start() -> Result<DeviceDesc, Error>;
    void stop();
    auto is_running() const -> bool;

private:
    class Impl;
    Box<Impl> impl_;
};
static_assert(rstd::mtp::check_trait<Capture, PulseCapture>());
} // namespace wavsen::audio::backend
