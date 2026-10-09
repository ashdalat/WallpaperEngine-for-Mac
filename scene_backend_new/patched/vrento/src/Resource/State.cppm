export module vrento.resource_registry:state;
import rstd;
import vrento.resource;
import vrento.vulkan;

import :prepared;
import :barrier;

using rstd::collections::HashMap;

export namespace vrento::resource_registry
{

using namespace rstd::prelude;

struct TextureStatePreparer {
    using Trait                  = TextureStatePreparer;
    static constexpr bool direct = false;

    template<typename Self, typename = void>
    struct Api {
        using Trait = TextureStatePreparer;

        auto Prepare(resource::TextureUseHandle use, TextureStateKind target,
                     TextureSubresourceRange range = {}, bool discard = false)
            -> Option<PreparedImageBarrier> {
            return rstd::trait_call<0>(this, use, target, range, discard);
        }

        bool Set(resource::TextureUseHandle use, TextureStateKind state) {
            return rstd::trait_call<1>(this, use, state);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::Prepare, &T::Set>;
};

class ResourceStateTracker {
public:
    bool Compile(const resource::ResourcePlan& plan, const PreparedResourceTable& resources) {
        m_textures.clear();
        m_uses.clear();
        if (plan.generation != resources.Generation()) return false;
        for (const auto& entry : plan.textures) {
            auto prepared = resources.Resolve(entry.handle);
            if (prepared.is_none()) return false;
            auto texture = (**prepared).resource;
            if (m_uses.insert(entry.handle, texture).is_some()) return false;

            auto image    = (**prepared).image.getActive();
            auto existing = m_textures.get(texture);
            if (existing.is_some()) {
                if ((**existing).image.handle != image.handle) return false;
                continue;
            }
            const bool preserve_across_frames =
                (entry.request.content &
                 resource::TextureContentFlag(resource::TextureContent::PreserveAcrossFrames)) !=
                u32();
            auto initial = entry.request.kind == resource::TextureRequestKind::Imported ||
                                   preserve_across_frames
                               ? TextureStateKind::Sampled
                               : TextureStateKind::Undefined;
            if (m_textures
                    .insert(texture,
                            TrackedTexture {
                                .image = image,
                                .state = initial,
                            })
                    .is_some()) {
                return false;
            }
        }
        return true;
    }

    auto Prepare(resource::TextureUseHandle use, TextureStateKind target,
                 TextureSubresourceRange range = {}, bool discard = false)
        -> Option<PreparedImageBarrier> {
        auto texture = m_uses.get(use);
        if (texture.is_none()) return None();
        auto tracked = m_textures.get_mut(**texture);
        if (tracked.is_none()) return None();

        auto source      = State(discard ? TextureStateKind::Undefined : (**tracked).state);
        auto destination = State(target);
        PreparedImageBarrier prepared {
            .src_stage = source.stage,
            .dst_stage = destination.stage,
            .barrier =
                VkImageMemoryBarrier {
                    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .pNext               = nullptr,
                    .srcAccessMask       = source.access,
                    .dstAccessMask       = destination.access,
                    .oldLayout           = source.layout,
                    .newLayout           = destination.layout,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image               = (**tracked).image.handle,
                    .subresourceRange =
                        VkImageSubresourceRange {
                            .aspectMask     = range.aspect,
                            .baseMipLevel   = range.base_mip_level.to_primitive(),
                            .levelCount     = range.level_count.to_primitive(),
                            .baseArrayLayer = range.base_array_layer.to_primitive(),
                            .layerCount     = range.layer_count.to_primitive(),
                        },
                },
        };
        (**tracked).state = target;
        return Some(rstd::move(prepared));
    }

    bool Set(resource::TextureUseHandle use, TextureStateKind state) {
        auto texture = m_uses.get(use);
        if (texture.is_none()) return false;
        auto tracked = m_textures.get_mut(**texture);
        if (tracked.is_none()) return false;
        (**tracked).state = state;
        return true;
    }

    void Reset() {
        m_textures.clear();
        m_uses.clear();
    }

    auto Size() const noexcept -> usize { return m_textures.len(); }

private:
    struct StateInfo {
        VkPipelineStageFlags stage;
        VkAccessFlags        access;
        VkImageLayout        layout;
    };

    struct TrackedTexture {
        vulkan::ImageParameters image;
        TextureStateKind        state { TextureStateKind::Undefined };
    };

    static auto State(TextureStateKind state) -> StateInfo {
        switch (state) {
        case TextureStateKind::Sampled:
            return {
                .stage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .access = VK_ACCESS_SHADER_READ_BIT,
                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
        case TextureStateKind::DepthSampled:
            return {
                .stage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .access = VK_ACCESS_SHADER_READ_BIT,
                .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            };
        case TextureStateKind::TransferSource:
            return {
                .stage  = VK_PIPELINE_STAGE_TRANSFER_BIT,
                .access = VK_ACCESS_TRANSFER_READ_BIT,
                .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            };
        case TextureStateKind::TransferDestination:
            return {
                .stage  = VK_PIPELINE_STAGE_TRANSFER_BIT,
                .access = VK_ACCESS_TRANSFER_WRITE_BIT,
                .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            };
        case TextureStateKind::ColorAttachment:
            return {
                .stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .access =
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            };
        case TextureStateKind::DepthAttachment:
            return {
                .stage  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                .access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            };
        case TextureStateKind::Undefined:
        default:
            return {
                .stage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                .access = 0,
                .layout = VK_IMAGE_LAYOUT_UNDEFINED,
            };
        }
    }

    using TextureMap    = HashMap<resource::TextureHandle, TrackedTexture>;
    using TextureUseMap = HashMap<resource::TextureUseHandle, resource::TextureHandle>;

    TextureMap    m_textures;
    TextureUseMap m_uses;
};

} // namespace vrento::resource_registry

export namespace rstd
{

template<>
struct Impl<vrento::resource_registry::TextureStatePreparer,
            vrento::resource_registry::ResourceStateTracker>
    : ImplBase<vrento::resource_registry::ResourceStateTracker> {
    auto Prepare(vrento::resource::TextureUseHandle                 use,
                 vrento::resource_registry::TextureStateKind        target,
                 vrento::resource_registry::TextureSubresourceRange range, bool discard)
        -> Option<vrento::resource_registry::PreparedImageBarrier> {
        return this->self().Prepare(use, target, range, discard);
    }

    bool Set(vrento::resource::TextureUseHandle          use,
             vrento::resource_registry::TextureStateKind state) {
        return this->self().Set(use, state);
    }
};

} // namespace rstd
