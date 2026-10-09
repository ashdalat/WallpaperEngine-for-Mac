module;
extern "C" {
#include <errno.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
#include <libswresample/swresample.h>
}
namespace wavsen_audio_constants
{
inline constexpr int k_AV_DICT_IGNORE_SUFFIX = AV_DICT_IGNORE_SUFFIX;
inline constexpr int k_AVIO_SEEKABLE_NORMAL  = AVIO_SEEKABLE_NORMAL;
inline constexpr int k_AVERROR_EXIT          = AVERROR_EXIT;
inline constexpr int k_EIO                   = EIO;

inline constexpr int     k_AV_TIME_BASE             = AV_TIME_BASE;
inline constexpr int     k_AV_ERROR_MAX_STRING_SIZE = AV_ERROR_MAX_STRING_SIZE;
inline constexpr int64_t k_AV_NOPTS_VALUE           = AV_NOPTS_VALUE;
inline constexpr int     k_AVERROR_EOF              = AVERROR_EOF;
inline constexpr int     k_EAGAIN                   = EAGAIN;
inline constexpr int     k_AVSEEK_FLAG_BACKWARD     = AVSEEK_FLAG_BACKWARD;
inline constexpr int     k_AVSEEK_FLAG_ANY          = AVSEEK_FLAG_ANY;
inline constexpr int     k_AVSEEK_SIZE              = AVSEEK_SIZE;
inline constexpr int     k_AVFMT_FLAG_CUSTOM_IO     = AVFMT_FLAG_CUSTOM_IO;
} // namespace wavsen_audio_constants
#undef AV_TIME_BASE
#undef AV_ERROR_MAX_STRING_SIZE
#undef AV_NOPTS_VALUE
#undef AVERROR_EOF
#undef EAGAIN
#undef AVSEEK_FLAG_BACKWARD
#undef AVSEEK_FLAG_ANY
#undef AVSEEK_SIZE
#undef AVFMT_FLAG_CUSTOM_IO
#undef AVERROR
#undef AV_DICT_IGNORE_SUFFIX
#undef AVIO_SEEKABLE_NORMAL
#undef AVERROR_EXIT
#undef EIO
export module wavsen.ffi.ffmpeg.audio;
export {
    inline constexpr int AV_DICT_IGNORE_SUFFIX = wavsen_audio_constants::k_AV_DICT_IGNORE_SUFFIX;
    inline constexpr int AVIO_SEEKABLE_NORMAL  = wavsen_audio_constants::k_AVIO_SEEKABLE_NORMAL;
    inline constexpr int AVERROR_EXIT          = wavsen_audio_constants::k_AVERROR_EXIT;
    inline constexpr int EIO                   = wavsen_audio_constants::k_EIO;
    inline constexpr int AV_TIME_BASE          = wavsen_audio_constants::k_AV_TIME_BASE;
    inline constexpr int AV_ERROR_MAX_STRING_SIZE =
        wavsen_audio_constants::k_AV_ERROR_MAX_STRING_SIZE;
    inline constexpr int64_t AV_NOPTS_VALUE       = wavsen_audio_constants::k_AV_NOPTS_VALUE;
    inline constexpr int     AVERROR_EOF          = wavsen_audio_constants::k_AVERROR_EOF;
    inline constexpr int     EAGAIN               = wavsen_audio_constants::k_EAGAIN;
    inline constexpr int     AVSEEK_FLAG_BACKWARD = wavsen_audio_constants::k_AVSEEK_FLAG_BACKWARD;
    inline constexpr int     AVSEEK_FLAG_ANY      = wavsen_audio_constants::k_AVSEEK_FLAG_ANY;
    inline constexpr int     AVSEEK_SIZE          = wavsen_audio_constants::k_AVSEEK_SIZE;
    inline constexpr int     AVFMT_FLAG_CUSTOM_IO = wavsen_audio_constants::k_AVFMT_FLAG_CUSTOM_IO;
    inline constexpr int     AVERROR(int error) noexcept { return -error; }
    using ::av_channel_layout_copy;
    using ::av_channel_layout_default;
    using ::av_channel_layout_uninit;
    using ::AV_CHANNEL_ORDER_UNSPEC;
    using ::AV_CODEC_ID_4XM;
    using ::AV_CODEC_ID_AAC;
    using ::AV_CODEC_ID_AAC_LATM;
    using ::AV_CODEC_ID_AC3;
    using ::AV_CODEC_ID_ADPCM_SWF;
    using ::AV_CODEC_ID_AMR_NB;
    using ::AV_CODEC_ID_AMR_WB;
    using ::AV_CODEC_ID_APNG;
    using ::AV_CODEC_ID_ASS;
    using ::AV_CODEC_ID_DTS;
    using ::AV_CODEC_ID_DVD_NAV;
    using ::AV_CODEC_ID_EAC3;
    using ::AV_CODEC_ID_FLAC;
    using ::AV_CODEC_ID_FLV1;
    using ::AV_CODEC_ID_GIF;
    using ::AV_CODEC_ID_GSM;
    using ::AV_CODEC_ID_H261;
    using ::AV_CODEC_ID_H263;
    using ::AV_CODEC_ID_ILBC;
    using ::AV_CODEC_ID_JACOSUB;
    using ::AV_CODEC_ID_JPEG2000;
    using ::AV_CODEC_ID_MJPEG;
    using ::AV_CODEC_ID_MP3;
    using ::AV_CODEC_ID_MPEG1VIDEO;
    using ::AV_CODEC_ID_OPUS;
    using ::AV_CODEC_ID_SRT;
    using ::AV_CODEC_ID_WEBP;
    using ::AV_CODEC_ID_WEBVTT;
    using ::av_dict_free;
    using ::av_dict_get;
    using ::av_dict_set;
    using ::av_dict_set_int;
    using ::av_find_best_stream;
    using ::av_frame_alloc;
    using ::av_frame_clone;
    using ::av_frame_free;
    using ::av_frame_ref;
    using ::av_frame_unref;
    using ::av_free;
    using ::av_freep;
    using ::av_malloc;
    using ::av_packet_alloc;
    using ::av_packet_free;
    using ::av_packet_unref;
    using ::av_read_frame;
    using ::AV_SAMPLE_FMT_FLT;
    using ::AV_SAMPLE_FMT_S16;
    using ::av_seek_frame;
    using ::av_strerror;
    using ::AVChannelLayout;
    using ::AVChannelOrder;
    using ::AVCodec;
    using ::avcodec_alloc_context3;
    using ::avcodec_find_decoder;
    using ::avcodec_find_decoder_by_name;
    using ::avcodec_flush_buffers;
    using ::avcodec_free_context;
    using ::avcodec_get_name;
    using ::avcodec_open2;
    using ::avcodec_parameters_to_context;
    using ::avcodec_receive_frame;
    using ::avcodec_send_packet;
    using ::AVCodecContext;
    using ::AVCodecID;
    using ::AVCodecParameters;
    using ::AVDictionary;
    using ::AVDictionaryEntry;
    using ::avformat_alloc_context;
    using ::avformat_close_input;
    using ::avformat_find_stream_info;
    using ::avformat_free_context;
    using ::avformat_open_input;
    using ::avformat_seek_file;
    using ::AVFormatContext;
    using ::AVFrame;
    using ::avio_alloc_context;
    using ::avio_context_free;
    using ::AVIOContext;
    using ::AVMEDIA_TYPE_AUDIO;
    using ::AVPacket;
    using ::AVRational;
    using ::AVStream;
    using ::swr_alloc_set_opts2;
    using ::swr_convert;
    using ::swr_free;
    using ::swr_get_out_samples;
    using ::swr_init;
    using ::SwrContext;

    namespace wavsen::ffi::ffmpeg
    {
    double av_q2d(AVRational value) noexcept;
    }
}
namespace wavsen::ffi::ffmpeg
{
double av_q2d(AVRational value) noexcept { return ::av_q2d(value); }
} // namespace wavsen::ffi::ffmpeg
