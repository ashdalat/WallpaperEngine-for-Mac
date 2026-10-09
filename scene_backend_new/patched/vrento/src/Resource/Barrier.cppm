export module vrento.resource_registry:barrier;
import rstd;
import vrento.vulkan;

export namespace vrento::resource_registry
{

using namespace rstd::prelude;

enum class TextureStateKind
{
    Undefined,
    Sampled,
    DepthSampled,
    TransferSource,
    TransferDestination,
    ColorAttachment,
    DepthAttachment,
};

struct TextureSubresourceRange {
    VkImageAspectFlags aspect { VK_IMAGE_ASPECT_COLOR_BIT };
    u32                base_mip_level { 0 };
    u32                level_count { VK_REMAINING_MIP_LEVELS };
    u32                base_array_layer { 0 };
    u32                layer_count { VK_REMAINING_ARRAY_LAYERS };
};

struct PreparedImageBarrier {
    VkPipelineStageFlags src_stage { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
    VkPipelineStageFlags dst_stage { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT };
    VkDependencyFlags    dependency { VK_DEPENDENCY_BY_REGION_BIT };
    VkImageMemoryBarrier barrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    };

    void Record(vvk::CommandBuffer& command) const {
        command.PipelineBarrier(src_stage, dst_stage, dependency, barrier);
    }
};

class PreparedBarrierBatch {
public:
    void Add(PreparedImageBarrier barrier) { m_barriers.push(rstd::move(barrier)); }

    void Clear() { m_barriers.clear(); }

    void Record(vvk::CommandBuffer& command) const {
        for (const auto& barrier : m_barriers) barrier.Record(command);
    }

    auto Size() const noexcept -> usize { return m_barriers.len(); }
    bool Empty() const noexcept { return m_barriers.is_empty(); }

    auto clone() const -> PreparedBarrierBatch {
        PreparedBarrierBatch cloned;
        for (const auto& barrier : m_barriers) cloned.Add(barrier);
        return cloned;
    }

private:
    Vec<PreparedImageBarrier> m_barriers;
};

} // namespace vrento::resource_registry
