module wavsen.video;

import rstd;
import rstd.log;
import vvk;
import :vk_device;
import :video_decoder;
import wavsen.ffi.ffmpeg;
import wavsen.ffi.ffmpeg.owner;
#if defined(__APPLE__)
import wavsen.ffi.corefoundation;
import wavsen.ffi.corevideo;
#endif

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;

namespace wavsen::video
{

namespace
{

using ffi::ffmpeg::BufRefPtr;
using ffi::ffmpeg::CodecCtxPtr;
using ffi::ffmpeg::FmtCtxPtr;
using ffi::ffmpeg::FramePtr;
using ffi::ffmpeg::PacketPtr;
using ffi::ffmpeg::SwsPtr;

/* Defined further down — forward-declared so the helpers above the
 * definitions can use them. */
bool fail(Error* err, ref<str> message);
bool fail(Error* err, String message);
auto av_err_str(int rc) -> String;

/* Translate FFmpeg's colorspace/range enums into our ColorSpace /
 * ColorRange ints (which the public Nv12Frame / VkFrameView carry).
 * Unknowns default to BT.709 limited — the most common case. */
u32 map_colorspace(int cs) {
    switch (cs) {
    case AVCOL_SPC_BT709: return u32();
    case AVCOL_SPC_BT470BG: // PAL / BT.601 625
    case AVCOL_SPC_SMPTE170M: return u32(1);
    case AVCOL_SPC_BT2020_NCL: return u32(2);
    case AVCOL_SPC_BT2020_CL: return u32(2);
    default: return u32();
    }
}
u32 map_range(int r) { return r == AVCOL_RANGE_JPEG ? u32(1) : u32(); }

/* `get_format` callback: prefer AV_PIX_FMT_VULKAN whenever the codec
 * offers it; fall back to whatever FFmpeg picks by default otherwise.
 *
 * This is the moment to bootstrap hw_frames_ctx — get_format fires
 * during avcodec_open2 with avctx->internal already allocated, so
 * avcodec_get_hw_frames_parameters (which derefs internal in
 * ff_decode_get_hw_frames_ctx) is safe to call. Calling it earlier
 * (between avcodec_alloc_context3 and avcodec_open2) segfaults.
 *
 * A rejected Vulkan frame configuration returns AV_PIX_FMT_NONE so
 * build_internal fails this decoder attempt and the outer trial loop
 * can rebuild it for the next backend. */
AVPixelFormat get_format_prefer_vulkan(AVCodecContext* cctx, const AVPixelFormat* fmts) {
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p != AV_PIX_FMT_VULKAN) continue;

        AVBufferRef* hw_frames = nullptr;
        int          rc        = avcodec_get_hw_frames_parameters(
            cctx, cctx->hw_device_ctx, AV_PIX_FMT_VULKAN, &hw_frames);
        if (rc < 0 || ! hw_frames) {
            rstd::log::warn("get_format_prefer_vulkan: avcodec_get_hw_frames_parameters "
                            "failed ({}); rejecting this Vulkan decode attempt.",
                            av_err_str(rc).as_str());
            if (hw_frames) av_buffer_unref(&hw_frames);
            return AV_PIX_FMT_NONE;
        }
        auto* fc = reinterpret_cast<AVHWFramesContext*>(hw_frames->data);
        if (int irc = avcodec_prepare_vulkan_decode_frames(hw_frames); irc < 0) {
            rstd::log::warn("get_format_prefer_vulkan: Vulkan decode frame preparation "
                            "failed ({}); rejecting this Vulkan decode attempt.",
                            av_err_str(irc).as_str());
            av_buffer_unref(&hw_frames);
            return AV_PIX_FMT_NONE;
        }
        if (cctx->hw_frames_ctx) av_buffer_unref(&cctx->hw_frames_ctx);
        cctx->hw_frames_ctx = hw_frames;
        rstd::log::info("get_format_prefer_vulkan: AV_PIX_FMT_VULKAN selected (sw_format={}).",
                        av_get_pix_fmt_name(fc->sw_format));
        return AV_PIX_FMT_VULKAN;
    }
    return avcodec_default_get_format(cctx, fmts);
}

#if defined(WAVSEN_HAS_VAAPI)
/* VAAPI counterpart: prefer AV_PIX_FMT_VAAPI when offered. The codec
 * bootstraps an internal AVHWFramesContext from the AVHWDeviceContext
 * we attached to cctx->hw_device_ctx, so we don't pre-allocate frames
 * either. */
AVPixelFormat get_format_prefer_vaapi(AVCodecContext* cctx, const AVPixelFormat* fmts) {
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == AV_PIX_FMT_VAAPI) return AV_PIX_FMT_VAAPI;
    }
    return avcodec_default_get_format(cctx, fmts);
}

/* Best-effort AV_HWDEVICE_TYPE_VAAPI context. FFmpeg owns the libva
 * VADisplay; we hand it the selected Vulkan device's render-node path.
 * Returns NULL on any failure with *err populated. */
AVBufferRef* make_vaapi_hwdevice(const String& render_node, Error* err) {
    if (render_node.is_empty()) {
        fail(err, "VAAPI requires the selected Vulkan device's render node"_str);
        return nullptr;
    }
    AVBufferRef* hwd           = nullptr;
    auto         render_node_c = rstd::ffi::CString::make(render_node.clone()).unwrap();
    const char*  dev           = render_node_c.as_ptr();
    int          rc = av_hwdevice_ctx_create(&hwd, AV_HWDEVICE_TYPE_VAAPI, dev, nullptr, 0);
    if (rc < 0 || ! hwd) {
        fail(err,
             rstd::format("av_hwdevice_ctx_create(VAAPI, {}): {}",
                          render_node.as_str(),
                          av_err_str(rc).as_str()));
        if (hwd) av_buffer_unref(&hwd);
        return nullptr;
    }
    return hwd;
}
#endif

String resolve_render_node(const Producer& producer, const OpenOpts& opts) {
    if (! opts.render_node.is_empty()) return opts.render_node.clone();
    auto node = producer.drm_render_node();
    return node.is_some() ? rstd::move(node).unwrap() : String {};
}

const char* hw_accel_name(HwAccel accel) {
    switch (accel) {
    case HwAccel::Auto: return "auto";
    case HwAccel::Vulkan: return "vulkan";
    case HwAccel::Vaapi: return "vaapi";
    case HwAccel::VideoToolbox: return "videotoolbox";
    case HwAccel::None: return "none";
    }
    return "unknown";
}

/* Build an AV_HWDEVICE_TYPE_VULKAN context wrapping the caller's
 * Producer-owned VkInstance/VkDevice. Returns a populated AVBufferRef
 * on success, or null + populated *err on any failure. */
AVBufferRef* make_shared_vulkan_hwdevice(const Producer& vk, Error* err) {
    AVBufferRef* hwd = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
    if (! hwd) {
        fail(err, "av_hwdevice_ctx_alloc(VULKAN) failed"_str);
        return nullptr;
    }
    auto* dctx = reinterpret_cast<AVHWDeviceContext*>(hwd->data);
    auto* vctx = reinterpret_cast<AVVulkanDeviceContext*>(dctx->hwctx);

    vctx->get_proc_addr = vk.instance_dispatch().resolver;
    vctx->inst          = vk.instance();
    vctx->phys_dev      = vk.physical_device();
    vctx->act_dev       = vk.device();

    auto iexts                       = vk.enabled_instance_extensions();
    auto dexts                       = vk.enabled_device_extensions();
    vctx->enabled_inst_extensions    = iexts.is_empty() ? nullptr : iexts.as_raw_ptr();
    vctx->nb_enabled_inst_extensions = static_cast<int>(iexts.len().to_primitive());
    vctx->enabled_dev_extensions     = dexts.is_empty() ? nullptr : dexts.as_raw_ptr();
    vctx->nb_enabled_dev_extensions  = static_cast<int>(dexts.len().to_primitive());

    auto qfs    = vk.queue_families();
    vctx->nb_qf = 0;
    for (usize i {}; i < qfs.len(); ++i) {
        const auto& q = qfs[i];
        if (vctx->nb_qf >= static_cast<int>(sizeof(vctx->qf) / sizeof(vctx->qf[0]))) break;
        AVVulkanDeviceQueueFamily entry {};
        entry.idx   = static_cast<int>(q.index.to_primitive());
        entry.num   = 1;
        entry.flags = static_cast<VkQueueFlagBits>(q.flags);
        entry.video_caps =
            static_cast<VkVideoCodecOperationFlagBitsKHR>(q.video_caps.to_primitive());
        vctx->qf[vctx->nb_qf++] = entry;
    }

    if (int rc = av_hwdevice_ctx_init(hwd); rc < 0) {
        fail(err, rstd::format("av_hwdevice_ctx_init(shared VULKAN): {}", av_err_str(rc).as_str()));
        av_buffer_unref(&hwd);
        return nullptr;
    }
    return hwd;
}

bool fail(Error* err, ref<str> message) {
    if (err) err->message = String::make(message);
    return false;
}

bool fail(Error* err, String message) {
    if (err) err->message = rstd::move(message);
    return false;
}

auto av_err_str(int rc) -> String {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(rc, buf, sizeof(buf));
    return String::make(rstd::ffi::CStr::from_ptr(buf).to_str().unwrap());
}

#if defined(__APPLE__)
AVPixelFormat get_format_prefer_videotoolbox(AVCodecContext* cctx, const AVPixelFormat* fmts) {
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p != AV_PIX_FMT_VIDEOTOOLBOX) continue;

        /* Ask FFmpeg's VideoToolbox hwcontext for a BGRA pixel-buffer pool.
         * The modern AVHWDevice path uses sw_format here, not the old
         * AVVideotoolboxContext hook. FFmpeg exposes BGRA as a full-range RGB
         * surface; the source stream may be tagged as TV-range YUV, but that
         * range is consumed before the RGB surface is created. Keep the
         * original value for fallback and advertise RGB's full range while
         * initializing this pool. This removes the synchronous Metal
         * NV12->BGRA conversion from the render thread. */
        const auto source_range = cctx->color_range;
        cctx->color_range       = AVCOL_RANGE_JPEG;

        AVBufferRef* hw_frames = nullptr;
        const int    rc        = avcodec_get_hw_frames_parameters(
            cctx, cctx->hw_device_ctx, AV_PIX_FMT_VIDEOTOOLBOX, &hw_frames);
        if (rc < 0 || ! hw_frames) {
            rstd::log::warn("get_format_prefer_videotoolbox: BGRA frame-pool preparation "
                            "failed ({}); keeping VideoToolbox's default output format.",
                            av_err_str(rc).as_str());
            if (hw_frames) av_buffer_unref(&hw_frames);
            cctx->color_range = source_range;
            return AV_PIX_FMT_VIDEOTOOLBOX;
        }

        auto* fc          = reinterpret_cast<AVHWFramesContext*>(hw_frames->data);
        fc->sw_format     = AV_PIX_FMT_BGRA;
        const int init_rc = av_hwframe_ctx_init(hw_frames);
        if (init_rc < 0) {
            rstd::log::warn("get_format_prefer_videotoolbox: BGRA frame-pool initialization "
                            "failed ({}); keeping VideoToolbox's default output format.",
                            av_err_str(init_rc).as_str());
            av_buffer_unref(&hw_frames);
            cctx->color_range = source_range;
            return AV_PIX_FMT_VIDEOTOOLBOX;
        }

        if (cctx->hw_frames_ctx) av_buffer_unref(&cctx->hw_frames_ctx);
        cctx->hw_frames_ctx = hw_frames;
        rstd::log::info("get_format_prefer_videotoolbox: selected BGRA VideoToolbox frame pool.");
        return AV_PIX_FMT_VIDEOTOOLBOX;
    }
    return AV_PIX_FMT_NONE;
}

AVBufferRef* make_videotoolbox_hwdevice(Error* err) {
    AVBufferRef* hwd = nullptr;
    const int rc = av_hwdevice_ctx_create(&hwd, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0);
    if (rc < 0) {
        fail(err, rstd::format("create videotoolbox device: {}", av_err_str(rc).as_str()));
        if (hwd) av_buffer_unref(&hwd);
        return nullptr;
    }
    return hwd;
}
#endif

u64 next_decoder_generation() {
    static Atomic<u64> next { u64(1) };
    auto               value = next.fetch_add(u64(1), Ordering::Relaxed);
    if (value == u64()) value = next.fetch_add(u64(1), Ordering::Relaxed);
    return value;
}

} // namespace

struct VkFrameLease::State {
    FramePtr source;
};

struct VkFrameAccess::State {
    FramePtr               source;
    AVHWFramesContext*     frames { nullptr };
    AVVulkanFramesContext* vulkan { nullptr };
    AVVkFrame*             frame { nullptr };
};

void VkFrameAccess::reset() noexcept {
    if (! state_) return;
    auto state = Box<State>::from_raw(mut_ptr<State>::from_raw_parts(state_));
    state_     = nullptr;
    state->vulkan->unlock_frame(state->frames, state->frame);
}

VkFrameAccess::VkFrameAccess(VkFrameAccess&& other) noexcept
    : state_(rstd::exchange(other.state_, nullptr)), view_(rstd::move(other.view_)) {}

VkFrameAccess& VkFrameAccess::operator=(VkFrameAccess&& other) noexcept {
    if (this == &other) return *this;
    reset();
    state_ = rstd::exchange(other.state_, nullptr);
    view_  = rstd::move(other.view_);
    return *this;
}

VkFrameAccess::~VkFrameAccess() { reset(); }

void VkFrameAccess::clear_access(u32 plane) noexcept {
    state_->frame->access[plane.to_primitive()] = {};
}

void VkFrameLease::reset() noexcept {
    if (! state_) return;
    auto state = Box<State>::from_raw(mut_ptr<State>::from_raw_parts(state_));
    state_     = nullptr;
}

VkFrameLease::VkFrameLease(VkFrameLease&& other) noexcept
    : state_(rstd::exchange(other.state_, nullptr)), info_(rstd::move(other.info_)) {}

VkFrameLease& VkFrameLease::operator=(VkFrameLease&& other) noexcept {
    if (this == &other) return *this;
    reset();
    state_ = rstd::exchange(other.state_, nullptr);
    info_  = rstd::move(other.info_);
    return *this;
}

VkFrameLease::~VkFrameLease() { reset(); }

auto VkFrameLease::lock() const -> Result<VkFrameAccess, Error> {
    if (! state_ || ! state_->source || ! state_->source->hw_frames_ctx ||
        ! state_->source->data[0]) {
        return Err(Error("lock called on invalid Vulkan frame lease"_str));
    }

    FramePtr source(av_frame_clone(state_->source.get()));
    if (! source) return Err(Error("av_frame_clone(Vulkan access) failed"_str));

    auto* frames = reinterpret_cast<AVHWFramesContext*>(source->hw_frames_ctx->data);
    auto* vulkan = reinterpret_cast<AVVulkanFramesContext*>(frames->hwctx);
    auto* frame  = reinterpret_cast<AVVkFrame*>(source->data[0]);
    if (! vulkan || ! vulkan->lock_frame || ! vulkan->unlock_frame) {
        return Err(Error("Vulkan frame lock callbacks are unavailable"_str));
    }

    vulkan->lock_frame(frames, frame);
    auto access_state    = Box<VkFrameAccess::State>::make();
    access_state->source = rstd::move(source);
    access_state->frames = frames;
    access_state->vulkan = vulkan;
    access_state->frame  = frame;
    auto state_ptr       = rstd::move(access_state).into_raw().as_raw_ptr();
    return Ok(VkFrameAccess(state_ptr,
                            VkFrameView {
                                .img          = frame->img,
                                .layout       = frame->layout,
                                .sem          = frame->sem,
                                .sem_value    = frame->sem_value,
                                .queue_family = frame->queue_family,
                                .plane_count  = frame->img[1] != VK_NULL_HANDLE ? u32(2) : u32(1),
                                .width        = info_.width,
                                .height       = info_.height,
                                .pts_seconds  = info_.pts_seconds,
                                .colorspace   = info_.colorspace,
                                .color_range  = info_.color_range,
                                .bit_depth    = info_.bit_depth,
                            }));
}

struct DrmFrameLease::State {
    FramePtr source;
    FramePtr mapped;
};

struct VaapiFrameLease::State {
    FramePtr source;
};

struct AppleFrameLease::State {
#if defined(__APPLE__)
    ffi::corevideo::CVPixelBufferRef pixel_buffer {};

    ~State() {
        if (pixel_buffer) ffi::corefoundation::CFRelease(pixel_buffer);
    }
#endif
};

void DrmFrameLease::reset() noexcept {
    if (! state_) return;
    auto state = Box<State>::from_raw(mut_ptr<State>::from_raw_parts(state_));
    state_     = nullptr;
}

DrmFrameLease::DrmFrameLease(DrmFrameLease&& other) noexcept
    : state_(rstd::exchange(other.state_, nullptr)),
      view_(rstd::move(other.view_)),
      resource_key_(other.resource_key_) {}

DrmFrameLease& DrmFrameLease::operator=(DrmFrameLease&& other) noexcept {
    if (this == &other) return *this;
    reset();
    state_        = rstd::exchange(other.state_, nullptr);
    view_         = rstd::move(other.view_);
    resource_key_ = other.resource_key_;
    return *this;
}

DrmFrameLease::~DrmFrameLease() { reset(); }

void VaapiFrameLease::reset() noexcept {
    if (! state_) return;
    auto state = Box<State>::from_raw(mut_ptr<State>::from_raw_parts(state_));
    state_     = nullptr;
}

VaapiFrameLease::VaapiFrameLease(VaapiFrameLease&& other) noexcept
    : state_(rstd::exchange(other.state_, nullptr)),
      view_(rstd::move(other.view_)),
      resource_key_(other.resource_key_) {}

VaapiFrameLease& VaapiFrameLease::operator=(VaapiFrameLease&& other) noexcept {
    if (this == &other) return *this;
    reset();
    state_        = rstd::exchange(other.state_, nullptr);
    view_         = rstd::move(other.view_);
    resource_key_ = other.resource_key_;
    return *this;
}

VaapiFrameLease::~VaapiFrameLease() { reset(); }

void AppleFrameLease::reset() noexcept {
    if (! state_) return;
    auto state = Box<State>::from_raw(mut_ptr<State>::from_raw_parts(state_));
    state_     = nullptr;
}

AppleFrameLease::AppleFrameLease(AppleFrameLease&& other) noexcept
    : state_(rstd::exchange(other.state_, nullptr)), view_(rstd::move(other.view_)) {}

AppleFrameLease& AppleFrameLease::operator=(AppleFrameLease&& other) noexcept {
    if (this == &other) return *this;
    reset();
    state_ = rstd::exchange(other.state_, nullptr);
    view_  = rstd::move(other.view_);
    return *this;
}

AppleFrameLease::~AppleFrameLease() { reset(); }

auto VaapiFrameLease::into_drm() && -> Result<DrmFrameLease, Error> {
    if (! state_) return Err(Error("into_drm called on invalid VAAPI frame lease"_str));

    auto source_state = Box<State>::from_raw(mut_ptr<State>::from_raw_parts(state_));
    state_            = nullptr;

    FramePtr mapped(av_frame_alloc());
    if (! mapped) return Err(Error("av_frame_alloc(drm_frame) failed"_str));

    mapped->format       = AV_PIX_FMT_DRM_PRIME;
    const int map_result = av_hwframe_map(
        mapped.get(), source_state->source.get(), AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT);
    if (map_result < 0) {
        return Err(
            Error(rstd::format("av_hwframe_map(DRM_PRIME): {}", av_err_str(map_result).as_str())));
    }

    const auto* descriptor = reinterpret_cast<const AVDRMFrameDescriptor*>(mapped->data[0]);
    if (! descriptor) return Err(Error("av_hwframe_map: DRM_PRIME descriptor null"_str));

    DrmFrameView drm_view;
    const int    object_count = descriptor->nb_objects < 4 ? descriptor->nb_objects : 4;
    const int    layer_count  = descriptor->nb_layers < 4 ? descriptor->nb_layers : 4;
    drm_view.object_count     = u32(static_cast<rstd::uint32_t>(object_count));
    for (int object = 0; object < object_count; ++object) {
        drm_view.objects[object].fd              = descriptor->objects[object].fd;
        drm_view.objects[object].size            = u64(descriptor->objects[object].size);
        drm_view.objects[object].format_modifier = descriptor->objects[object].format_modifier;
    }
    drm_view.layer_count = u32(static_cast<rstd::uint32_t>(layer_count));
    for (int layer = 0; layer < layer_count; ++layer) {
        const auto& source_layer      = descriptor->layers[layer];
        auto&       destination_layer = drm_view.layers[layer];
        destination_layer.fourcc      = source_layer.format;
        const int plane_count         = source_layer.nb_planes < 4 ? source_layer.nb_planes : 4;
        destination_layer.plane_count = u32(static_cast<rstd::uint32_t>(plane_count));
        for (int plane = 0; plane < plane_count; ++plane) {
            destination_layer.planes[plane].object_index =
                u32(static_cast<rstd::uint32_t>(source_layer.planes[plane].object_index));
            destination_layer.planes[plane].offset = u64(source_layer.planes[plane].offset);
            destination_layer.planes[plane].pitch  = u64(source_layer.planes[plane].pitch);
        }
    }
    drm_view.width       = view_.width;
    drm_view.height      = view_.height;
    drm_view.pts_seconds = view_.pts_seconds;
    drm_view.colorspace  = view_.colorspace;
    drm_view.color_range = view_.color_range;
    drm_view.bit_depth   = view_.bit_depth;

    auto drm_state     = Box<DrmFrameLease::State>::make();
    drm_state->source  = rstd::move(source_state->source);
    drm_state->mapped  = rstd::move(mapped);
    auto drm_state_ptr = rstd::move(drm_state).into_raw().as_raw_ptr();
    return Ok(DrmFrameLease(drm_state_ptr, rstd::move(drm_view), resource_key_));
}

#if defined(__APPLE__)
struct ApplePrefetchState {
    Vec<AppleFramePull> frames;
    bool                stop { false };
    bool                error { false };
    String              error_message;
};
#endif

struct VideoDecoder::State {
    /* Custom-IO source (open_from_stream path). Declared first so it
     * outlives every libav object that holds avio_ctx — the destructor
     * resets `fmt` and frees `avio_ctx` explicitly before the implicit
     * member destructors run, then `input_stream` is destroyed last as
     * the implicit destructions unwind in reverse declaration order. */
    Option<Box<dyn<InputStream>>> input_stream;
    AVIOContext*                  avio_ctx { nullptr };

    FmtCtxPtr   fmt;
    CodecCtxPtr cctx;
    PacketPtr   pkt;
    FramePtr    src_frame;
    /* Sw landing frame for vulkan→sw downloads via
     * av_hwframe_transfer_data. Allocated lazily on first hw frame. */
    FramePtr sw_frame;
    SwsPtr   sws;
    /* Hwdevice context owned by the codec when present. Best-effort: a
     * NULL `hwd` here just means we run sw decode. */
    BufRefPtr     hwd;
    AVPixelFormat sws_src_fmt { AV_PIX_FMT_NONE };
    int           sws_src_w { 0 };
    int           sws_src_h { 0 };
    int           video_idx { -1 };
    AVRational    stream_tb { 0, 1 };
    u64           decoder_generation;
    bool          flushing { false };
#if defined(__APPLE__)
    rstd::sync::Mutex<ApplePrefetchState>   apple_prefetch { ApplePrefetchState {} };
    rstd::sync::Condvar                     apple_prefetch_condition;
    Option<rstd::thread::JoinHandle<empty>> apple_prefetch_thread;
#endif

    ~State() {
#if defined(__APPLE__)
        {
            auto prefetch  = apple_prefetch.lock().unwrap_unchecked();
            prefetch->stop = true;
        }
        apple_prefetch_condition.notify_all();
        if (apple_prefetch_thread.is_some()) {
            (void)rstd::move(apple_prefetch_thread.take().unwrap_unchecked()).join();
        }
#endif
        /* Tear down libavformat first so it stops invoking our avio
         * callbacks. Then free the avio buffer + context. input_stream
         * is released last (implicit destruction) — it's still alive
         * here in case avformat_close_input touches pb. */
        fmt.reset();
        if (avio_ctx) {
            rstd::uint8_t* buf = avio_ctx->buffer;
            avio_context_free(&avio_ctx);
            if (buf) av_free(buf);
        }
    }
};

VideoDecoder::StateOwner::~StateOwner() {
    auto state = Box<State>::from_raw(mut_ptr<State>::from_raw_parts(state_));
}

namespace
{

bool ensure_sws(VideoDecoder::State& st, int src_w, int src_h, AVPixelFormat src_fmt, u32 target_w,
                u32 target_h) {
    if (st.sws && st.sws_src_w == src_w && st.sws_src_h == src_h && st.sws_src_fmt == src_fmt) {
        return true;
    }
    /* Always emit NV12 — that's what YuvToRgba consumes. */
    st.sws.reset(sws_getContext(src_w,
                                src_h,
                                src_fmt,
                                static_cast<int>(target_w.to_primitive()),
                                static_cast<int>(target_h.to_primitive()),
                                AV_PIX_FMT_NV12,
                                SWS_BICUBIC,
                                nullptr,
                                nullptr,
                                nullptr));
    if (! st.sws) return false;
    st.sws_src_w   = src_w;
    st.sws_src_h   = src_h;
    st.sws_src_fmt = src_fmt;
    return true;
}

void reset_after_seek(VideoDecoder::State& st) {
    avcodec_flush_buffers(st.cctx.get());
    av_packet_unref(st.pkt.get());
    av_frame_unref(st.src_frame.get());
    if (st.sw_frame) av_frame_unref(st.sw_frame.get());
    st.flushing           = false;
    st.decoder_generation = next_decoder_generation();
}

bool seek_to_start(VideoDecoder::State& st) {
    int rc = av_seek_frame(st.fmt.get(), -1, 0, AVSEEK_FLAG_BACKWARD);
    if (rc < 0) return false;
    reset_after_seek(st);
    return true;
}

} // namespace

namespace
{
bool probe_native_impl(ref<str> path, u32* native_width, u32* native_height, Error* err) {
    *native_width             = u32();
    *native_height            = u32();
    AVFormatContext* raw_fmt  = nullptr;
    auto             path_c   = rstd::ffi::CString::make(String::make(path)).unwrap();
    auto             path_raw = path_c.as_ptr();
    if (int rc = avformat_open_input(&raw_fmt, path_raw, nullptr, nullptr); rc < 0) {
        fail(err, rstd::format("avformat_open_input: {}", av_err_str(rc).as_str()));
        return false;
    }
    FmtCtxPtr fmt(raw_fmt);
    if (int rc = avformat_find_stream_info(fmt.get(), nullptr); rc < 0) {
        fail(err, rstd::format("avformat_find_stream_info: {}", av_err_str(rc).as_str()));
        return false;
    }
    int idx = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (idx < 0) {
        fail(err, "no video stream in file"_str);
        return false;
    }
    AVCodecParameters* par = fmt->streams[idx]->codecpar;
    if (par->width <= 0 || par->height <= 0) {
        fail(err, "video stream has invalid native dimensions"_str);
        return false;
    }
    *native_width  = u32(static_cast<rstd::uint32_t>(par->width));
    *native_height = u32(static_cast<rstd::uint32_t>(par->height));
    return true;
}
} // namespace

/* AVIOContext shims that bounce libavformat IO into an IInputStream. */
namespace
{
int avio_read_shim(void* opaque, rstd::uint8_t* buf, int buf_size) {
    auto* state = static_cast<VideoDecoder::State*>(opaque);
    int   n     = state->input_stream->as_mut_ptr()->read(buf, buf_size);
    if (n == 0) return AVERROR_EOF;
    if (n < 0) return AVERROR(EIO);
    return n;
}
rstd::int64_t avio_seek_shim(void* opaque, rstd::int64_t offset, int whence) {
    auto* state = static_cast<VideoDecoder::State*>(opaque);
    return state->input_stream->as_mut_ptr()->seek(offset, whence);
}
} // namespace

VideoDecoder::~VideoDecoder() = default;

auto VideoDecoder::probe_native(ref<str> path) -> Result<ProbeResult, Error> {
    Error err;
    u32   width;
    u32   height;
    if (! probe_native_impl(path, &width, &height, &err)) return Err(rstd::move(err));
    return Ok(ProbeResult { width, height });
}

auto VideoDecoder::open(ref<str> path, u32 target_width, u32 target_height, bool loop)
    -> Result<Box<VideoDecoder>, Error> {
    Error err;
    auto  decoder = build_internal(InputSpec { String::make(path), None() },
                                   target_width,
                                   target_height,
                                   loop,
                                   nullptr,
                                   FrameKind::Sw,
                                   &err);
    if (decoder.is_none()) return Err(rstd::move(err));
    return Ok(rstd::move(decoder).unwrap());
}

auto VideoDecoder::open_with_vk(ref<str> path, u32 target_width, u32 target_height, bool loop,
                                const Producer& producer, const OpenOpts& opts)
    -> Result<Box<VideoDecoder>, Error> {
    auto render_node = resolve_render_node(producer, opts);
    /* Resolve trial order. On Apple, VideoToolbox is the first choice because
     * MoltenVK generally does not expose Vulkan video decode queues. */
    HwAccel order[3] = { HwAccel::None, HwAccel::None, HwAccel::None };
    int     n_order  = 0;
    switch (opts.hwaccel) {
    case HwAccel::Auto:
#if defined(__APPLE__)
        order[0] = HwAccel::VideoToolbox;
        n_order  = 1;
#else
        order[0] = HwAccel::Vulkan;
        order[1] = HwAccel::Vaapi;
        n_order  = 2;
#endif
        break;
    case HwAccel::Vulkan:
        order[0] = HwAccel::Vulkan;
        n_order  = 1;
        break;
    case HwAccel::Vaapi:
        order[0] = HwAccel::Vaapi;
        n_order  = 1;
        break;
    case HwAccel::VideoToolbox:
        order[0] = HwAccel::VideoToolbox;
        n_order  = 1;
        break;
    case HwAccel::None: n_order = 0; break;
    }

    for (int i = 0; i < n_order; ++i) {
        Error        local_err;
        AVBufferRef* hwd  = nullptr;
        FrameKind    kind = FrameKind::Sw;
        if (order[i] == HwAccel::Vulkan) {
            hwd  = make_shared_vulkan_hwdevice(producer, &local_err);
            kind = FrameKind::VulkanShared;
        } else if (order[i] == HwAccel::Vaapi) {
#if defined(WAVSEN_HAS_VAAPI)
            hwd  = make_vaapi_hwdevice(render_node, &local_err);
            kind = FrameKind::VaapiDrm;
#else
            local_err.message = String::make("wavsen built without VAAPI support"_str);
#endif
        } else if (order[i] == HwAccel::VideoToolbox) {
#if defined(__APPLE__)
            hwd  = make_videotoolbox_hwdevice(&local_err);
            kind = FrameKind::VideoToolbox;
#else
            local_err.message =
                String::make("VideoToolbox is only available on Apple platforms"_str);
#endif
        }
        if (! hwd) {
            rstd::log::info("VideoDecoder: hwaccel attempt {} skipped: {}",
                            hw_accel_name(order[i]),
                            local_err.message.as_str());
            continue;
        }
        Error err;
        auto  decoder = build_internal(InputSpec { String::make(path), None() },
                                       target_width,
                                       target_height,
                                       loop,
                                       hwd,
                                       kind,
                                       &err);
        if (decoder.is_some()) return Ok(rstd::move(decoder).unwrap());
        rstd::log::info("VideoDecoder: hwaccel {} build_internal failed: {} — trying next",
                        hw_accel_name(order[i]),
                        err.message.as_str());
        /* build_internal already unref'd `hwd` on failure via state. */
    }

    /* Final fallback: pure sw decode. */
    Error err;
    auto  decoder = build_internal(InputSpec { String::make(path), None() },
                                   target_width,
                                   target_height,
                                   loop,
                                   nullptr,
                                   FrameKind::Sw,
                                   &err);
    if (decoder.is_none()) return Err(rstd::move(err));
    return Ok(rstd::move(decoder).unwrap());
}

auto VideoDecoder::open_from_stream(InputStreamFactory make_stream, u32 target_width,
                                    u32 target_height, bool loop, const Producer* producer,
                                    const OpenOpts& opts) -> Result<Box<VideoDecoder>, Error> {
    auto fresh_stream = [&] {
        return make_stream.as_mut_ptr()->operator()();
    };

    /* VideoToolbox does not need a shared Vulkan producer: FFmpeg owns the
     * decoder device and the frame is bridged to Metal later by the caller. */
#if defined(__APPLE__)
    if (! producer && (opts.hwaccel == HwAccel::Auto || opts.hwaccel == HwAccel::VideoToolbox)) {
        Error        local_err;
        AVBufferRef* hwd = make_videotoolbox_hwdevice(&local_err);
        if (hwd != nullptr) {
            Error err;
            auto  decoder = build_internal(InputSpec { {}, Some(fresh_stream()) },
                                           target_width,
                                           target_height,
                                           loop,
                                           hwd,
                                           FrameKind::VideoToolbox,
                                           &err);
            if (decoder.is_some()) return Ok(rstd::move(decoder).unwrap());
            rstd::log::info("VideoDecoder: VideoToolbox build_internal failed: {} — falling back",
                            err.message.as_str());
        } else {
            rstd::log::info("VideoDecoder: VideoToolbox unavailable: {}",
                            local_err.message.as_str());
        }
    }
#endif

    /* Sw / vaapi-only fast path (no shared Vulkan hwdev). */
    if (! producer) {
        Error err;
        auto  decoder = build_internal(InputSpec { {}, Some(fresh_stream()) },
                                       target_width,
                                       target_height,
                                       loop,
                                       nullptr,
                                       FrameKind::Sw,
                                       &err);
        if (decoder.is_none()) return Err(rstd::move(err));
        return Ok(rstd::move(decoder).unwrap());
    }

    /* Shared Vulkan path: mirror open_with_vk's trial loop. Each trial
     * gets a fresh IInputStream from the factory — build_internal
     * consumes it, and on failure State's destructor cleans up. */
    auto    render_node = resolve_render_node(*producer, opts);
    HwAccel order[3]    = { HwAccel::None, HwAccel::None, HwAccel::None };
    int     n_order     = 0;
    switch (opts.hwaccel) {
    case HwAccel::Auto:
#if defined(__APPLE__)
        order[0] = HwAccel::VideoToolbox;
        n_order  = 1;
#else
        order[0] = HwAccel::Vulkan;
        order[1] = HwAccel::Vaapi;
        n_order  = 2;
#endif
        break;
    case HwAccel::Vulkan:
        order[0] = HwAccel::Vulkan;
        n_order  = 1;
        break;
    case HwAccel::Vaapi:
        order[0] = HwAccel::Vaapi;
        n_order  = 1;
        break;
    case HwAccel::VideoToolbox:
        order[0] = HwAccel::VideoToolbox;
        n_order  = 1;
        break;
    case HwAccel::None: n_order = 0; break;
    }

    for (int i = 0; i < n_order; ++i) {
        Error        local_err;
        AVBufferRef* hwd  = nullptr;
        FrameKind    kind = FrameKind::Sw;
        if (order[i] == HwAccel::Vulkan) {
            hwd  = make_shared_vulkan_hwdevice(*producer, &local_err);
            kind = FrameKind::VulkanShared;
        } else if (order[i] == HwAccel::Vaapi) {
#if defined(WAVSEN_HAS_VAAPI)
            hwd  = make_vaapi_hwdevice(render_node, &local_err);
            kind = FrameKind::VaapiDrm;
#else
            local_err.message = String::make("wavsen built without VAAPI support"_str);
#endif
        } else if (order[i] == HwAccel::VideoToolbox) {
#if defined(__APPLE__)
            hwd  = make_videotoolbox_hwdevice(&local_err);
            kind = FrameKind::VideoToolbox;
#else
            local_err.message =
                String::make("VideoToolbox is only available on Apple platforms"_str);
#endif
        }
        if (! hwd) {
            rstd::log::info("VideoDecoder: hwaccel attempt {} skipped: {}",
                            hw_accel_name(order[i]),
                            local_err.message.as_str());
            continue;
        }
        Error err;
        auto  decoder = build_internal(InputSpec { {}, Some(fresh_stream()) },
                                       target_width,
                                       target_height,
                                       loop,
                                       hwd,
                                       kind,
                                       &err);
        if (decoder.is_some()) return Ok(rstd::move(decoder).unwrap());
        rstd::log::info("VideoDecoder: hwaccel {} build_internal failed: {} — trying next",
                        hw_accel_name(order[i]),
                        err.message.as_str());
        /* build_internal already unref'd `hwd` via State on failure. */
    }

    /* Final sw fallback — also covers HwAccel::None (n_order == 0). */
    Error err;
    auto  decoder = build_internal(InputSpec { {}, Some(fresh_stream()) },
                                   target_width,
                                   target_height,
                                   loop,
                                   nullptr,
                                   FrameKind::Sw,
                                   &err);
    if (decoder.is_none()) return Err(rstd::move(err));
    return Ok(rstd::move(decoder).unwrap());
}

auto VideoDecoder::build_internal(InputSpec input, u32 target_width, u32 target_height, bool loop,
                                  void* prebuilt_hwdevice_value, FrameKind requested_kind,
                                  Error* err) -> Option<Box<VideoDecoder>> {
    AVBufferRef* prebuilt_hwdevice = static_cast<AVBufferRef*>(prebuilt_hwdevice_value);
    if (target_width == u32() || target_height == u32()) {
        fail(err, "target dimensions must be non-zero"_str);
        if (prebuilt_hwdevice) av_buffer_unref(&prebuilt_hwdevice);
        return None();
    }
    if (input.path.is_empty() && input.stream.is_none()) {
        fail(err, "InputSpec: neither path nor stream provided"_str);
        if (prebuilt_hwdevice) av_buffer_unref(&prebuilt_hwdevice);
        return None();
    }
    /* NV12 chroma is half-resolution → both dims must be even. */
    if (target_width % u32(2) != u32()) ++target_width;
    if (target_height % u32(2) != u32()) ++target_height;

    auto state                       = Box<VideoDecoder::State>::make();
    auto state_ptr                   = rstd::move(state).into_raw().as_raw_ptr();
    auto self                        = Box<VideoDecoder>::make(state_ptr);
    self->target_width_              = target_width;
    self->target_height_             = target_height;
    self->loop_                      = loop;
    self->state_->decoder_generation = next_decoder_generation();
    /* Provisional; downgraded to Sw below if hwdevice attach fails. */
    self->kind_ = requested_kind;
    /* Take ownership of the caller's hwdevice ref immediately so that
     * any early-return path below (avformat_open_input failure etc.)
     * unrefs it via state.hwd's deleter rather than leaking. The codec
     * gets its own ref later. */
    if (prebuilt_hwdevice) self->state_->hwd.reset(prebuilt_hwdevice);

    AVFormatContext* raw_fmt = nullptr;
    if (input.stream.is_some()) {
        /* Custom-IO open: install an AVIOContext that calls back into
         * the caller's IInputStream. fmt->pb must outlive fmt — see
         * State's explicit destructor for the cleanup ordering. */
        auto input_stream          = rstd::move(input.stream).unwrap();
        self->state_->input_stream = Some(rstd::move(input_stream));
        constexpr int kAvioBuf     = 4096;
        auto*         avio_buf     = static_cast<unsigned char*>(av_malloc(kAvioBuf));
        if (! avio_buf) {
            fail(err, "av_malloc(avio buffer) failed"_str);
            return None();
        }
        self->state_->avio_ctx = avio_alloc_context(avio_buf,
                                                    kAvioBuf,
                                                    /*write_flag=*/0,
                                                    /*opaque=*/&*self->state_,
                                                    &avio_read_shim,
                                                    /*write_packet=*/nullptr,
                                                    &avio_seek_shim);
        if (! self->state_->avio_ctx) {
            av_free(avio_buf);
            fail(err, "avio_alloc_context failed"_str);
            return None();
        }
        raw_fmt = avformat_alloc_context();
        if (! raw_fmt) {
            fail(err, "avformat_alloc_context failed"_str);
            return None();
        }
        raw_fmt->pb = self->state_->avio_ctx;
        raw_fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
        if (int rc = avformat_open_input(&raw_fmt, nullptr, nullptr, nullptr); rc < 0) {
            /* On failure avformat_open_input frees raw_fmt for us, but
             * leaves avio_ctx alone — State's destructor will free it. */
            fail(err, rstd::format("avformat_open_input(stream): {}", av_err_str(rc).as_str()));
            return None();
        }
    } else {
        auto input_path     = rstd::ffi::CString::make(rstd::move(input.path)).unwrap();
        auto input_path_raw = input_path.as_ptr();
        if (int rc = avformat_open_input(&raw_fmt, input_path_raw, nullptr, nullptr); rc < 0) {
            fail(err, rstd::format("avformat_open_input: {}", av_err_str(rc).as_str()));
            return None();
        }
    }
    self->state_->fmt.reset(raw_fmt);

    if (int rc = avformat_find_stream_info(self->state_->fmt.get(), nullptr); rc < 0) {
        fail(err, rstd::format("avformat_find_stream_info: {}", av_err_str(rc).as_str()));
        return None();
    }

    int idx = av_find_best_stream(self->state_->fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (idx < 0) {
        fail(err, "no video stream in file"_str);
        return None();
    }
    self->state_->video_idx = idx;
    AVStream*          st   = self->state_->fmt->streams[idx];
    AVCodecParameters* par  = st->codecpar;
    self->state_->stream_tb = st->time_base;

    /* FFmpeg's native `av1` decoder has no software path — it's a
     * parser + hwaccel dispatcher and returns ENOSYS on send_packet when
     * no hardware accelerator picked up the stream. Prefer libdav1d for
     * the pure-sw case so disabling hwdec actually works on AV1. */
    const AVCodec* dec = nullptr;
    if (par->codec_id == AV_CODEC_ID_AV1 && requested_kind == FrameKind::Sw) {
        dec = avcodec_find_decoder_by_name("libdav1d");
    }
    if (! dec) dec = avcodec_find_decoder(par->codec_id);
    if (! dec) {
        fail(err, rstd::format("no decoder for codec {}", avcodec_get_name(par->codec_id)));
        return None();
    }
    self->state_->cctx.reset(avcodec_alloc_context3(dec));
    if (! self->state_->cctx) {
        fail(err, "avcodec_alloc_context3 failed"_str);
        return None();
    }
    if (int rc = avcodec_parameters_to_context(self->state_->cctx.get(), par); rc < 0) {
        fail(err, rstd::format("avcodec_parameters_to_context: {}", av_err_str(rc).as_str()));
        return None();
    }
    self->state_->cctx->pkt_timebase = st->time_base;

    /* Hand the codec its own ref on the hwdevice the trial loop picked
     * (if any). Sw mode has hwd == nullptr — codec stays sw. */
    if (self->state_->hwd) {
        self->state_->cctx->hw_device_ctx = av_buffer_ref(self->state_->hwd.get());
        if (requested_kind == FrameKind::VulkanShared) {
            /* get_format_prefer_vulkan runs during avcodec_open2 below
             * and bootstraps a compatible hw_frames_ctx if it picks
             * AV_PIX_FMT_VULKAN. On any failure inside that
             * callback the codec falls through to a sw pix_fmt; we
             * detect that after open and reset kind_ to Sw. */
            self->state_->cctx->get_format = get_format_prefer_vulkan;
            rstd::log::info("VideoDecoder: AV_HWDEVICE_TYPE_VULKAN attached for codec {}.",
                            avcodec_get_name(par->codec_id));
        }
#if defined(WAVSEN_HAS_VAAPI)
        else if (requested_kind == FrameKind::VaapiDrm) {
            self->state_->cctx->get_format = get_format_prefer_vaapi;
            rstd::log::info("VideoDecoder: AV_HWDEVICE_TYPE_VAAPI attached for codec {}.",
                            avcodec_get_name(par->codec_id));
        }
#endif
#if defined(__APPLE__)
        else if (requested_kind == FrameKind::VideoToolbox) {
            self->state_->cctx->get_format = get_format_prefer_videotoolbox;
            rstd::log::info("VideoDecoder: AV_HWDEVICE_TYPE_VIDEOTOOLBOX attached for codec {}.",
                            avcodec_get_name(par->codec_id));
        }
#endif
    } else {
        rstd::log::info("VideoDecoder: sw decode for codec {}.", avcodec_get_name(par->codec_id));
    }

    if (int rc = avcodec_open2(self->state_->cctx.get(), dec, nullptr); rc < 0) {
        fail(err, rstd::format("avcodec_open2: {}", av_err_str(rc).as_str()));
        return None();
    }

    self->state_->pkt.reset(av_packet_alloc());
    self->state_->src_frame.reset(av_frame_alloc());
    if (! self->state_->pkt || ! self->state_->src_frame) {
        fail(err, "av_packet_alloc / av_frame_alloc failed"_str);
        return None();
    }

    /* Force get_format to run by feeding one probe packet — h264
     * (and most modern codecs) only invoke get_format on first frame
     * decode, not at avcodec_open2 time. We need to know whether the
     * hwaccel actually accepted the codec NOW, before returning, so
     * the trial loop can fall through to the next backend / sw on
     * mismatch instead of dying at first frame.
     *
     * The probe is also necessary for VAAPI specifically: when libva
     * rejects a profile (e.g. h264 profile 77), FFmpeg's get_format
     * silently falls back to a sw pix_fmt — avcodec_open2 still
     * succeeds, but the per-frame pump would then see a sw frame and
     * abort with "decoder produced non-VAAPI frame".
     *
     * Read frames until we hit a video packet, send it, then seek back
     * and flush so the user's first next_*_frame starts from byte 0. */
    AVPixelFormat want_pix_fmt = AV_PIX_FMT_NONE;
    const char*   hw_label     = nullptr;
    if (requested_kind == FrameKind::VulkanShared) {
        want_pix_fmt = AV_PIX_FMT_VULKAN;
        hw_label     = "vulkan";
    } else if (requested_kind == FrameKind::VaapiDrm) {
        want_pix_fmt = AV_PIX_FMT_VAAPI;
        hw_label     = "vaapi";
#if defined(__APPLE__)
    } else if (requested_kind == FrameKind::VideoToolbox) {
        want_pix_fmt = AV_PIX_FMT_VIDEOTOOLBOX;
        hw_label     = "videotoolbox";
#endif
    }
    if (want_pix_fmt != AV_PIX_FMT_NONE) {
        AVPacket* probe = av_packet_alloc();
        if (probe) {
            bool got_video = false;
            while (av_read_frame(self->state_->fmt.get(), probe) >= 0) {
                if (probe->stream_index == self->state_->video_idx) {
                    got_video = true;
                    break;
                }
                av_packet_unref(probe);
            }
            if (got_video) {
                avcodec_send_packet(self->state_->cctx.get(), probe);
                av_packet_unref(probe);
            }
            av_packet_free(&probe);

            if (av_seek_frame(self->state_->fmt.get(), -1, 0, AVSEEK_FLAG_BACKWARD) >= 0) {
                reset_after_seek(*self->state_);
            }
        }

        if (self->state_->cctx->pix_fmt != want_pix_fmt) {
            /* Return failure so the trial loop in open_with_vk /
             * open_from_stream falls through to the next backend (or
             * final sw fallback, which picks libdav1d for AV1). Just
             * flipping kind_ would leave cctx bound to a hw-only native
             * decoder and trip ENOSYS / wrong-format on first frame. */
            fail(err,
                 rstd::format("{} hwaccel rejected codec {} (probe pix_fmt={})",
                              hw_label,
                              avcodec_get_name(par->codec_id),
                              av_get_pix_fmt_name(self->state_->cctx->pix_fmt)));
            return None();
        }
    }

#if defined(__APPLE__)
    if (self->kind_ == FrameKind::VideoToolbox) self->start_apple_prefetch();
#endif
    return Some(rstd::move(self));
}

int VideoDecoder::next_vk_frame_(VkFrameInfo& out, Error* err) {
    if (kind_ != FrameKind::VulkanShared) {
        fail(err, "next_vk_frame called on non-shared-device decoder"_str);
        return -1;
    }
    State& st     = *state_;
    bool   looped = false;

    /* Release the previously-yielded AVVkFrame back to the pool. The
     * caller has either queue-submitted all GPU work that references it
     * or explicitly discarded it. Submitted work updates the frame's
     * timeline value before the next pull. */
    av_frame_unref(st.src_frame.get());

    while (true) {
        int rc = avcodec_receive_frame(st.cctx.get(), st.src_frame.get());
        if (rc == 0) {
            if (st.src_frame->format != AV_PIX_FMT_VULKAN) {
                fail(err, "next_vk_frame: decoder produced non-vulkan frame"_str);
                return -1;
            }
            out.width       = u32(static_cast<rstd::uint32_t>(st.src_frame->width));
            out.height      = u32(static_cast<rstd::uint32_t>(st.src_frame->height));
            out.colorspace  = map_colorspace(st.src_frame->colorspace);
            out.color_range = map_range(st.src_frame->color_range);
            /* Look up the AVHWFramesContext's sw_format to know whether
             * the GPU images we're about to sample are 8-bit (NV12) or
             * 10-bit (P010). Both are 2-image disjoint formats here. */
            out.bit_depth = u32(8);
            if (st.src_frame->hw_frames_ctx) {
                auto* hwfc =
                    reinterpret_cast<AVHWFramesContext*>(st.src_frame->hw_frames_ctx->data);
                if (hwfc->sw_format == AV_PIX_FMT_P010 || hwfc->sw_format == AV_PIX_FMT_P016) {
                    out.bit_depth = u32(16);
                }
            }
            const rstd::int64_t pts = (st.src_frame->best_effort_timestamp != AV_NOPTS_VALUE)
                                          ? st.src_frame->best_effort_timestamp
                                          : st.src_frame->pts;
            out.pts_seconds =
                pts == AV_NOPTS_VALUE
                    ? f64(-1.0)
                    : f64(static_cast<double>(pts) * ffi::ffmpeg::av_q2d(st.stream_tb));
            return looped ? 2 : 0;
        }
        if (rc == AVERROR_EOF) {
            if (loop_) {
                if (! seek_to_start(st)) {
                    fail(err, "loop seek-to-zero failed"_str);
                    return -1;
                }
                looped = true;
                continue;
            }
            return 1;
        }
        if (rc != AVERROR(EAGAIN)) {
            fail(err, rstd::format("avcodec_receive_frame: {}", av_err_str(rc).as_str()));
            return -1;
        }
        if (st.flushing) continue;

        rc = av_read_frame(st.fmt.get(), st.pkt.get());
        if (rc == AVERROR_EOF) {
            avcodec_send_packet(st.cctx.get(), nullptr);
            st.flushing = true;
            continue;
        }
        if (rc < 0) {
            fail(err, rstd::format("av_read_frame: {}", av_err_str(rc).as_str()));
            return -1;
        }
        if (st.pkt->stream_index != st.video_idx) {
            av_packet_unref(st.pkt.get());
            continue;
        }
        rc = avcodec_send_packet(st.cctx.get(), st.pkt.get());
        av_packet_unref(st.pkt.get());
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            fail(err, rstd::format("avcodec_send_packet: {}", av_err_str(rc).as_str()));
            return -1;
        }
    }
}

int VideoDecoder::next_vaapi_frame_(VaapiFrameView& out, Error* err) {
    if (kind_ != FrameKind::VaapiDrm) {
        fail(err, "next_vaapi_frame called on non-VAAPI decoder"_str);
        return -1;
    }
    State& st     = *state_;
    bool   looped = false;

    /* Returned leases keep independent frame references, so the decoder
     * can release and reuse its working frame before the next receive. */
    av_frame_unref(st.src_frame.get());

    while (true) {
        int rc = avcodec_receive_frame(st.cctx.get(), st.src_frame.get());
        if (rc == 0) {
#if defined(WAVSEN_HAS_VAAPI)
            if (st.src_frame->format != AV_PIX_FMT_VAAPI) {
                fail(err, "next_vaapi_frame: decoder produced non-VAAPI frame"_str);
                return -1;
            }
#else
            fail(err, "next_vaapi_frame: VAAPI support not built"_str);
            return -1;
#endif
            out.width       = u32(static_cast<rstd::uint32_t>(st.src_frame->width));
            out.height      = u32(static_cast<rstd::uint32_t>(st.src_frame->height));
            out.colorspace  = map_colorspace(st.src_frame->colorspace);
            out.color_range = map_range(st.src_frame->color_range);
            /* VAAPI 8-bit profiles land as NV12; 10-bit as P010. We only
             * support 8-bit on the DRM_PRIME zero-copy path for now. */
            out.bit_depth           = u32(8);
            const rstd::int64_t pts = (st.src_frame->best_effort_timestamp != AV_NOPTS_VALUE)
                                          ? st.src_frame->best_effort_timestamp
                                          : st.src_frame->pts;
            out.pts_seconds =
                pts == AV_NOPTS_VALUE
                    ? f64(-1.0)
                    : f64(static_cast<double>(pts) * ffi::ffmpeg::av_q2d(st.stream_tb));
            return looped ? 2 : 0;
        }
        if (rc == AVERROR_EOF) {
            if (loop_) {
                if (! seek_to_start(st)) {
                    fail(err, "loop seek-to-zero failed"_str);
                    return -1;
                }
                looped = true;
                continue;
            }
            return 1;
        }
        if (rc != AVERROR(EAGAIN)) {
            fail(err, rstd::format("avcodec_receive_frame: {}", av_err_str(rc).as_str()));
            return -1;
        }
        if (st.flushing) continue;

        rc = av_read_frame(st.fmt.get(), st.pkt.get());
        if (rc == AVERROR_EOF) {
            avcodec_send_packet(st.cctx.get(), nullptr);
            st.flushing = true;
            continue;
        }
        if (rc < 0) {
            fail(err, rstd::format("av_read_frame: {}", av_err_str(rc).as_str()));
            return -1;
        }
        if (st.pkt->stream_index != st.video_idx) {
            av_packet_unref(st.pkt.get());
            continue;
        }
        rc = avcodec_send_packet(st.cctx.get(), st.pkt.get());
        av_packet_unref(st.pkt.get());
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            fail(err, rstd::format("avcodec_send_packet: {}", av_err_str(rc).as_str()));
            return -1;
        }
    }
}

int VideoDecoder::next_apple_frame_(AppleVideoFrameView& out, Error* err) {
#if ! defined(__APPLE__)
    fail(err, "next_apple_frame called on a non-Apple build"_str);
    return -1;
#else
    if (kind_ != FrameKind::VideoToolbox) {
        fail(err, "next_apple_frame called on a non-VideoToolbox decoder"_str);
        return -1;
    }
    State& st     = *state_;
    bool   looped = false;

    av_frame_unref(st.src_frame.get());
    while (true) {
        int rc = avcodec_receive_frame(st.cctx.get(), st.src_frame.get());
        if (rc == 0) {
            if (st.src_frame->format != AV_PIX_FMT_VIDEOTOOLBOX) {
                fail(err, "next_apple_frame: decoder produced a non-VideoToolbox frame"_str);
                return -1;
            }
            out.width               = u32(static_cast<rstd::uint32_t>(st.src_frame->width));
            out.height              = u32(static_cast<rstd::uint32_t>(st.src_frame->height));
            out.colorspace          = map_colorspace(st.src_frame->colorspace);
            out.color_range         = map_range(st.src_frame->color_range);
            const rstd::int64_t pts = (st.src_frame->best_effort_timestamp != AV_NOPTS_VALUE)
                                          ? st.src_frame->best_effort_timestamp
                                          : st.src_frame->pts;
            out.pts_seconds =
                pts == AV_NOPTS_VALUE
                    ? f64(-1.0)
                    : f64(static_cast<double>(pts) * ffi::ffmpeg::av_q2d(st.stream_tb));
            return looped ? 2 : 0;
        }
        if (rc == AVERROR_EOF) {
            if (loop_) {
                if (! seek_to_start(st)) {
                    fail(err, "loop seek-to-zero failed"_str);
                    return -1;
                }
                looped = true;
                continue;
            }
            return 1;
        }
        if (rc != AVERROR(EAGAIN)) {
            fail(err, rstd::format("avcodec_receive_frame: {}", av_err_str(rc).as_str()));
            return -1;
        }
        if (st.flushing) continue;

        rc = av_read_frame(st.fmt.get(), st.pkt.get());
        if (rc == AVERROR_EOF) {
            avcodec_send_packet(st.cctx.get(), nullptr);
            st.flushing = true;
            continue;
        }
        if (rc < 0) {
            fail(err, rstd::format("av_read_frame: {}", av_err_str(rc).as_str()));
            return -1;
        }
        if (st.pkt->stream_index != st.video_idx) {
            av_packet_unref(st.pkt.get());
            continue;
        }
        rc = avcodec_send_packet(st.cctx.get(), st.pkt.get());
        av_packet_unref(st.pkt.get());
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            fail(err, rstd::format("avcodec_send_packet: {}", av_err_str(rc).as_str()));
            return -1;
        }
    }
#endif
}

int VideoDecoder::next_frame_(Nv12Frame& out, Error* err) {
    State& st     = *state_;
    bool   looped = false;

    /* Resize output buffer to NV12 size on first call (and on extent
     * change, but the extent is fixed for VideoDecoder lifetime). */
    const usize want = usize(target_width_.to_primitive()) * usize(target_height_.to_primitive()) *
                       usize(3) / usize(2);
    if (out.width != target_width_ || out.height != target_height_ || out.data.len() != want) {
        out.width                = target_width_;
        out.height               = target_height_;
        const rstd::uint8_t zero = 0;
        out.data.resize(want, zero);
    }

    while (true) {
        int rc = avcodec_receive_frame(st.cctx.get(), st.src_frame.get());
        if (rc == 0) {
            /* If the decoder produced a vulkan-typed frame (Iter 4 hw
             * path), download it to a sw frame first. The download lands
             * in whatever YUV format the AVHWFramesContext exposes —
             * typically NV12 — and swscale handles whatever it is. */
            AVFrame* feed = st.src_frame.get();
            if (feed->format == AV_PIX_FMT_VULKAN) {
                if (! st.sw_frame) st.sw_frame.reset(av_frame_alloc());
                if (! st.sw_frame) {
                    fail(err, "av_frame_alloc(sw_frame) failed"_str);
                    return -1;
                }
                av_frame_unref(st.sw_frame.get());
                int trc = av_hwframe_transfer_data(st.sw_frame.get(), feed, 0);
                if (trc < 0) {
                    fail(err,
                         rstd::format("av_hwframe_transfer_data: {}", av_err_str(trc).as_str()));
                    av_frame_unref(st.src_frame.get());
                    return -1;
                }
                /* Preserve PTS across the transfer (transfer_data copies
                 * pixel data only). */
                st.sw_frame->pts                   = feed->pts;
                st.sw_frame->best_effort_timestamp = feed->best_effort_timestamp;
                feed                               = st.sw_frame.get();
            }

            const auto src_fmt = static_cast<AVPixelFormat>(feed->format);
            const int  src_w   = feed->width;
            const int  src_h   = feed->height;
            if (src_w <= 0 || src_h <= 0 || src_fmt == AV_PIX_FMT_NONE) {
                fail(err, "decoded frame has invalid dimensions/format"_str);
                return -1;
            }
            if (! ensure_sws(st, src_w, src_h, src_fmt, target_width_, target_height_)) {
                fail(err,
                     rstd::format("sws_getContext failed (src={})", av_get_pix_fmt_name(src_fmt)));
                return -1;
            }
            rstd::uint8_t* y_dst         = out.data.data();
            rstd::uint8_t* uv_dst        = out.data.data() + (usize(target_width_.to_primitive()) *
                                                              usize(target_height_.to_primitive()))
                                                                 .to_primitive();
            rstd::uint8_t* dst_planes[4] = { y_dst, uv_dst, nullptr, nullptr };
            int dst_strides[4] = { static_cast<int>(target_width_.to_primitive()),
                                   static_cast<int>(
                                       target_width_.to_primitive()), /* NV12 UV pitch == width */
                                   0,
                                   0 };
            int scaled         = sws_scale(
                st.sws.get(), feed->data, feed->linesize, 0, src_h, dst_planes, dst_strides);
            if (scaled <= 0) {
                fail(err, "sws_scale produced no rows"_str);
                return -1;
            }
            const rstd::int64_t pts = (feed->best_effort_timestamp != AV_NOPTS_VALUE)
                                          ? feed->best_effort_timestamp
                                          : feed->pts;
            out.pts_seconds =
                pts == AV_NOPTS_VALUE
                    ? f64(-1.0)
                    : f64(static_cast<double>(pts) * ffi::ffmpeg::av_q2d(st.stream_tb));
            out.colorspace  = map_colorspace(feed->colorspace);
            out.color_range = map_range(feed->color_range);
            av_frame_unref(st.src_frame.get());
            if (st.sw_frame) av_frame_unref(st.sw_frame.get());
            return looped ? 2 : 0;
        }
        if (rc == AVERROR_EOF) {
            if (loop_) {
                if (! seek_to_start(st)) {
                    fail(err, "loop seek-to-zero failed"_str);
                    return -1;
                }
                looped = true;
                continue;
            }
            return 1;
        }
        if (rc != AVERROR(EAGAIN)) {
            fail(err, rstd::format("avcodec_receive_frame: {}", av_err_str(rc).as_str()));
            return -1;
        }

        if (st.flushing) continue;

        rc = av_read_frame(st.fmt.get(), st.pkt.get());
        if (rc == AVERROR_EOF) {
            avcodec_send_packet(st.cctx.get(), nullptr);
            st.flushing = true;
            continue;
        }
        if (rc < 0) {
            fail(err, rstd::format("av_read_frame: {}", av_err_str(rc).as_str()));
            return -1;
        }
        if (st.pkt->stream_index != st.video_idx) {
            av_packet_unref(st.pkt.get());
            continue;
        }
        rc = avcodec_send_packet(st.cctx.get(), st.pkt.get());
        av_packet_unref(st.pkt.get());
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            fail(err, rstd::format("avcodec_send_packet: {}", av_err_str(rc).as_str()));
            return -1;
        }
    }
}

// ---------------------------------------------------------------------------
// Public Result wrappers for the per-frame pull
// ---------------------------------------------------------------------------

auto VideoDecoder::next_frame(Nv12Frame& out) -> Result<NextFrame, Error> {
    Error err;
    int   rc = next_frame_(out, &err);
    if (rc < 0) return Err(rstd::move(err));
    if (rc == 1) return Ok(NextFrame::Eof);
    if (rc == 2) return Ok(NextFrame::Looped);
    return Ok(NextFrame::Ok);
}

auto VideoDecoder::next_vk_frame() -> Result<VkFramePull, Error> {
    VkFrameInfo out;
    Error       err;
    int         rc = next_vk_frame_(out, &err);
    if (rc < 0) return Err(rstd::move(err));
    if (rc == 1) return Ok(VkFramePull { .status = NextFrame::Eof, .frame = None() });

    FramePtr source(av_frame_clone(state_->src_frame.get()));
    if (! source) return Err(Error("av_frame_clone(Vulkan lease) failed"_str));

    auto lease_state    = Box<VkFrameLease::State>::make();
    lease_state->source = rstd::move(source);
    auto state_ptr      = rstd::move(lease_state).into_raw().as_raw_ptr();
    return Ok(VkFramePull {
        .status = rc == 2 ? NextFrame::Looped : NextFrame::Ok,
        .frame  = Some(VkFrameLease(state_ptr, rstd::move(out))),
    });
}

auto VideoDecoder::next_vaapi_frame() -> Result<VaapiFramePull, Error> {
    VaapiFrameView out;
    Error          err;
    int            rc = next_vaapi_frame_(out, &err);
    if (rc < 0) return Err(rstd::move(err));
    if (rc == 1) return Ok(VaapiFramePull { .status = NextFrame::Eof, .frame = None() });

    FramePtr source(av_frame_clone(state_->src_frame.get()));
    if (! source) return Err(Error("av_frame_clone(VAAPI lease) failed"_str));

    auto lease_state            = Box<VaapiFrameLease::State>::make();
    lease_state->source         = rstd::move(source);
    auto       lease_state_ptr  = rstd::move(lease_state).into_raw().as_raw_ptr();
    const auto surface_identity = u64(
        static_cast<rstd::uint64_t>(reinterpret_cast<rstd::uintptr_t>(state_->src_frame->data[3])));
    VaapiFrameLease lease(lease_state_ptr,
                          rstd::move(out),
                          DrmResourceKey {
                              .decoder_generation = state_->decoder_generation,
                              .surface_identity   = surface_identity,
                          });
    return Ok(VaapiFramePull {
        .status = rc == 2 ? NextFrame::Looped : NextFrame::Ok,
        .frame  = Some(rstd::move(lease)),
    });
}

#if defined(__APPLE__)
auto VideoDecoder::next_apple_frame_sync() -> Result<AppleFramePull, Error> {
    AppleVideoFrameView out;
    Error               err;
    int                 rc = next_apple_frame_(out, &err);
    if (rc < 0) return Err(rstd::move(err));
    if (rc == 1) return Ok(AppleFramePull { .status = NextFrame::Eof, .frame = None() });

    using namespace ffi::corevideo;
    auto buffer = reinterpret_cast<CVPixelBufferRef>(state_->src_frame->data[3]);
    if (! buffer) return Err(Error("videotoolbox frame has no pixel buffer"_str));
    auto lease_state = Box<AppleFrameLease::State>::make();
    ffi::corefoundation::CFRetain(buffer);
    lease_state->pixel_buffer     = buffer;
    auto                state_ptr = rstd::move(lease_state).into_raw().as_raw_ptr();
    AppleVideoFrameView view {
        .pixel_buffer = buffer,
        .io_surface   = CVPixelBufferGetIOSurface(buffer),
        .width        = u32(CVPixelBufferGetWidth(buffer)),
        .height       = u32(CVPixelBufferGetHeight(buffer)),
        .pixel_format = u32(CVPixelBufferGetPixelFormatType(buffer)),
        .plane_count =
            CVPixelBufferIsPlanar(buffer) ? u32(CVPixelBufferGetPlaneCount(buffer)) : u32(1),
        .pts_seconds = out.pts_seconds,
        .colorspace  = out.colorspace,
        .color_range = out.color_range,
    };
    return Ok(AppleFramePull {
        .status = rc == 2 ? NextFrame::Looped : NextFrame::Ok,
        .frame  = Some(AppleFrameLease(state_ptr, rstd::move(view))),
    });
}

void VideoDecoder::start_apple_prefetch() {
    State& state    = *state_;
    auto   prefetch = state.apple_prefetch.lock().unwrap_unchecked();
    if (state.apple_prefetch_thread.is_some()) return;

    prefetch->stop          = false;
    prefetch->error         = false;
    prefetch->error_message = {};
    auto thread             = rstd::thread::spawn([this] {
        State& state = *state_;

        for (;;) {
            {
                auto prefetch = state.apple_prefetch.lock().unwrap_unchecked();
                state.apple_prefetch_condition.wait_while(prefetch, [](const auto& value) {
                    return ! value.stop && value.frames.len() >= usize(8);
                });
                if (prefetch->stop) return empty {};
            }

            auto pulled = next_apple_frame_sync();
            if (pulled.is_err()) {
                auto error    = rstd::move(pulled).unwrap_err();
                auto prefetch = state.apple_prefetch.lock().unwrap_unchecked();
                if (prefetch->stop) return empty {};
                prefetch->error         = true;
                prefetch->error_message = rstd::move(error.message);
                state.apple_prefetch_condition.notify_all();
                return empty {};
            }

            auto frame    = rstd::move(pulled).unwrap();
            auto prefetch = state.apple_prefetch.lock().unwrap_unchecked();
            if (prefetch->stop) return empty {};
            prefetch->frames.push(rstd::move(frame));
            state.apple_prefetch_condition.notify_all();
        }
    });

    if (thread.is_ok()) {
        state.apple_prefetch_thread = Some(rstd::move(thread).unwrap_unchecked());
    } else {
        prefetch->error         = true;
        prefetch->error_message = String::make("failed to start VideoToolbox prefetch thread"_str);
        state.apple_prefetch_condition.notify_all();
    }
}

void VideoDecoder::stop_apple_prefetch() {
    State& state = *state_;
    {
        auto prefetch  = state.apple_prefetch.lock().unwrap_unchecked();
        prefetch->stop = true;
    }
    state.apple_prefetch_condition.notify_all();
    if (state.apple_prefetch_thread.is_some()) {
        (void)rstd::move(state.apple_prefetch_thread.take().unwrap_unchecked()).join();
    }

    auto prefetch = state.apple_prefetch.lock().unwrap_unchecked();
    prefetch->frames.clear();
    prefetch->error         = false;
    prefetch->error_message = {};
}
#endif

auto VideoDecoder::next_apple_frame() -> Result<AppleFramePull, Error> {
#if ! defined(__APPLE__)
    return Err(Error("VideoToolbox is only available on Apple platforms"_str));
#else
    start_apple_prefetch();
    State& state    = *state_;
    auto   prefetch = state.apple_prefetch.lock().unwrap_unchecked();
    state.apple_prefetch_condition.wait_while(prefetch, [](const auto& value) {
        return ! value.stop && value.frames.is_empty() && ! value.error;
    });

    if (! prefetch->frames.is_empty()) {
        auto frame = prefetch->frames.remove(usize());
        state.apple_prefetch_condition.notify_all();
        return Ok(rstd::move(frame));
    }
    if (prefetch->error) {
        return Err(Error(prefetch->error_message.clone()));
    }
    return Err(Error("VideoToolbox prefetch stopped"_str));
#endif
}

auto VideoDecoder::next_drm_frame() -> Result<DrmFramePull, Error> {
    auto pulled = next_vaapi_frame();
    if (pulled.is_err()) return Err(rstd::move(pulled).unwrap_err());

    auto vaapi = rstd::move(pulled).unwrap();
    if (vaapi.frame.is_none()) {
        return Ok(DrmFramePull { .status = vaapi.status, .frame = None() });
    }
    auto mapped = rstd::move(*vaapi.frame).into_drm();
    if (mapped.is_err()) return Err(rstd::move(mapped).unwrap_err());
    return Ok(DrmFramePull {
        .status = vaapi.status,
        .frame  = Some(rstd::move(mapped).unwrap()),
    });
}

auto VideoDecoder::seek(f64 seconds) -> Result<empty, Error> {
    if (! seconds.is_finite() || seconds < f64()) {
        return Err(Error("seek time must be finite and non-negative"_str));
    }

#if defined(__APPLE__)
    if (kind_ == FrameKind::VideoToolbox) stop_apple_prefetch();
#endif

    State& st    = *state_;
    auto   limit = duration();
    if (limit.is_some()) seconds = seconds.min(*limit);
    const double time_base = ffi::ffmpeg::av_q2d(st.stream_tb);
    if (time_base <= 0.0) {
        return Err(Error("video stream has an invalid time base"_str));
    }
    const auto timestamp = static_cast<rstd::int64_t>(seconds.to_primitive() / time_base);
    const int  rc = av_seek_frame(st.fmt.get(), st.video_idx, timestamp, AVSEEK_FLAG_BACKWARD);
    if (rc < 0) {
#if defined(__APPLE__)
        if (kind_ == FrameKind::VideoToolbox) start_apple_prefetch();
#endif
        return Err(Error(rstd::format("av_seek_frame: {}", av_err_str(rc).as_str())));
    }

    reset_after_seek(st);
#if defined(__APPLE__)
    if (kind_ == FrameKind::VideoToolbox) start_apple_prefetch();
#endif
    return Ok(empty {});
}

auto VideoDecoder::duration() const -> Option<f64> {
    const State& st = *state_;
    if (st.fmt->duration > 0) {
        return Some(f64(static_cast<double>(st.fmt->duration) / AV_TIME_BASE));
    }
    const auto* stream = st.fmt->streams[st.video_idx];
    if (stream->duration <= 0) return None();
    return Some(
        f64(static_cast<double>(stream->duration) * ffi::ffmpeg::av_q2d(stream->time_base)));
}

} // namespace wavsen::video
