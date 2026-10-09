import rstd;
import wavsen.audio;

using namespace rstd::prelude;

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

auto decoded_frames(f64 rate) -> u64 {
    auto                         cursor = rstd::io::Cursor<Vec<u8>>(pcm_wav());
    auto                         stream = wavsen::audio::ByteStream::make(rstd::move(cursor));
    wavsen::audio::StreamDecoder decoder;
    if (! decoder.open(
            rstd::move(stream),
            wavsen::audio::DeviceDesc { .channels = u32(2), .sample_rate = u32(48000) })) {
        return u64();
    }
    if (! decoder.set_playback_rate(rate)) return u64();

    rstd::array<float, 2048> samples {};
    u64                      total;
    for (u32 iteration; iteration < u32(200); ++iteration) {
        const auto count = decoder.next_pcm(samples.data(), u32(1024));
        total += count;
        if (count == u64() && decoder.is_eof()) break;
    }
    return total;
}

} // namespace

int main() {
    const auto normal  = decoded_frames(f64(1.0));
    const auto fast    = decoded_frames(f64(2.0));
    const auto fastest = decoded_frames(f64(4.0));
    const auto slow    = decoded_frames(f64(0.5));
    if (normal < u64(47000) || normal > u64(49000)) return 1;
    if (fast * u64(100) < normal * u64(45) || fast * u64(100) > normal * u64(55)) return 2;
    if (fastest * u64(100) < normal * u64(20) || fastest * u64(100) > normal * u64(30)) return 3;
    if (slow * u64(100) < normal * u64(190) || slow * u64(100) > normal * u64(210)) return 4;

    wavsen::audio::StreamDecoder decoder;
    if (decoder.set_playback_rate(f64())) return 5;
    if (decoder.set_playback_rate(f64::NAN_)) return 6;
    if (decoder.set_playback_rate(f64(-1.0))) return 7;
    if (decoder.set_playback_rate(f64::INFINITY_)) return 8;
    return 0;
}
