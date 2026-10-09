export module vrento.resource_registry:external;
import rstd;
import vrento.resource;
import vrento.vulkan;

import :barrier;

export namespace vrento::resource_registry
{

using namespace rstd::prelude;

struct PreparedExternalFrame {
    FrameSurfaceLease    lease;
    PreparedBarrierBatch before_copy;
    PreparedBarrierBatch after_copy;

    auto clone() const -> PreparedExternalFrame {
        return PreparedExternalFrame {
            .lease       = lease,
            .before_copy = before_copy.clone(),
            .after_copy  = after_copy.clone(),
        };
    }
};

class ExternalResourceBridge {
public:
    auto Prepare(const vulkan::DeviceCapabilities& capabilities, FrameSurfaceLease lease,
                 rstd::uint32_t graphics_queue_family)
        -> Result<PreparedExternalFrame, resource::ResourceError> {
        if (! lease.valid()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::MissingDefinition,
                .message = rstd::format("invalid external frame surface lease"),
            });
        }
        if (lease.acquire.kind == FrameSurfaceAcquireKind::ExternalProtocol &&
            ! capabilities.external_memory_fd) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("external frame memory is unavailable"),
            });
        }
        const bool imports_foreign = lease.initial_queue_family == VK_QUEUE_FAMILY_FOREIGN_EXT;
        const bool exports_foreign = lease.final_queue_family == VK_QUEUE_FAMILY_FOREIGN_EXT;
        if ((imports_foreign || exports_foreign) && ! capabilities.foreign_queue) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = rstd::format("foreign queue ownership is unavailable"),
            });
        }

        auto range = VkImageSubresourceRange {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };
        PreparedExternalFrame prepared { .lease = rstd::move(lease) };
        const bool import_queue = prepared.lease.initial_queue_family != graphics_queue_family;
        prepared.before_copy.Add(PreparedImageBarrier {
            .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .barrier =
                VkImageMemoryBarrier {
                    .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .srcAccessMask = 0,
                    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .oldLayout     = prepared.lease.discard_content ? VK_IMAGE_LAYOUT_UNDEFINED
                                                                    : prepared.lease.initial_layout,
                    .newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .srcQueueFamilyIndex = import_queue ? prepared.lease.initial_queue_family
                                                        : VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex =
                        import_queue ? graphics_queue_family : VK_QUEUE_FAMILY_IGNORED,
                    .image            = prepared.lease.image.handle,
                    .subresourceRange = range,
                },
        });

        const bool export_queue = prepared.lease.final_queue_family != graphics_queue_family;
        prepared.after_copy.Add(PreparedImageBarrier {
            .src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .barrier =
                VkImageMemoryBarrier {
                    .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstAccessMask = 0,
                    .oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .newLayout     = prepared.lease.final_layout,
                    .srcQueueFamilyIndex =
                        export_queue ? graphics_queue_family : VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex =
                        export_queue ? prepared.lease.final_queue_family : VK_QUEUE_FAMILY_IGNORED,
                    .image            = prepared.lease.image.handle,
                    .subresourceRange = range,
                },
        });
        return Ok(rstd::move(prepared));
    }
};

} // namespace vrento::resource_registry
