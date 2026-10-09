export module wavsen.audio.backend.coreaudio.output;
export import wavsen.audio.backend;
import rstd;
using namespace rstd::prelude;
export namespace wavsen::audio::backend
{
class CoreAudioOutput {
public:
    explicit CoreAudioOutput(OutputSink sink);
    ~CoreAudioOutput();
    auto dispatch(ControlFunction function, void* context) -> bool;
    auto open(const AudioClientIdentity& identity) -> Result<DeviceDesc, Error>;
    void set_playing(bool playing);
    void flush();
    void close();

private:
    class Impl;
    Box<Impl> impl_;
};
static_assert(rstd::mtp::check_trait<Output, CoreAudioOutput>());
} // namespace wavsen::audio::backend
