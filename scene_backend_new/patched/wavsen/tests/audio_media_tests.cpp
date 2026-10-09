import rstd;
import wavsen.audio;
using namespace rstd::prelude;
using namespace rstd::literals;
using namespace wavsen::audio;
namespace
{
void push_u16(Vec<u8>& out, rstd::uint16_t value) {
    out.push(u8(value & 0xffu));
    out.push(u8((value >> 8u) & 0xffu));
}

void push_u32(Vec<u8>& out, rstd::uint32_t value) {
    push_u16(out, static_cast<rstd::uint16_t>(value & 0xffffu));
    push_u16(out, static_cast<rstd::uint16_t>(value >> 16u));
}

void push_tag(Vec<u8>& out, const char* value) {
    for (rstd::size_t index = 0; index < 4; ++index) {
        out.push(u8(static_cast<rstd::uint8_t>(value[index])));
    }
}

auto pcm_wav() -> Vec<u8> {
    constexpr rstd::uint32_t sample_rate = 48000;
    constexpr rstd::uint32_t data_size   = sample_rate * 2;
    auto                     bytes       = Vec<u8>::with_capacity(usize(44 + data_size));
    push_tag(bytes, "RIFF");
    push_u32(bytes, 36 + data_size);
    push_tag(bytes, "WAVE");
    push_tag(bytes, "fmt ");
    push_u32(bytes, 16);
    push_u16(bytes, 1);
    push_u16(bytes, 1);
    push_u32(bytes, sample_rate);
    push_u32(bytes, sample_rate * 2);
    push_u16(bytes, 2);
    push_u16(bytes, 16);
    push_tag(bytes, "data");
    push_u32(bytes, data_size);
    for (rstd::uint32_t index = 0; index < data_size; ++index) bytes.push(u8());
    return bytes;
}

bool cancel(void* value) noexcept {
    return static_cast<rstd::sync::atomic::Atomic<bool>*>(value)->load(
        rstd::sync::atomic::Ordering::Acquire);
}
} // namespace
int main(int argc, char** argv) {
    if (argc > 1) {
        const bool expect_error =
            argc > 2 && rstd::ffi::CStr::from_ptr(argv[2]).to_str().unwrap() == "error"_str;
        rstd::sync::atomic::Atomic<bool>        stop { false };
        Option<rstd::thread::JoinHandle<empty>> worker;
        if (argc > 2 && ! expect_error) {
            auto thread = rstd::thread::spawn([&] {
                rstd::thread::sleep(rstd::time::Duration::from_millis(u64(100)));
                stop.store(true, rstd::sync::atomic::Ordering::Release);
                return empty {};
            });
            if (thread.is_err()) return 30;
            worker = Some(rstd::move(thread).unwrap());
        }
        auto opened = OpenedMedia::open(
            rstd::ffi::CStr::from_ptr(argv[1]).to_str().unwrap(),
            { .context = &stop, .cancelled = &cancel, .reconnect = ! expect_error });
        if (worker.is_some()) {
            (void)rstd::move(worker.take().unwrap()).join();
            return opened.is_err() && opened.unwrap_err().kind == MediaErrorKind::Cancelled ? 0
                                                                                            : 31;
        }
        if (opened.is_err()) return 32;
        StreamDecoder decoder;
        if (! decoder.open(rstd::move(opened).unwrap(), { u32(2), u32(48000) })) return 33;
        rstd::array<float, 2048> output {};
        u64                      count;
        for (int i = 0; i < 1000 && ! decoder.is_eof(); ++i) {
            count += decoder.next_pcm(output.data(), u32(1024));
            if (decoder.error().kind != MediaErrorKind::None) return expect_error ? 0 : 34;
        }
        return ! expect_error && count > u64() && decoder.is_eof() ? 0 : 35;
    }
    auto stream =
        make_stream(ByteStream::make(rstd::io::Cursor<Vec<u8>>(pcm_wav())), { u32(2), u32(48000) });
    if (stream.is_none()) return 40;
    rstd::array<float, 2048> stream_output {};
    if ((*stream)->stream().next_pcm(stream_output.data(), u32(1024)) != u64(1024)) return 41;
    stream = None();
    if (make_stream(ByteStream::make(rstd::io::Cursor<Vec<u8>>(Vec<u8>())), { u32(2), u32(48000) })
            .is_some())
        return 42;
    StreamDecoder invalid_decoder;
    if (invalid_decoder.open(OpenedMedia(), { u32(2), u32(48000) })) return 14;
    if (invalid_decoder.error().kind != MediaErrorKind::InvalidInput) return 15;
    auto cursor = rstd::io::Cursor<Vec<u8>>(pcm_wav());
    auto opened = OpenedMedia::open(ByteStream::make(rstd::move(cursor)));
    if (opened.is_err()) return 1;
    auto media = rstd::move(opened).unwrap();
    if (media.info().duration_seconds.is_none()) return 2;
    if (! media.info().seekable || media.info().streams.len() != usize(1)) return 3;
    StreamDecoder decoder;
    if (! decoder.open(rstd::move(media), { u32(2), u32(44100) })) return 4;
    rstd::array<float, 2048> output {};
    u64                      total;
    for (int i = 0; i < 100 && ! decoder.is_eof(); ++i) {
        total += decoder.next_pcm(output.data(), u32(1024));
        if (decoder.error().kind != MediaErrorKind::None) return 5;
    }
    if (! decoder.is_eof() || total != u64(44100)) return 6;
    if (! decoder.seek_to(f64())) return 7;
    if (decoder.next_pcm(output.data(), u32(1024)) != u64(1024)) return 8;
    if (decoder.pcm_position_seconds() != f64()) return 9;
    if (! decoder.seek_to(f64(0.5))) return 10;
    if (decoder.next_pcm(output.data(), u32(1024)) == u64()) return 11;
    auto invalid = OpenedMedia::open(ByteStream::make(rstd::io::Cursor<Vec<u8>>(Vec<u8>())));
    if (invalid.is_ok()) return 12;
    rstd::sync::atomic::Atomic<bool> stopped { true };
    auto cancelled = OpenedMedia::open(ByteStream::make(rstd::io::Cursor<Vec<u8>>(pcm_wav())),
                                       { .context = &stopped, .cancelled = &cancel });
    if (cancelled.is_ok() || cancelled.unwrap_err().kind != MediaErrorKind::Cancelled) return 13;
    return 0;
}
