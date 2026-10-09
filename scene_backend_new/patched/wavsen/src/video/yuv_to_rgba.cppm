export module wavsen.video:yuv_to_rgba;

import rstd;
import vvk;
import :vk_device;     // Error
import :video_decoder; // DrmFrameView

using namespace rstd::prelude;
using rstd::sync::Arc;

export namespace wavsen::video
{

// Coefficients for the YUV→RGB push constant. CPU side fills this from
// the source frame's colorspace + range; the shader applies it as
// `rgb = M * (ycbcr + offset)`.
struct ColorMatrix {
    float m_r[3]; // Y, Cb, Cr scalings producing R
    float m_g[3];
    float m_b[3];
    float offset[3]; // subtracted from (Y, Cb, Cr) before matmul
};

// Mirrors FFmpeg's `enum AVColorSpace` for the cases we actually
// branch on. Keeping our own enum avoids leaking <libavutil/pixfmt.h>
// into the public surface.
enum class ColorSpace : rstd::uint32_t
{
    Bt709  = 0,
    Bt601  = 1,
    Bt2020 = 2,
};

enum class ColorRange : rstd::uint32_t
{
    Limited = 0,
    Full    = 1,
};

enum class ConvertTarget : rstd::uint32_t
{
    BridgeForeign = 0,
    SampledLocal  = 1,
};

struct ConversionTargetView {
    VkImage       image { VK_NULL_HANDLE };
    VkImageView   view { VK_NULL_HANDLE };
    u32           width {};
    u32           height {};
    ConvertTarget kind { ConvertTarget::BridgeForeign };
};

struct ConversionLimits {
    u32 max_in_flight { 3 };
    u32 max_drm_imports { 32 };
};

struct ConversionReservationRequest {
    ConversionTargetView        target;
    Option<rstd::time::Instant> deadline;
};

struct ConversionSubmission {
    VkImage                          target { VK_NULL_HANDLE };
    u32                              width {};
    u32                              height {};
    ConvertTarget                    target_kind { ConvertTarget::BridgeForeign };
    VkImageLayout                    final_layout { VK_IMAGE_LAYOUT_UNDEFINED };
    rstd::uint32_t                   final_queue_family { VK_QUEUE_FAMILY_IGNORED };
    u64                              content_revision { 0 };
    vvk::SubmissionToken             readiness;
    vvk::TimelineExecutionDependency execution_dependency;
    int                              sync_fd { -1 };

    bool submitted() const noexcept {
        return target != VK_NULL_HANDLE && width != u32() && height != u32() &&
               content_revision != u64() && readiness.valid() && execution_dependency.valid() &&
               execution_dependency.completion == readiness;
    }

    ConversionSubmission clone() const {
        return ConversionSubmission {
            .target               = target,
            .width                = width,
            .height               = height,
            .target_kind          = target_kind,
            .final_layout         = final_layout,
            .final_queue_family   = final_queue_family,
            .content_revision     = content_revision,
            .readiness            = readiness,
            .execution_dependency = execution_dependency.clone(),
            .sync_fd              = sync_fd,
        };
    }
};

// Derive the ColorMatrix to push to the shader. Defaults to BT.709
// limited when either argument is the canonical "unknown" sentinel.
ColorMatrix make_color_matrix(ColorSpace cs, ColorRange cr);

class YuvToRgba;

class ConversionReservation {
public:
    ConversionReservation(const ConversionReservation&)            = delete;
    ConversionReservation& operator=(const ConversionReservation&) = delete;
    ConversionReservation(ConversionReservation&& other) noexcept;
    ConversionReservation& operator=(ConversionReservation&& other) noexcept;
    ~ConversionReservation();

    bool valid() const noexcept { return state_ != nullptr; }

    struct State;

private:
    friend class YuvToRgba;

    explicit ConversionReservation(State* state) noexcept: state_(state) {}
    void reset() noexcept;

    State* state_ { nullptr };
};

class YuvToRgba {
public:
    ~YuvToRgba();
    YuvToRgba()                            = default;
    YuvToRgba(const YuvToRgba&)            = delete;
    YuvToRgba& operator=(const YuvToRgba&) = delete;

    // Borrowed Vulkan objects and their loader must outlive the converter.
    static auto create(const vvk::InstanceDispatch& instance_dispatch, VkPhysicalDevice phys,
                       const vvk::DeviceDispatch& device_dispatch, u32 queue_family, VkQueue queue,
                       u32 max_w, u32 max_h) -> Result<Box<YuvToRgba>, Error>;

    auto convert_nv12(VkImage dst, u32 dst_w, u32 dst_h, const rstd::uint8_t* nv12, usize nv12_size,
                      const ColorMatrix& cm) -> Result<int, Error>;
    auto convert_nv12(VkImage dst, u32 dst_w, u32 dst_h, const rstd::uint8_t* nv12, usize nv12_size,
                      const ColorMatrix& cm, ConvertTarget target) -> Result<int, Error>;
    auto submit_nv12(VkImage dst, u32 dst_w, u32 dst_h, const rstd::uint8_t* nv12, usize nv12_size,
                     const ColorMatrix& cm, ConvertTarget target)
        -> Result<ConversionSubmission, Error>;

    auto submit_av_vk_frame(ConversionReservation&& reservation, VkFrameLease&& frame,
                            const ColorMatrix& cm) -> Result<ConversionSubmission, Error>;

    auto configure_pipeline(ConversionLimits limits) -> Result<empty, Error>;
    auto reserve(ConversionReservationRequest request)
        -> Result<Option<ConversionReservation>, Error>;
    auto submit_drm_prime(ConversionReservation&& reservation, DrmFrameLease&& drm,
                          const ColorMatrix& cm) -> Result<Option<ConversionSubmission>, Error>;
    auto reclaim_submissions() -> Result<usize, Error>;
    auto drain_submissions(u64 timeout_ns) -> Result<empty, Error>;
    auto invalidate_targets() -> Result<empty, Error>;

    vvk::CompletionObservation   poll_completion() const noexcept;
    vvk::CompletionObservation   wait_completion(const vvk::SubmissionToken& required,
                                                 u64 timeout_ns) const noexcept;
    Option<vvk::SubmissionToken> last_submission_readiness() const noexcept;

private:
    bool init(const vvk::InstanceDispatch& instance_dispatch, VkPhysicalDevice phys,
              const vvk::DeviceDispatch& device_dispatch, u32 queue_family, VkQueue queue,
              u32 max_w, u32 max_h, Error* err);
    int  convert_nv12_(VkImage dst, rstd::uint32_t dst_w, rstd::uint32_t dst_h,
                       const rstd::uint8_t* nv12, usize nv12_size, const ColorMatrix& cm,
                       ConvertTarget target, Error* err);
    auto convert_av_vk_frame_(ConversionReservation& reservation, VkFrameAccess& access,
                              const ColorMatrix& cm) -> Result<int, Error>;
    void publish_submission(VkImage dst, u32 dst_w, u32 dst_h, ConvertTarget target,
                            u64 completion_value);

    struct ConversionPipelineState;

    vvk::InstanceDispatch instance_dispatch_;
    vvk::DeviceDispatch   device_dispatch_;
    vvk::Instance         instance_;
    vvk::PhysicalDevice   phys_;
    vvk::Device           device_;
    vvk::Queue            queue_;
    u32                   queue_family_ {};

    u32 max_w_ {};
    u32 max_h_ {};

    vvk::ShaderModule        shader_;
    vvk::DescriptorSetLayout dsl_;
    vvk::PipelineLayout      pipeline_layout_;
    vvk::Pipeline            pipeline_;
    vvk::Sampler             sampler_;

    vvk::DeviceMemory y_memory_;
    vvk::Image        y_image_;
    vvk::ImageView    y_view_;

    vvk::DeviceMemory uv_memory_;
    vvk::Image        uv_image_;
    vvk::ImageView    uv_view_;

    vvk::DeviceMemory staging_mem_;
    vvk::Buffer       staging_buf_;
    rstd::uint8_t*    staging_map_ { nullptr };
    VkDeviceSize      staging_size_ { 0 };

    vvk::CommandPool    cmd_pool_;
    vvk::CommandBuffers software_command_buffers_;
    vvk::CommandBuffer  software_command_;

    vvk::Semaphore software_export_semaphore_;
    vvk::Fence     software_fence_;
    bool           software_fence_pending_ { false };

    u64                                           completion_value_ { 0 };
    Option<Arc<vvk::TimelineSemaphoreGeneration>> completion_timeline_;
    Option<vvk::TimelineCompletionObserver>       completion_observer_;
    Option<ConversionSubmission>                  last_submission_;

    Option<Arc<vvk::DescriptorArenaGeneration>> descriptor_arena_;
    vvk::DescriptorSetLease                     software_descriptor_set_;

    vvk::ImageView software_target_view_;

    ConversionPipelineState* pipeline_state_ { nullptr };
};

} // namespace wavsen::video
