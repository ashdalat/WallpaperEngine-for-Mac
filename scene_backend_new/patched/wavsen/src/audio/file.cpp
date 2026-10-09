module wavsen.audio;
import :file;
import rstd;
import :byte_stream;
import wavsen.audio.core;
import :mixer;
import wavsen.ffi.ffmpeg.audio;

using namespace rstd::prelude;
using namespace rstd::literals;
namespace wavsen::audio
{
namespace
{
auto text(const char* value) -> String {
    return String::make(rstd::ffi::CStr::from_ptr(value).to_str().unwrap_or(""_str));
}
auto mime_type(AVCodecID codec) -> String {
    switch (codec) {
    case AV_CODEC_ID_4XM: return String::make("audio/x-adpcm"_str);
    case AV_CODEC_ID_AAC: return String::make("audio/aac"_str);
    case AV_CODEC_ID_AC3: return String::make("audio/x-ac3"_str);
    case AV_CODEC_ID_AMR_NB: return String::make("audio/amr"_str);
    case AV_CODEC_ID_AMR_WB: return String::make("audio/amr"_str);
    case AV_CODEC_ID_APNG: return String::make("image/png"_str);
    case AV_CODEC_ID_ASS: return String::make("text/x-ass"_str);
    case AV_CODEC_ID_DTS: return String::make("audio/x-dca"_str);
    case AV_CODEC_ID_DVD_NAV: return String::make("video/mpeg"_str);
    case AV_CODEC_ID_EAC3: return String::make("audio/x-eac3"_str);
    case AV_CODEC_ID_FLAC: return String::make("audio/x-flac"_str);
    case AV_CODEC_ID_FLV1: return String::make("video/x-flv"_str);
    case AV_CODEC_ID_GIF: return String::make("image/gif"_str);
    case AV_CODEC_ID_GSM: return String::make("audio/x-gsm"_str);
    case AV_CODEC_ID_H261: return String::make("video/x-h261"_str);
    case AV_CODEC_ID_H263: return String::make("video/x-h263"_str);
    case AV_CODEC_ID_ILBC: return String::make("audio/iLBC"_str);
    case AV_CODEC_ID_JACOSUB: return String::make("text/x-jacosub"_str);
    case AV_CODEC_ID_JPEG2000: return String::make("image/jpeg"_str);
    case AV_CODEC_ID_AAC_LATM: return String::make("audio/MP4A-LATM"_str);
    case AV_CODEC_ID_MJPEG: return String::make("video/x-mjpeg"_str);
    case AV_CODEC_ID_MPEG1VIDEO: return String::make("video/mpeg"_str);
    case AV_CODEC_ID_MP3: return String::make("audio/mpeg"_str);
    case AV_CODEC_ID_OPUS: return String::make("audio/ogg"_str);
    case AV_CODEC_ID_SRT: return String::make("application/x-subrip"_str);
    case AV_CODEC_ID_ADPCM_SWF: return String::make("application/x-shockwave-flash"_str);
    case AV_CODEC_ID_WEBP: return String::make("image/webp"_str);
    case AV_CODEC_ID_WEBVTT: return String::make("text/vtt"_str);
    default: return String::make("unknown/unknown"_str);
    }
}
} // namespace
class OpenedMedia::Impl {
public:
    ~Impl() {
        if (format) avformat_close_input(&format);
        if (avio) {
            av_freep(&avio->buffer);
            avio_context_free(&avio);
        }
    }
    bool       cancelled() const { return options.cancelled && options.cancelled(options.context); }
    static int interrupt(void* value) { return static_cast<Impl*>(value)->cancelled(); }
    static int read(void* value, rstd::uint8_t* bytes, int length) {
        auto& self = *static_cast<Impl*>(value);
        if (self.cancelled()) return AVERROR_EXIT;
        auto result = (*self.source)
                          ->read(rstd::as_u8_slice_mut(mut_ref<rstd::byte[]>::from_raw_parts(
                              reinterpret_cast<rstd::byte*>(bytes), usize(length))));
        if (result.is_err()) {
            self.read_error = true;
            return AVERROR(EIO);
        }
        auto count = rstd::move(result).unwrap();
        return count == usize() ? AVERROR_EOF : static_cast<int>(count.to_primitive());
    }
    static rstd::int64_t seek(void* value, rstd::int64_t offset, int whence) {
        auto& self = *static_cast<Impl*>(value);
        if (self.cancelled()) return AVERROR_EXIT;
        if (whence == AVSEEK_SIZE) return -1;
        auto from   = whence == 0   ? rstd::io::SeekFrom::from_start(u64(offset))
                      : whence == 1 ? rstd::io::SeekFrom::from_current(i64(offset))
                                    : rstd::io::SeekFrom::from_end(i64(offset));
        auto result = (*self.source)->seek(from);
        return result.is_err()
                   ? -1
                   : static_cast<rstd::int64_t>(rstd::move(result).unwrap().to_primitive());
    }
    auto open(const char* url) -> MediaError {
        format = avformat_alloc_context();
        if (! format) return { MediaErrorKind::Open, i32(-12) };
        format->interrupt_callback = { &interrupt, this };
        if (source.is_some()) {
            auto* buffer = static_cast<rstd::uint8_t*>(av_malloc(32768));
            if (! buffer) return { MediaErrorKind::Open, i32(-12) };
            avio = avio_alloc_context(buffer, 32768, 0, this, &read, nullptr, &seek);
            if (! avio) {
                av_free(buffer);
                return { MediaErrorKind::Open, i32(-12) };
            }
            format->pb = avio;
            format->flags |= AVFMT_FLAG_CUSTOM_IO;
        }
        AVDictionary* opts = nullptr;
        av_dict_set_int(
            &opts, "rw_timeout", options.timeout_ms.to_primitive() * rstd::int64_t(1000), 0);
        av_dict_set_int(
            &opts, "timeout", options.timeout_ms.to_primitive() * rstd::int64_t(1000), 0);
        if (options.reconnect) av_dict_set(&opts, "reconnect", "1", 0);
        int rc = avformat_open_input(&format, url, nullptr, &opts);
        av_dict_free(&opts);
        if (rc < 0)
            return { cancelled() ? MediaErrorKind::Cancelled : MediaErrorKind::Open, i32(rc) };
        rc = avformat_find_stream_info(format, nullptr);
        if (rc < 0)
            return { cancelled() ? MediaErrorKind::Cancelled : MediaErrorKind::Probe, i32(rc) };
        if (cancelled()) return { MediaErrorKind::Cancelled };
        if (format->duration != AV_NOPTS_VALUE && format->duration >= 0)
            info.duration_seconds = Some(f64(double(format->duration) / AV_TIME_BASE));
        info.seekable                = format->pb && (format->pb->seekable & AVIO_SEEKABLE_NORMAL);
        const AVDictionaryEntry* tag = nullptr;
        while ((tag = av_dict_get(format->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
            info.tags.push(MediaTag { text(tag->key), text(tag->value) });
        for (unsigned i = 0; i < format->nb_streams; ++i) {
            auto* params = format->streams[i]->codecpar;
            info.streams.push(MediaStreamInfo { i64(params->bit_rate > 0 ? params->bit_rate : 0),
                                                mime_type(params->codec_id) });
        }
        return {};
    }
    MediaOpenOptions   options;
    Option<ByteStream> source;
    AVFormatContext*   format {};
    AVIOContext*       avio {};
    MediaInfo          info;
    bool               read_error {};
};
OpenedMedia::OpenedMedia(): impl_(Box<Impl>::make()) {}
OpenedMedia::~OpenedMedia()                                         = default;
OpenedMedia::OpenedMedia(OpenedMedia&&) noexcept                    = default;
auto OpenedMedia::operator=(OpenedMedia&&) noexcept -> OpenedMedia& = default;
auto OpenedMedia::open(ref<str> url, MediaOpenOptions options) -> Result<OpenedMedia, MediaError> {
    auto name = rstd::ffi::CString::make(String::make(url));
    if (name.is_err()) return Err(MediaError { MediaErrorKind::InvalidInput });
    OpenedMedia media;
    media.impl_->options = options;
    auto error           = media.impl_->open(name.unwrap().as_ptr());
    if (error.kind != MediaErrorKind::None) return Err(error);
    return Ok(rstd::move(media));
}
auto OpenedMedia::open(ByteStream source, MediaOpenOptions options)
    -> Result<OpenedMedia, MediaError> {
    OpenedMedia media;
    media.impl_->options = options;
    media.impl_->source.insert(rstd::move(source));
    auto error = media.impl_->open(nullptr);
    if (error.kind != MediaErrorKind::None) return Err(error);
    return Ok(rstd::move(media));
}
auto OpenedMedia::info() const -> const MediaInfo& { return impl_->info; }

class StreamDecoder::Impl {
public:
    ~Impl() { teardown(); }
    auto fail(MediaErrorKind kind, int rc = 0) -> bool {
        error_ = { media_.impl_->cancelled() ? MediaErrorKind::Cancelled : kind, i32(rc) };
        return false;
    }
    bool open(OpenedMedia media, const DeviceDesc& target) {
        teardown();
        media_   = rstd::move(media);
        fmt_ctx_ = media_.impl_->format;
        if (! fmt_ctx_) return fail(MediaErrorKind::InvalidInput);
        target_              = target;
        const AVCodec* codec = nullptr;
        stream_idx_          = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (stream_idx_ < 0 || ! codec) return fail(MediaErrorKind::NoAudio, stream_idx_);
        cctx_ = avcodec_alloc_context3(codec);
        if (! cctx_) return fail(MediaErrorKind::Decode, -12);
        int rc = avcodec_parameters_to_context(cctx_, fmt_ctx_->streams[stream_idx_]->codecpar);
        if (rc < 0) return fail(MediaErrorKind::Decode, rc);
        cctx_->pkt_timebase = fmt_ctx_->streams[stream_idx_]->time_base;
        rc                  = avcodec_open2(cctx_, codec, nullptr);
        if (rc < 0) return fail(MediaErrorKind::Decode, rc);
        pkt_   = av_packet_alloc();
        frame_ = av_frame_alloc();
        if (! pkt_ || ! frame_) return fail(MediaErrorKind::Decode, -12);
        if (! setup_resampler()) return fail(MediaErrorKind::Resample);
        return true;
    }
    void retarget(const DeviceDesc& target) {
        if (! cctx_) {
            target_ = target;
            return;
        }
        if (target.channels == target_.channels && target.sample_rate == target_.sample_rate)
            return;
        SwrContext* next = nullptr;
        if (! build_resampler(target, playback_rate_, &next)) {
            fail(MediaErrorKind::Resample);
            return;
        }
        if (swr_) swr_free(&swr_);
        swr_            = next;
        target_         = target;
        pending_offset_ = pending_frames_ = u32();
    }
    bool set_playback_rate(f64 rate) {
        if (! rate.is_finite() || rate <= f64()) return false;
        if (rate == playback_rate_) return true;
        if (cctx_) {
            SwrContext* next = nullptr;
            if (! build_resampler(target_, rate, &next)) return false;
            if (swr_) swr_free(&swr_);
            swr_ = next;
        }
        playback_rate_  = rate;
        pending_offset_ = pending_frames_ = u32();
        return true;
    }
    auto next_pcm(void* dst, u32 frames) -> u64 {
        if (! cctx_ || ! swr_ || error_.kind != MediaErrorKind::None) return u64();
        auto*      out = static_cast<rstd::uint8_t*>(dst);
        const auto bps = usize(sizeof(float)) * usize(target_.channels.to_primitive());
        u32        produced;
        batch_position_ = pcm_position_;
        while (produced < frames) {
            if (media_.impl_->cancelled()) {
                fail(MediaErrorKind::Cancelled);
                break;
            }
            if (pending_frames_ > u32()) {
                const auto take = (frames - produced).min(pending_frames_);
                rstd::mem::memcpy(out + (usize(produced.to_primitive()) * bps).to_primitive(),
                                  pending_buf_.data() +
                                      (usize(pending_offset_.to_primitive()) * bps).to_primitive(),
                                  usize(take.to_primitive()) * bps);
                pcm_position_ +=
                    f64(double(take.to_primitive()) / target_.sample_rate.to_primitive()) *
                    playback_rate_;
                produced += take;
                pending_offset_ += take;
                pending_frames_ -= take;
                continue;
            }
            if (eof_) break;
            if (! decoder_done_ && ! pull_frame()) {
                if (error_.kind != MediaErrorKind::None) break;
            }
            if (! clock_anchored_ && ! decoder_done_) {
                pcm_position_   = last_pts_seconds_;
                batch_position_ = pcm_position_;
                clock_anchored_ = true;
            }
            int maximum = swr_get_out_samples(swr_, decoder_done_ ? 0 : frame_->nb_samples);
            if (maximum < 1) maximum = 256;
            auto size = usize(maximum) * bps;
            if (pending_buf_.len() < size) pending_buf_.resize(size, 0u);
            auto* buffer = pending_buf_.data();
            int   count  = swr_convert(
                swr_,
                &buffer,
                maximum,
                decoder_done_ ? nullptr : const_cast<const rstd::uint8_t**>(frame_->extended_data),
                decoder_done_ ? 0 : frame_->nb_samples);
            if (count < 0) {
                fail(MediaErrorKind::Resample, count);
                break;
            }
            pending_offset_ = u32();
            pending_frames_ = u32(count);
            if (decoder_done_ && count == 0) eof_ = true;
        }
        return u64(produced.to_primitive());
    }
    bool pull_frame() {
        for (;;) {
            if (media_.impl_->cancelled()) return fail(MediaErrorKind::Cancelled);
            int rc = avcodec_receive_frame(cctx_, frame_);
            if (rc == 0) {
                auto pts = frame_->best_effort_timestamp;
                if (pts == AV_NOPTS_VALUE) pts = frame_->pts;
                if (pts != AV_NOPTS_VALUE)
                    last_pts_seconds_ =
                        f64(double(pts) *
                            ffi::ffmpeg::av_q2d(fmt_ctx_->streams[stream_idx_]->time_base));
                return true;
            }
            if (rc == AVERROR_EOF) {
                decoder_done_ = true;
                return false;
            }
            if (rc != AVERROR(EAGAIN)) return fail(MediaErrorKind::Decode, rc);
            if (input_done_) return fail(MediaErrorKind::Decode, rc);
            do {
                rc = av_read_frame(fmt_ctx_, pkt_);
                if (rc < 0) break;
                if (pkt_->stream_index == stream_idx_) break;
                av_packet_unref(pkt_);
            } while (true);
            if (rc == AVERROR_EOF && ! media_.impl_->read_error &&
                (! fmt_ctx_->pb || fmt_ctx_->pb->error >= 0 ||
                 fmt_ctx_->pb->error == AVERROR_EOF)) {
                input_done_ = true;
                rc          = avcodec_send_packet(cctx_, nullptr);
            } else if (rc < 0)
                return fail(MediaErrorKind::Read, rc);
            else {
                rc = avcodec_send_packet(cctx_, pkt_);
                av_packet_unref(pkt_);
            }
            if (rc < 0) return fail(MediaErrorKind::Decode, rc);
        }
    }
    bool seek_to(f64 seconds) {
        if (! fmt_ctx_ || stream_idx_ < 0 || ! seconds.is_finite() || seconds < f64()) return false;
        auto timestamp = static_cast<rstd::int64_t>(
            seconds.to_primitive() /
            ffi::ffmpeg::av_q2d(fmt_ctx_->streams[stream_idx_]->time_base));
        int rc = avformat_seek_file(fmt_ctx_, stream_idx_, timestamp, timestamp, timestamp, 0);
        if (rc < 0) return fail(MediaErrorKind::Seek, rc);
        if (fmt_ctx_->pb) fmt_ctx_->pb->error = 0;
        avcodec_flush_buffers(cctx_);
        if (! setup_resampler()) return fail(MediaErrorKind::Resample);
        pending_offset_ = pending_frames_ = u32();
        input_done_ = decoder_done_ = eof_ = false;
        media_.impl_->read_error           = false;
        error_                             = {};
        last_pts_seconds_ = pcm_position_ = batch_position_ = seconds;
        clock_anchored_                                     = false;
        return true;
    }
    bool build_resampler(const DeviceDesc& target, f64 rate, SwrContext** result) {
        const auto effective_input_rate = f64(cctx_->sample_rate) * rate;
        if (! effective_input_rate.is_finite() || effective_input_rate < f64(1.0) ||
            effective_input_rate > f64(i32::MAX.to_primitive())) {
            return false;
        }

        AVChannelLayout out_layout {};
        av_channel_layout_default(&out_layout, static_cast<int>(target.channels.to_primitive()));

        AVChannelLayout in_layout {};
        if (cctx_->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC) {
            av_channel_layout_copy(&in_layout, &cctx_->ch_layout);
        } else {
            av_channel_layout_default(&in_layout, cctx_->ch_layout.nb_channels);
        }

        SwrContext* next = nullptr;
        if (swr_alloc_set_opts2(&next,
                                &out_layout,
                                AV_SAMPLE_FMT_FLT,
                                static_cast<int>(target.sample_rate.to_primitive()),
                                &in_layout,
                                cctx_->sample_fmt,
                                static_cast<int>(effective_input_rate.to_primitive() + 0.5),
                                /*log_offset=*/0,
                                /*log_ctx=*/nullptr) < 0) {
            av_channel_layout_uninit(&out_layout);
            av_channel_layout_uninit(&in_layout);

            return false;
        }
        av_channel_layout_uninit(&out_layout);
        av_channel_layout_uninit(&in_layout);

        if (swr_init(next) < 0) {
            swr_free(&next);
            return false;
        }
        *result = next;
        return true;
    }

    bool setup_resampler() {
        SwrContext* next = nullptr;
        if (! build_resampler(target_, playback_rate_, &next)) return false;
        if (swr_) swr_free(&swr_);
        swr_ = next;
        return true;
    }

    void teardown() {
        if (swr_) swr_free(&swr_);
        if (frame_) av_frame_free(&frame_);
        if (pkt_) av_packet_free(&pkt_);
        if (cctx_) avcodec_free_context(&cctx_);
        fmt_ctx_        = nullptr;
        media_          = OpenedMedia();
        pending_offset_ = pending_frames_ = u32();
        input_done_ = decoder_done_ = eof_ = false;
        error_                             = {};
        last_pts_seconds_ = pcm_position_ = batch_position_ = f64();
        clock_anchored_                                     = false;
        playback_rate_                                      = f64(1.0);
    }
    OpenedMedia        media_;
    DeviceDesc         target_ {};
    AVFormatContext*   fmt_ctx_ {};
    AVCodecContext*    cctx_ {};
    SwrContext*        swr_ {};
    AVPacket*          pkt_ {};
    AVFrame*           frame_ {};
    int                stream_idx_ { -1 };
    Vec<rstd::uint8_t> pending_buf_;
    u32                pending_offset_, pending_frames_;
    bool               input_done_ {}, decoder_done_ {}, eof_ {};
    MediaError         error_;
    f64                last_pts_seconds_, pcm_position_, batch_position_;
    bool               clock_anchored_ {};
    f64                playback_rate_ { 1.0 };
};
StreamDecoder::StreamDecoder(): impl_(Box<Impl>::make()) {}
StreamDecoder::~StreamDecoder()                                   = default;
StreamDecoder::StreamDecoder(StreamDecoder&&) noexcept            = default;
StreamDecoder& StreamDecoder::operator=(StreamDecoder&&) noexcept = default;
bool           StreamDecoder::open(ByteStream source, const DeviceDesc& target) {
    auto media = OpenedMedia::open(rstd::move(source));
    if (media.is_err()) {
        impl_->teardown();
        impl_->error_ = rstd::move(media).unwrap_err();
        return false;
    }
    return open(rstd::move(media).unwrap(), target);
}
bool StreamDecoder::open(OpenedMedia media, const DeviceDesc& target) {
    return impl_->open(rstd::move(media), target);
}
auto StreamDecoder::error() const -> MediaError { return impl_->error_; }
auto StreamDecoder::info() const -> const MediaInfo& { return impl_->media_.info(); }
void StreamDecoder::retarget(const DeviceDesc& target) { impl_->retarget(target); }
bool StreamDecoder::set_playback_rate(f64 rate) { return impl_->set_playback_rate(rate); }
auto StreamDecoder::playback_rate() const -> f64 { return impl_->playback_rate_; }
auto StreamDecoder::next_pcm(void* dst, u32 frames) -> u64 { return impl_->next_pcm(dst, frames); }
bool StreamDecoder::seek_to(f64 seconds) { return impl_->seek_to(seconds); }
auto StreamDecoder::current_pts_seconds() const -> f64 { return impl_->last_pts_seconds_; }
auto StreamDecoder::pcm_position_seconds() const -> f64 { return impl_->batch_position_; }
bool StreamDecoder::is_eof() const { return impl_->eof_; }
auto StreamDecoder::sample_rate() const -> u32 {
    return impl_->cctx_ ? u32(impl_->cctx_->sample_rate) : u32();
}
auto StreamDecoder::channels() const -> u32 {
    return impl_->cctx_ ? u32(impl_->cctx_->ch_layout.nb_channels) : u32();
}
namespace
{

// SoundStream backed by libav* decoder. Created via make_stream(); the
// caller pulls PCM synchronously; blocking sources require a worker.
class DecoderStream : public SoundStream {
public:
    explicit DecoderStream(StreamDecoder dec): dec_(rstd::move(dec)) {}

    auto next_pcm(void* dst, u32 frames) -> u64 override { return dec_.next_pcm(dst, frames); }
    void pass_desc(const Desc& d) override { dec_.retarget({ d.channels, d.sample_rate }); }

private:
    StreamDecoder dec_;
};

} // namespace

auto make_stream(ByteStream source, const SoundStream::Desc& desc)
    -> Option<Box<dyn<SoundStreamObject>>> {
    StreamDecoder dec;
    if (! dec.open(rstd::move(source), { desc.channels, desc.sample_rate })) {
        return None();
    }
    auto stream = Box<DecoderStream>::make(rstd::move(dec));
    return Some(Box<dyn<SoundStreamObject>>::from_raw(
        dyn<SoundStreamObject>::from_ptr(rstd::move(stream).into_raw())));
}

} // namespace wavsen::audio
