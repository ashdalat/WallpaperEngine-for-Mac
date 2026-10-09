module;

#include <vulkan/vulkan.h>

extern "C" {
#include <errno.h>
#include <libavutil/avutil.h>
#include <libavutil/buffer.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/dict.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/hwcontext_vulkan.h>
#if defined(WAVSEN_HAS_VAAPI)
#    include <libavutil/hwcontext_vaapi.h>
#endif
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
}

namespace _wv_avutil
{
// AV_PIX_FMT_P010 / P016 are AV_PIX_FMT_NE-style endian-alias macros
// (NOT enumerators), so they must be captured + #undef'd like ints.
inline constexpr AVPixelFormat k_AV_PIX_FMT_P010 = AV_PIX_FMT_P010;
inline constexpr AVPixelFormat k_AV_PIX_FMT_P016 = AV_PIX_FMT_P016;
} // namespace _wv_avutil

#undef AV_TIME_BASE
#undef AV_ERROR_MAX_STRING_SIZE
#undef AV_NOPTS_VALUE
#undef AVERROR_EOF
#undef AVERROR
#undef EAGAIN
#undef AV_PIX_FMT_P010
#undef AV_PIX_FMT_P016

export module wavsen.ffi.ffmpeg:avutil;
export import wavsen.ffi.ffmpeg.audio;

export {
    inline constexpr AVPixelFormat AV_PIX_FMT_P010    = _wv_avutil::k_AV_PIX_FMT_P010;
    inline constexpr AVPixelFormat AV_PIX_FMT_P016    = _wv_avutil::k_AV_PIX_FMT_P016;

    using ::AV_VK_FRAME_FLAG_DISABLE_MULTIPLANE;
    using ::AV_VK_FRAME_FLAG_NONE;
    using ::AVBufferRef;
    using ::AVChannel;
    using ::AVColorRange;
    using ::AVColorSpace;
    using ::AVHWDeviceContext;
    using ::AVHWDeviceType;
    using ::AVHWFramesContext;
    using ::AVMediaType;
    using ::AVPixelFormat;
    using ::AVPixFmtDescriptor;
    using ::AVSampleFormat;
    using ::AVVkFrame;
    using ::AVVkFrameFlags;
    using ::AVVulkanDeviceContext;
    using ::AVVulkanDeviceQueueFamily;
    using ::AVVulkanFramesContext;
#if defined(WAVSEN_HAS_VAAPI)
    using ::AVVAAPIDeviceContext;
    using ::AVVAAPIFramesContext;
#endif
    using ::AVDRMDeviceContext;
    using ::AVDRMFrameDescriptor;
    using ::AVDRMLayerDescriptor;
    using ::AVDRMObjectDescriptor;
    using ::AVDRMPlaneDescriptor;
    inline constexpr int AV_DRM_MAX_PLANES_C = ::AV_DRM_MAX_PLANES;

    using ::AVMEDIA_TYPE_DATA;
    using ::AVMEDIA_TYPE_SUBTITLE;
    using ::AVMEDIA_TYPE_UNKNOWN;
    using ::AVMEDIA_TYPE_VIDEO;

    using ::AV_PIX_FMT_BGRA;
    using ::AV_PIX_FMT_NONE;
    using ::AV_PIX_FMT_NV12;
    using ::AV_PIX_FMT_RGBA;
    using ::AV_PIX_FMT_VULKAN;
    using ::AV_PIX_FMT_YUV420P;
#if defined(WAVSEN_HAS_VAAPI)
    using ::AV_PIX_FMT_VAAPI;
#endif
    using ::AV_PIX_FMT_DRM_PRIME;

    using ::AVCOL_RANGE_JPEG;
    using ::AVCOL_RANGE_MPEG;
    using ::AVCOL_SPC_BT2020_CL;
    using ::AVCOL_SPC_BT2020_NCL;
    using ::AVCOL_SPC_BT470BG;
    using ::AVCOL_SPC_BT709;
    using ::AVCOL_SPC_SMPTE170M;

    using ::AV_HWDEVICE_TYPE_DRM;
    using ::AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
    using ::AV_HWDEVICE_TYPE_VULKAN;
#if defined(WAVSEN_HAS_VAAPI)
    using ::AV_HWDEVICE_TYPE_VAAPI;
#endif

    using ::AV_HWFRAME_MAP_DIRECT;
    using ::AV_HWFRAME_MAP_OVERWRITE;
    using ::AV_HWFRAME_MAP_READ;
    using ::AV_HWFRAME_MAP_WRITE;




    using ::av_buffer_alloc;
    using ::av_buffer_ref;
    using ::av_buffer_unref;


    using ::av_hwdevice_ctx_alloc;
    using ::av_hwdevice_ctx_create;
    using ::av_hwdevice_ctx_init;
    using ::av_hwframe_ctx_init;
    using ::av_hwframe_map;
    using ::av_hwframe_transfer_data;

    using ::av_get_pix_fmt_name;
    using ::av_pix_fmt_desc_get;


    using ::av_image_fill_arrays;
    using ::av_image_get_buffer_size;


}
