export module wavsen.video:video_decoder;

import rstd;
import vvk;
import :vk_device;

using namespace rstd::prelude;

export namespace wavsen::video
{

struct Nv12Frame {
    Vec<rstd::uint8_t> data;
    u32                width {};
    u32                height {};
    f64                pts_seconds { -1.0 };
    u32                colorspace {};
    u32                color_range {};
};

struct ProbeResult {
    u32 width;
    u32 height;
};

enum class NextFrame
{
    Ok,
    Looped,
    Eof,
};

struct VkFrameInfo {
    u32 width;
    u32 height;
    f64 pts_seconds { -1.0 };
    u32 colorspace {};
    u32 color_range {};
    u32 bit_depth { 8 };
};

struct VkFrameView {
    VkImage*        img;
    VkImageLayout*  layout;
    VkSemaphore*    sem;
    rstd::uint64_t* sem_value;
    rstd::uint32_t* queue_family;
    u32             plane_count;
    u32             width;
    u32             height;
    f64             pts_seconds { -1.0 };
    u32             colorspace {};
    u32             color_range {};
    u32             bit_depth { 8 };
};

class VkFrameLease;
class YuvToRgba;

class VkFrameAccess {
public:
    VkFrameAccess(const VkFrameAccess&)            = delete;
    VkFrameAccess& operator=(const VkFrameAccess&) = delete;
    VkFrameAccess(VkFrameAccess&& other) noexcept;
    VkFrameAccess& operator=(VkFrameAccess&& other) noexcept;
    ~VkFrameAccess();

    const VkFrameView& view() const noexcept { return view_; }
    bool               valid() const noexcept { return state_ != nullptr; }

    struct State;

private:
    friend class VkFrameLease;
    friend class YuvToRgba;

    VkFrameAccess(State* state, VkFrameView view) noexcept
        : state_(state), view_(rstd::move(view)) {}

    void reset() noexcept;
    void clear_access(u32 plane) noexcept;

    State*      state_ { nullptr };
    VkFrameView view_;
};

class VkFrameLease {
public:
    VkFrameLease(const VkFrameLease&)            = delete;
    VkFrameLease& operator=(const VkFrameLease&) = delete;
    VkFrameLease(VkFrameLease&& other) noexcept;
    VkFrameLease& operator=(VkFrameLease&& other) noexcept;
    ~VkFrameLease();

    const VkFrameInfo& info() const noexcept { return info_; }
    bool               valid() const noexcept { return state_ != nullptr; }
    auto               lock() const -> Result<VkFrameAccess, Error>;

    struct State;

private:
    friend class VideoDecoder;

    VkFrameLease(State* state, VkFrameInfo info) noexcept: state_(state), info_(rstd::move(info)) {}

    void reset() noexcept;

    State*      state_ { nullptr };
    VkFrameInfo info_;
};

struct VkFramePull {
    NextFrame            status { NextFrame::Ok };
    Option<VkFrameLease> frame;
};

enum class HwAccel
{
    Auto   = 0,
    Vulkan = 1,
    Vaapi  = 2,
    None   = 3,
    // The value is part of the cross-platform option ABI; the implementation
    // reports it unavailable on non-Apple targets.
    VideoToolbox = 4,
};

struct OpenOpts {
    HwAccel hwaccel { HwAccel::Auto };
    String  render_node;
};

struct InputStream {
    using Trait                  = InputStream;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api {
        using Trait = InputStream;

        auto read(rstd::uint8_t* buf, int size) -> int {
            return rstd::trait_call<0>(this, buf, size);
        }
        auto seek(rstd::int64_t offset, int whence) -> rstd::int64_t {
            return rstd::trait_call<1>(this, offset, whence);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::read, &T::seek>;
};

using InputStreamFactory = Box<dyn<FnMut<Box<dyn<InputStream>>()>>>;

enum class FrameKind
{
    Sw           = 0,
    VulkanShared = 1,
    VaapiDrm     = 2,
    VideoToolbox = 3,
};

struct DrmPlane {
    u32 object_index;
    u64 offset;
    u64 pitch;
};

struct DrmLayer {
    rstd::uint32_t fourcc;
    u32            plane_count;
    DrmPlane       planes[4];
};

struct DrmObject {
    int            fd;
    u64            size;
    rstd::uint64_t format_modifier;
};

struct DrmFrameView {
    u32       object_count {};
    DrmObject objects[4] {};
    u32       layer_count {};
    DrmLayer  layers[4] {};
    u32       width {};
    u32       height {};
    f64       pts_seconds { -1.0 };
    u32       colorspace {};
    u32       color_range {};
    u32       bit_depth { 8 };
};

struct DrmResourceKey {
    u64 decoder_generation;
    u64 surface_identity;

    friend bool operator==(const DrmResourceKey&, const DrmResourceKey&) = default;
};

class VideoDecoder;
class VaapiFrameLease;

class DrmFrameLease {
public:
    DrmFrameLease(const DrmFrameLease&)            = delete;
    DrmFrameLease& operator=(const DrmFrameLease&) = delete;
    DrmFrameLease(DrmFrameLease&& other) noexcept;
    DrmFrameLease& operator=(DrmFrameLease&& other) noexcept;
    ~DrmFrameLease();

    const DrmFrameView& view() const noexcept { return view_; }
    DrmResourceKey      resource_key() const noexcept { return resource_key_; }
    bool                valid() const noexcept { return state_ != nullptr; }

    struct State;

private:
    friend class VideoDecoder;
    friend class VaapiFrameLease;

    DrmFrameLease(State* state, DrmFrameView view, DrmResourceKey resource_key) noexcept
        : state_(state), view_(rstd::move(view)), resource_key_(resource_key) {}

    void reset() noexcept;

    State*         state_ { nullptr };
    DrmFrameView   view_;
    DrmResourceKey resource_key_;
};

struct VaapiFrameView {
    u32 width {};
    u32 height {};
    f64 pts_seconds { -1.0 };
    u32 colorspace {};
    u32 color_range {};
    u32 bit_depth { 8 };
};

class VaapiFrameLease {
public:
    VaapiFrameLease(const VaapiFrameLease&)            = delete;
    VaapiFrameLease& operator=(const VaapiFrameLease&) = delete;
    VaapiFrameLease(VaapiFrameLease&& other) noexcept;
    VaapiFrameLease& operator=(VaapiFrameLease&& other) noexcept;
    ~VaapiFrameLease();

    const VaapiFrameView& view() const noexcept { return view_; }
    bool                  valid() const noexcept { return state_ != nullptr; }
    auto                  into_drm() && -> Result<DrmFrameLease, Error>;

    struct State;

private:
    friend class VideoDecoder;

    VaapiFrameLease(State* state, VaapiFrameView view, DrmResourceKey resource_key) noexcept
        : state_(state), view_(rstd::move(view)), resource_key_(resource_key) {}

    void reset() noexcept;

    State*         state_ { nullptr };
    VaapiFrameView view_;
    DrmResourceKey resource_key_;
};

struct VaapiFramePull {
    NextFrame               status { NextFrame::Ok };
    Option<VaapiFrameLease> frame;
};

// Apple VideoToolbox exposes decoded frames as CVPixelBuffer/IOSurface
// objects. Keep the public module ABI opaque so the rest of wavsen and the
// Linux build do not depend on CoreVideo or Objective-C types.
struct AppleVideoFrameView {
    void* pixel_buffer { nullptr };
    void* io_surface { nullptr };
    u32   width {};
    u32   height {};
    u32   pixel_format {};
    u32   plane_count {};
    f64   pts_seconds { -1.0 };
    u32   colorspace {};
    u32   color_range {};
};

class AppleFrameLease {
public:
    AppleFrameLease(const AppleFrameLease&)            = delete;
    AppleFrameLease& operator=(const AppleFrameLease&) = delete;
    AppleFrameLease(AppleFrameLease&& other) noexcept;
    AppleFrameLease& operator=(AppleFrameLease&& other) noexcept;
    ~AppleFrameLease();

    const AppleVideoFrameView& view() const noexcept { return view_; }
    bool                       valid() const noexcept { return state_ != nullptr; }

    struct State;

private:
    friend class VideoDecoder;

    AppleFrameLease(State* state, AppleVideoFrameView view) noexcept
        : state_(state), view_(rstd::move(view)) {}

    void reset() noexcept;

    State*              state_ { nullptr };
    AppleVideoFrameView view_;
};

struct AppleFramePull {
    NextFrame               status { NextFrame::Ok };
    Option<AppleFrameLease> frame;
};

struct DrmFramePull {
    NextFrame             status { NextFrame::Ok };
    Option<DrmFrameLease> frame;
};

class VideoDecoder {
public:
    static auto probe_native(ref<str> path) -> Result<ProbeResult, Error>;

    static auto open(ref<str> path, u32 target_width, u32 target_height, bool loop)
        -> Result<Box<VideoDecoder>, Error>;

    static auto open_with_vk(ref<str> path, u32 target_width, u32 target_height, bool loop,
                             const Producer& producer, const OpenOpts& opts = {})
        -> Result<Box<VideoDecoder>, Error>;

    static auto open_from_stream(InputStreamFactory make_stream, u32 target_width,
                                 u32 target_height, bool loop, const Producer* producer = nullptr,
                                 const OpenOpts& opts = {}) -> Result<Box<VideoDecoder>, Error>;

    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&)            = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    auto next_frame(Nv12Frame& out) -> Result<NextFrame, Error>;
    auto next_vk_frame() -> Result<VkFramePull, Error>;
    auto next_vaapi_frame() -> Result<VaapiFramePull, Error>;
    auto next_apple_frame() -> Result<AppleFramePull, Error>;
    auto next_drm_frame() -> Result<DrmFramePull, Error>;
    auto seek(f64 seconds) -> Result<empty, Error>;
    auto duration() const -> Option<f64>;

    FrameKind kind() const { return kind_; }
    bool      using_vk_frames() const { return kind_ == FrameKind::VulkanShared; }
    u32       width() const { return target_width_; }
    u32       height() const { return target_height_; }
    void      set_loop(bool loop) { loop_ = loop; }

    struct State;

    class StateOwner {
    public:
        explicit StateOwner(State* state): state_(state) {}
        ~StateOwner();

        StateOwner(const StateOwner&)            = delete;
        StateOwner& operator=(const StateOwner&) = delete;
        State*      operator->() const { return state_; }
        State&      operator*() const { return *state_; }

    private:
        State* state_;
    };

    explicit VideoDecoder(State* state): state_(state) {}

private:
    struct InputSpec {
        String                        path;
        Option<Box<dyn<InputStream>>> stream;
    };

    static auto build_internal(InputSpec input, u32 target_width, u32 target_height, bool loop,
                               void* prebuilt_hwdevice, FrameKind requested_kind, Error* err)
        -> Option<Box<VideoDecoder>>;

    int next_frame_(Nv12Frame& out, Error* err);
    int next_vk_frame_(VkFrameInfo& out, Error* err);
    int next_vaapi_frame_(VaapiFrameView& out, Error* err);
    int next_apple_frame_(AppleVideoFrameView& out, Error* err);
#if defined(__APPLE__)
    auto next_apple_frame_sync() -> Result<AppleFramePull, Error>;
    void start_apple_prefetch();
    void stop_apple_prefetch();
#endif

    StateOwner state_;
    u32        target_width_ {};
    u32        target_height_ {};
    bool       loop_ { false };
    FrameKind  kind_ { FrameKind::Sw };
};

} // namespace wavsen::video
