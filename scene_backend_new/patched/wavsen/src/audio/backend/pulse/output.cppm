export module wavsen.audio.backend.pulse.output;
export import wavsen.audio.backend;
import rstd;
using namespace rstd::prelude;
export namespace wavsen::audio::backend
{
class PulseOutput {
public:
    explicit PulseOutput(OutputSink sink);
    ~PulseOutput();
    auto dispatch(ControlFunction function, void* context) -> bool;
    auto open(const AudioClientIdentity& identity) -> Result<DeviceDesc, Error>;
    void set_playing(bool playing);
    void flush();
    void close();

private:
    class Impl;
    Box<Impl> impl_;
};
static_assert(rstd::mtp::check_trait<Output, PulseOutput>());
} // namespace wavsen::audio::backend
