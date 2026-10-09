export module wavsen.video:vk_device;

import rstd;
export import vvk;

using namespace rstd::prelude;

export namespace wavsen::video
{

// Unified error carrier. Free-form message — callers either log or
// surface as an opaque diagnostic.
struct Error {
    String message;

    Error() = default;
    explicit Error(ref<str> value): message(String::make(value)) {}
    explicit Error(String value): message(rstd::move(value)) {}
};

// Per-queue-family record exposed to FFmpeg's AVVulkanDeviceContext::qf[].
// Producer creates 1 queue from each enumerated family and remembers each
// family's caps so FFmpeg can pick the right one for video decode/encode.
struct QueueFamily {
    u32          index;
    VkQueueFlags flags;
    /* Video codec ops the family advertises (only meaningful when the
     * device exposes VK_KHR_video_queue). 0 if unknown. */
    u32 video_caps;
};

// Bring up a VkInstance/VkPhysicalDevice/VkDevice with the extension set
// the bridge pool's Vulkan backend needs (DMA-BUF export, modifier
// import, semaphore SYNC_FD), plus a HOST_VISIBLE|COHERENT staging
// buffer pre-mapped at `width*height*4` bytes for repeated RGBA8
// uploads.
class Producer {
public:
    ~Producer();
    Producer(const Producer&)            = delete;
    Producer& operator=(const Producer&) = delete;

    static auto create(u32 width, u32 height) -> Result<Box<Producer>, Error>;

    // Pin the picked VkPhysicalDevice to the GPU that exposes
    // `render_node` (e.g. "/dev/dri/renderD128"). Empty string → behaves
    // identically to the no-arg overload (first device that advertises
    // the required extension set wins).
    static auto create_with_render_node(u32 width, u32 height, ref<str> render_node)
        -> Result<Box<Producer>, Error>;

    // Adopt a caller-owned VkInstance/VkDevice. The returned Producer
    // exposes them via instance() / device() etc. and feeds FFmpeg's
    // AVVulkanDeviceContext via make_shared_vulkan_hwdevice, but does
    // NOT destroy them in its destructor — the caller retains ownership.
    // `enabled_inst_exts` and `enabled_dev_exts` are the extension lists
    // the caller passed to vkCreateInstance / vkCreateDevice; they're
    // mirrored to FFmpeg verbatim (the pointed-to char* must outlive
    // the Producer).
    // No staging buffer / command pool is allocated — `upload_into`
    // returns an error on adopted Producers; this constructor is meant
    // for shared-device decode only.
    struct ExternalDeviceInfo {
        VkInstance       instance;
        VkPhysicalDevice physical_device;
        VkDevice         device;
        VkQueue          queue;
        u32              queue_family_index;
        // Full per-family caps list — typically as wide as
        // vkGetPhysicalDeviceQueueFamilyProperties returns. Used for
        // AVVulkanDeviceContext::qf[]. May be empty (FFmpeg falls back
        // to its own queue discovery).
        Vec<QueueFamily> queue_families;
        Vec<const char*> enabled_instance_extensions;
        Vec<const char*> enabled_device_extensions;
        u32              api_version { 0x00403000u }; // VK_API_VERSION_1_3
        u32              width {};
        u32              height {};
        // Optional DRM render-node info (renderD12X). drm_render_fd is
        // adopted (closed on Producer destruction) if >= 0.
        int drm_render_fd { -1 };
        u32 drm_render_major {};
        u32 drm_render_minor {};
        // Tables are copied; their loader and Vulkan objects must outlive this Producer.
        const vvk::InstanceDispatch* instance_dispatch {};
        const vvk::DeviceDispatch*   device_dispatch {};
    };
    static auto from_external(ExternalDeviceInfo info) -> Result<Box<Producer>, Error>;

    const vvk::InstanceDispatch& instance_dispatch() const { return instance_dispatch_; }
    const vvk::DeviceDispatch&   device_dispatch() const { return device_dispatch_; }
    VkInstance                   instance() const { return *instance_; }
    VkPhysicalDevice             physical_device() const { return *phys_; }
    VkDevice                     device() const { return *device_; }
    VkQueue                      queue() const { return *queue_; }
    u32                          queue_family_index() const { return queue_family_; }
    u32                          drm_render_major() const { return drm_render_major_; }
    u32                          drm_render_minor() const { return drm_render_minor_; }
    auto                         drm_render_node() const -> Option<String>;
    const rstd::uint8_t*         device_uuid() const { return have_uuid_ ? device_uuid_ : nullptr; }
    const rstd::uint8_t*         driver_uuid() const { return have_uuid_ ? driver_uuid_ : nullptr; }
    int                          drm_render_fd() const { return drm_render_file_.as_raw_fd(); }
    u32                          width() const { return width_; }
    u32                          height() const { return height_; }

    u32                instance_api_version() const { return instance_api_version_; }
    slice<const char*> enabled_instance_extensions() const { return enabled_inst_exts_.as_slice(); }
    slice<const char*> enabled_device_extensions() const { return enabled_dev_exts_.as_slice(); }
    slice<QueueFamily> queue_families() const { return queue_families_.as_slice(); }

    // Copy `data` (tightly packed RGBA8, `size` bytes == width*height*4)
    // into `target` VkImage. On success returns an exported sync_fd that
    // signals when the GPU is done writing — the bridge pool takes
    // ownership.
    auto upload_into(VkImage target, u32 target_width, u32 target_height, const rstd::uint8_t* data,
                     usize size) -> Result<int, Error>;

    Producer() = default;

private:
    // Internal builder used by the two public factories. Returns a
    // unique_ptr or nullptr; on failure populates `*err` with a message.
    static Option<Box<Producer>> build_(u32 width, u32 height, Option<ref<str>> render_node,
                                        Error* err);

    int upload_into_(VkImage target, u32 target_width, u32 target_height, const rstd::uint8_t* data,
                     usize size, Error* err);

    Option<vvk::VulkanLoader> loader_;
    vvk::InstanceDispatch     instance_dispatch_;
    vvk::DeviceDispatch       device_dispatch_;
    vvk::Instance             instance_;
    vvk::PhysicalDevice       phys_;
    vvk::Device               device_;
    u32                       queue_family_ {};
    vvk::Queue                queue_;

    vvk::CommandPool    cmd_pool_;
    vvk::CommandBuffers command_buffers_;
    vvk::CommandBuffer  cmd_;
    vvk::Semaphore      signal_sem_;
    vvk::Fence          done_fence_;
    bool                fence_pending_ { false };

    vvk::DeviceMemory staging_mem_;
    vvk::Buffer       staging_buf_;
    rstd::uint8_t*    staging_map_ { nullptr };
    VkDeviceSize      staging_size_ { 0 };

    u32            width_ {};
    u32            height_ {};
    u32            drm_render_major_ {};
    u32            drm_render_minor_ {};
    rstd::fs::File drm_render_file_;

    bool          have_uuid_ { false };
    rstd::uint8_t device_uuid_[16] {};
    rstd::uint8_t driver_uuid_[16] {};

    u32              instance_api_version_ {};
    Vec<const char*> enabled_inst_exts_;
    Vec<const char*> enabled_dev_exts_;
    Vec<QueueFamily> queue_families_;

    /* When true, ~Producer destroys the VkInstance/VkDevice and the
     * staging/command-pool/fence/semaphore resources it allocated. When
     * false (from_external path) only resources we created are torn
     * down; instance/device belong to the caller. */
    bool owns_device_ { true };
};

} // namespace wavsen::video
