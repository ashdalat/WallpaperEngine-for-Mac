export module wavsen.audio:byte_stream;

import rstd;

export namespace wavsen::audio
{

using ByteStream = rstd::io::ReadSeekHandle;

inline auto open_file(rstd::ref<rstd::path::Path> path) -> rstd::io::Result<ByteStream> {
    auto file = rstd::fs::File::open(path);
    if (file.is_err()) return rstd::Err(rstd::move(file).unwrap_err_unchecked());
    return rstd::Ok(ByteStream::make(rstd::move(file).unwrap_unchecked()));
}

} // namespace wavsen::audio
