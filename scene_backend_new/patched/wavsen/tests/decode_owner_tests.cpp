#include <rstd/macro.hpp>

import rstd;
import wavsen.decode;
import wavsen.ffi.ffmpeg.owner;

using namespace rstd::prelude;
using namespace rstd::literals;
using wavsen::ffi::ffmpeg::AvOwner;

namespace
{
struct Resource {
    int releases {};
};
void release(Resource*& value) {
    ++value->releases;
    value = nullptr;
}
} // namespace

int main() {
    Resource first, second;
    {
        AvOwner<Resource, release> a(&first);
        AvOwner<Resource, release> b(&second);
        b = rstd::move(a);
        rstd_assert(! a && b.get() == &first);
        rstd_assert(second.releases == 1);
        auto c = rstd::move(b);
        rstd_assert(! b && c.get() == &first);
        c.reset();
        c.reset();
    }
    rstd_assert(first.releases == 1 && second.releases == 1);

    auto    directory = rstd::fs::TempDir::make("wavsen-thumbnail"_str).unwrap();
    auto    path      = rstd::path::PathBuf::from(directory.path()).join("image.ppm"_str);
    Vec<u8> bytes     = Vec<u8>::from("P6\n2 1\n255\n"_str.as_bytes());
    for (auto value : array<u8, 6> { u8(255), u8(), u8(), u8(), u8(255), u8() }) bytes.push(value);
    rstd::fs::write(path.as_path(), bytes.as_slice()).unwrap();
    auto image = wavsen::decode::extract_thumbnail(path.as_path().to_str().unwrap(), {});
    rstd_assert(image.is_ok());
    rstd_assert(image->width == u32(2) && image->height == u32(1));
    rstd_assert(image->data.len() == usize(8));
    rstd_assert(image->data[usize()] == 255 && image->data[usize(1)] == 0);
    rstd_assert(image->data[usize(4)] == 0 && image->data[usize(5)] == 255);
    rstd_assert(
        wavsen::decode::extract_thumbnail(path.as_path().to_str().unwrap(), { .max_edge = u32() })
            .is_err());
    rstd::fs::write(path.as_path(), "invalid"_str.as_bytes()).unwrap();
    rstd_assert(wavsen::decode::extract_thumbnail(path.as_path().to_str().unwrap(), {}).is_err());
}
