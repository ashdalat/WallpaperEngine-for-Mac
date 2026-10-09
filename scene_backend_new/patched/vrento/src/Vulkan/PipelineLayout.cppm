export module vrento.pipeline_layout;
import rstd;
import vrento.resource_registry;
export import vrento.vulkan;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace vrento::vulkan
{

struct PipelineLayoutPolicy {
    Option<u32> shared_set;
};

struct PipelineLayoutBindingRequirement {
    u32         binding {};
    u32         descriptor_type {};
    u32         descriptor_count { u32(1) };
    u32         stage_flags {};
    Option<u64> shared_identity;
};

struct PipelineLayoutSetRequirement {
    u32                                   set {};
    bool                                  push_descriptor { false };
    Vec<PipelineLayoutBindingRequirement> bindings;
};

struct PipelineLayoutRequirement {
    resource::PipelineUseHandle       pipeline;
    Vec<PipelineLayoutSetRequirement> descriptor_sets;
    Vec<VkPushConstantRange>          push_constants;
};

struct PlannedPipelineLayoutFamily {
    resource_registry::PipelineLayoutRequest request;
    Vec<resource::PipelineUseHandle>         pipelines;
};

struct PipelineLayoutConflict {
    resource::PipelineUseHandle pipeline;
    usize                       family {};
    String                      message;
};

struct GlobalBindingIdentity {
    u32 binding {};
    u64 identity {};
};

struct PipelineLayoutPlan {
    Vec<PlannedPipelineLayoutFamily> families;
    Vec<PipelineLayoutConflict>      conflicts;
    Vec<GlobalBindingIdentity>       global_bindings;
};

struct PipelineLayoutAssignment {
    resource::PipelineUseHandle    pipeline;
    resource::PipelineLayoutHandle layout;

    friend bool operator==(const PipelineLayoutAssignment&,
                           const PipelineLayoutAssignment&) = default;
};

struct PipelineLayoutAssignments {
    Vec<PipelineLayoutAssignment> entries;

    auto Resolve(resource::PipelineUseHandle pipeline) const
        -> Option<resource::PipelineLayoutHandle> {
        for (const auto& entry : entries) {
            if (entry.pipeline == pipeline) {
                return Some(resource::PipelineLayoutHandle {
                    .index      = entry.layout.index,
                    .generation = entry.layout.generation,
                });
            }
        }
        return None();
    }

    friend bool operator==(const PipelineLayoutAssignments& lhs,
                           const PipelineLayoutAssignments& rhs) {
        if (lhs.entries.len() != rhs.entries.len()) return false;
        for (usize index {}; index < lhs.entries.len(); ++index) {
            if (lhs.entries[index] != rhs.entries[index]) return false;
        }
        return true;
    }
};

namespace pipeline_layout_detail
{

inline auto Error(ref<str> message) -> Result<empty, resource::ResourceError> {
    return Err(resource::ResourceError {
        .kind    = resource::ResourceErrorKind::MissingDefinition,
        .message = String::make(message),
    });
}

inline auto FindBinding(Vec<VkDescriptorSetLayoutBinding>& bindings, u32 binding)
    -> VkDescriptorSetLayoutBinding* {
    for (auto& candidate : bindings) {
        if (candidate.binding == binding.to_primitive()) return rstd::addressof(candidate);
    }
    return nullptr;
}

inline auto MergeBinding(Vec<VkDescriptorSetLayoutBinding>&      target,
                         const PipelineLayoutBindingRequirement& requirement)
    -> Result<empty, resource::ResourceError> {
    auto existing = FindBinding(target, requirement.binding);
    if (existing == nullptr) {
        target.push(VkDescriptorSetLayoutBinding {
            .binding = requirement.binding.to_primitive(),
            .descriptorType =
                static_cast<VkDescriptorType>(requirement.descriptor_type.to_primitive()),
            .descriptorCount    = requirement.descriptor_count.to_primitive(),
            .stageFlags         = requirement.stage_flags.to_primitive(),
            .pImmutableSamplers = nullptr,
        });
        return Ok(empty {});
    }
    if (existing->descriptorType !=
            static_cast<VkDescriptorType>(requirement.descriptor_type.to_primitive()) ||
        existing->descriptorCount != requirement.descriptor_count.to_primitive()) {
        return Error("descriptor binding layouts are incompatible"_str);
    }
    existing->stageFlags |= requirement.stage_flags.to_primitive();
    return Ok(empty {});
}

inline auto MergePushConstants(Vec<VkPushConstantRange>&       target,
                               const Vec<VkPushConstantRange>& source)
    -> Result<empty, resource::ResourceError> {
    for (const auto& range : source) {
        if (range.stageFlags == 0 || range.size == 0 || range.offset % 4 != 0 ||
            range.size % 4 != 0) {
            return Error("invalid pipeline push constant range"_str);
        }
        VkPushConstantRange* exact = nullptr;
        for (auto& existing : target) {
            if (existing.offset == range.offset && existing.size == range.size) {
                exact = rstd::addressof(existing);
                continue;
            }
            const auto existing_end = existing.offset + existing.size;
            const auto range_end    = range.offset + range.size;
            if (existing.offset < range_end && range.offset < existing_end &&
                (existing.stageFlags & range.stageFlags) != 0) {
                return Error("pipeline push constant ranges are incompatible"_str);
            }
        }
        if (exact != nullptr) {
            exact->stageFlags |= range.stageFlags;
        } else {
            auto copied = range;
            target.push(rstd::move(copied));
        }
    }
    return Ok(empty {});
}

inline auto MergeGlobalSet(Vec<VkDescriptorSetLayoutBinding>&  target,
                           Vec<GlobalBindingIdentity>&         identities,
                           const PipelineLayoutSetRequirement& set)
    -> Result<empty, resource::ResourceError> {
    if (set.push_descriptor) return Error("shared descriptor set cannot use push descriptors"_str);
    for (const auto& binding : set.bindings) {
        if (binding.shared_identity.is_none()) {
            return Error("shared descriptor set requires resource identity"_str);
        }
        for (const auto& existing : identities) {
            if (existing.binding != binding.binding) continue;
            if (existing.identity != *binding.shared_identity) {
                return Error("global descriptor binding identities are incompatible"_str);
            }
        }
        bool known = false;
        for (const auto& existing : identities) {
            if (existing.binding == binding.binding) known = true;
        }
        if (! known) {
            identities.push(GlobalBindingIdentity {
                .binding  = binding.binding,
                .identity = *binding.shared_identity,
            });
        }
        auto merged = MergeBinding(target, binding);
        if (merged.is_err()) return merged;
    }
    return Ok(empty {});
}

inline auto MergeLocalRequirement(resource_registry::PipelineLayoutRequest& family,
                                  const PipelineLayoutRequirement&          requirement,
                                  bool                        push_descriptor_supported,
                                  const PipelineLayoutPolicy& policy)
    -> Result<empty, resource::ResourceError> {
    auto candidate = family.clone();
    u32  push_set { u32::MAX };
    for (u32 set_index {}; set_index < as_cast<u32>(candidate.descriptor_sets.len()); ++set_index) {
        if (candidate.descriptor_sets[as_cast<usize>(set_index)].push_descriptor) {
            push_set = set_index;
        }
    }
    for (const auto& set : requirement.descriptor_sets) {
        if (policy.shared_set.is_some() && set.set == *policy.shared_set) continue;
        if (set.set == u32::MAX) return Error("invalid descriptor set index"_str);
        const auto required_size = rstd::as_cast<usize>(set.set) + usize(1);
        while (candidate.descriptor_sets.len() < required_size) {
            candidate.descriptor_sets.push(DescriptorSetInfo {});
        }
        auto&      target = candidate.descriptor_sets[as_cast<usize>(set.set)];
        const bool push   = set.push_descriptor && push_descriptor_supported;
        if (! target.bindings.is_empty() && target.push_descriptor != push) {
            return Error("local descriptor set modes are incompatible"_str);
        }
        if (push && push_set != u32::MAX && push_set != set.set) {
            return Error(
                "a pipeline layout family cannot contain multiple push descriptor sets"_str);
        }
        target.push_descriptor = push;
        if (push) push_set = set.set;
        for (const auto& binding : set.bindings) {
            if (binding.shared_identity.is_some()) {
                return Error("shared resources must use the configured shared set"_str);
            }
            auto merged = MergeBinding(target.bindings, binding);
            if (merged.is_err()) return merged;
        }
    }
    family = rstd::move(candidate);
    return Ok(empty {});
}

inline void SortBindings(resource_registry::PipelineLayoutRequest& request) {
    for (auto& set : request.descriptor_sets) {
        rstd::slice_::sort_unstable_by(set.bindings.as_mut_slice().as_mut_ref(),
                                       [](const auto& lhs, const auto& rhs) {
                                           return lhs.binding < rhs.binding;
                                       });
    }
    rstd::slice_::sort_unstable_by(request.push_constants.as_mut_slice().as_mut_ref(),
                                   [](const auto& lhs, const auto& rhs) {
                                       if (lhs.offset != rhs.offset) return lhs.offset < rhs.offset;
                                       if (lhs.size != rhs.size) return lhs.size < rhs.size;
                                       return lhs.stageFlags < rhs.stageFlags;
                                   });
}

inline auto BindingCount(const resource_registry::PipelineLayoutRequest& request) -> usize {
    usize count {};
    for (const auto& set : request.descriptor_sets) count += set.bindings.len();
    return count;
}

struct DescriptorCounts {
    rstd::uint64_t samplers {};
    rstd::uint64_t uniform_buffers {};
    rstd::uint64_t uniform_buffers_dynamic {};
    rstd::uint64_t storage_buffers {};
    rstd::uint64_t storage_buffers_dynamic {};
    rstd::uint64_t sampled_images {};
    rstd::uint64_t storage_images {};
    rstd::uint64_t input_attachments {};
    rstd::uint64_t resources {};
};

inline void CountBinding(DescriptorCounts& counts, const VkDescriptorSetLayoutBinding& binding) {
    const auto count = static_cast<rstd::uint64_t>(binding.descriptorCount);
    counts.resources += count;
    switch (binding.descriptorType) {
    case VK_DESCRIPTOR_TYPE_SAMPLER: counts.samplers += count; break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        counts.samplers += count;
        counts.sampled_images += count;
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: counts.sampled_images += count; break;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: counts.storage_images += count; break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: counts.uniform_buffers += count; break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: counts.uniform_buffers_dynamic += count; break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: counts.storage_buffers += count; break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: counts.storage_buffers_dynamic += count; break;
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: counts.input_attachments += count; break;
    default: break;
    }
}

inline bool Exceeds(rstd::uint64_t count, rstd::uint32_t limit) {
    return limit != 0 && count > limit;
}

inline auto ValidateDescriptorLimits(const resource_registry::PipelineLayoutRequest& request,
                                     const VkPhysicalDeviceLimits&                   limits)
    -> Result<empty, resource::ResourceError> {
    if (limits.maxBoundDescriptorSets != 0 &&
        request.descriptor_sets.len() > usize(limits.maxBoundDescriptorSets)) {
        return Error("pipeline descriptor set count exceeds the device limit"_str);
    }

    constexpr array<VkShaderStageFlagBits, 5> stages {
        VK_SHADER_STAGE_VERTEX_BIT,
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        VK_SHADER_STAGE_GEOMETRY_BIT,
        VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    DescriptorCounts           totals;
    array<DescriptorCounts, 5> per_stage;
    for (const auto& set : request.descriptor_sets) {
        for (const auto& binding : set.bindings) {
            CountBinding(totals, binding);
            for (usize index {}; index < stages.len(); ++index) {
                if ((binding.stageFlags & stages[index]) != 0) {
                    CountBinding(per_stage[index], binding);
                }
            }
        }
    }

    if (Exceeds(totals.samplers, limits.maxDescriptorSetSamplers) ||
        Exceeds(totals.uniform_buffers, limits.maxDescriptorSetUniformBuffers) ||
        Exceeds(totals.uniform_buffers_dynamic, limits.maxDescriptorSetUniformBuffersDynamic) ||
        Exceeds(totals.storage_buffers, limits.maxDescriptorSetStorageBuffers) ||
        Exceeds(totals.storage_buffers_dynamic, limits.maxDescriptorSetStorageBuffersDynamic) ||
        Exceeds(totals.sampled_images, limits.maxDescriptorSetSampledImages) ||
        Exceeds(totals.storage_images, limits.maxDescriptorSetStorageImages) ||
        Exceeds(totals.input_attachments, limits.maxDescriptorSetInputAttachments)) {
        return Error("pipeline descriptor layout exceeds a device set limit"_str);
    }
    for (const auto& counts : per_stage) {
        if (Exceeds(counts.samplers, limits.maxPerStageDescriptorSamplers) ||
            Exceeds(counts.uniform_buffers + counts.uniform_buffers_dynamic,
                    limits.maxPerStageDescriptorUniformBuffers) ||
            Exceeds(counts.storage_buffers + counts.storage_buffers_dynamic,
                    limits.maxPerStageDescriptorStorageBuffers) ||
            Exceeds(counts.sampled_images, limits.maxPerStageDescriptorSampledImages) ||
            Exceeds(counts.storage_images, limits.maxPerStageDescriptorStorageImages) ||
            Exceeds(counts.input_attachments, limits.maxPerStageDescriptorInputAttachments) ||
            Exceeds(counts.resources, limits.maxPerStageResources)) {
            return Error("pipeline descriptor layout exceeds a device stage limit"_str);
        }
    }
    return Ok(empty {});
}

} // namespace pipeline_layout_detail

inline auto PlanPipelineLayouts(slice<PipelineLayoutRequirement> requirements,
                                const PipelineLayoutPolicy& policy, bool push_descriptor_supported,
                                u32                           max_push_descriptors   = u32::MAX,
                                u32                           max_push_constant_size = u32::MAX,
                                const VkPhysicalDeviceLimits* descriptor_limits      = nullptr)
    -> Result<PipelineLayoutPlan, resource::ResourceError> {
    using namespace pipeline_layout_detail;

    if (policy.shared_set == Some(u32::MAX) ||
        (policy.shared_set.is_some() && descriptor_limits != nullptr &&
         policy.shared_set->to_primitive() >= descriptor_limits->maxBoundDescriptorSets))
        return Err(resource::ResourceError {
            .kind    = resource::ResourceErrorKind::MissingDefinition,
            .message = "invalid shared descriptor set index"_Str,
        });
    Vec<VkDescriptorSetLayoutBinding> global_bindings;
    Vec<GlobalBindingIdentity>        global_identities;
    Vec<VkPushConstantRange>          canonical_push_constants;
    for (const auto& requirement : requirements) {
        if (! requirement.pipeline.Valid()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::MissingDefinition,
                .message = "invalid pipeline layout requirement"_Str,
            });
        }
        for (const auto& set : requirement.descriptor_sets) {
            if (policy.shared_set.is_none() || set.set != *policy.shared_set) continue;
            auto merged = MergeGlobalSet(global_bindings, global_identities, set);
            if (merged.is_err()) return Err(rstd::move(merged).unwrap_err_unchecked());
        }
        auto merged_push = MergePushConstants(canonical_push_constants, requirement.push_constants);
        if (merged_push.is_err()) {
            return Err(rstd::move(merged_push).unwrap_err_unchecked());
        }
    }
    for (const auto& range : canonical_push_constants) {
        const auto end = static_cast<rstd::uint64_t>(range.offset) + range.size;
        if (end > max_push_constant_size.to_primitive()) {
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::MissingDefinition,
                .message = "pipeline push constant range exceeds the device limit"_Str,
            });
        }
    }

    resource_registry::PipelineLayoutRequest base;
    rstd::slice_::sort_unstable_by(global_identities.as_mut_slice().as_mut_ref(),
                                   [](const auto& lhs, const auto& rhs) {
                                       return lhs.binding < rhs.binding;
                                   });
    if (policy.shared_set.is_some()) {
        while (base.descriptor_sets.len() <= as_cast<usize>(*policy.shared_set))
            base.descriptor_sets.push(DescriptorSetInfo {});
        base.descriptor_sets[as_cast<usize>(*policy.shared_set)].bindings =
            rstd::move(global_bindings);
    }
    base.push_constants = rstd::move(canonical_push_constants);

    PipelineLayoutPlan plan {
        .global_bindings = rstd::move(global_identities),
    };
    for (const auto& requirement : requirements) {
        bool                                     assigned { false };
        usize                                    best_family { usize::MAX };
        usize                                    best_cost { usize::MAX };
        resource_registry::PipelineLayoutRequest best_request;
        for (usize family_index {}; family_index < plan.families.len(); ++family_index) {
            auto candidate = plan.families[family_index].request.clone();
            auto merged =
                MergeLocalRequirement(candidate, requirement, push_descriptor_supported, policy);
            if (merged.is_ok() && descriptor_limits != nullptr) {
                merged = ValidateDescriptorLimits(candidate, *descriptor_limits);
            }
            if (merged.is_err()) {
                auto error = rstd::move(merged).unwrap_err_unchecked();
                plan.conflicts.push(PipelineLayoutConflict {
                    .pipeline =
                        resource::PipelineUseHandle {
                            .index      = requirement.pipeline.index,
                            .generation = requirement.pipeline.generation,
                        },
                    .family  = family_index,
                    .message = rstd::move(error.message),
                });
                continue;
            }
            auto cost = BindingCount(candidate) - BindingCount(plan.families[family_index].request);
            if (assigned && cost >= best_cost) continue;
            assigned     = true;
            best_family  = family_index;
            best_cost    = cost;
            best_request = rstd::move(candidate);
        }
        if (assigned) {
            auto& family   = plan.families[best_family];
            family.request = rstd::move(best_request);
            family.pipelines.push(resource::PipelineUseHandle {
                .index      = requirement.pipeline.index,
                .generation = requirement.pipeline.generation,
            });
            continue;
        }

        auto request = base.clone();
        auto merged =
            MergeLocalRequirement(request, requirement, push_descriptor_supported, policy);
        if (merged.is_ok() && descriptor_limits != nullptr) {
            merged = ValidateDescriptorLimits(request, *descriptor_limits);
        }
        if (merged.is_err()) return Err(rstd::move(merged).unwrap_err_unchecked());
        auto pipelines = Vec<resource::PipelineUseHandle>::make();
        pipelines.push(resource::PipelineUseHandle {
            .index      = requirement.pipeline.index,
            .generation = requirement.pipeline.generation,
        });
        plan.families.push(PlannedPipelineLayoutFamily {
            .request   = rstd::move(request),
            .pipelines = rstd::move(pipelines),
        });
    }
    for (auto& family : plan.families) {
        for (auto& set : family.request.descriptor_sets) {
            if (! set.push_descriptor) continue;
            rstd::uint32_t descriptor_count {};
            for (const auto& binding : set.bindings) {
                descriptor_count += binding.descriptorCount;
            }
            if (descriptor_count > max_push_descriptors.to_primitive()) {
                set.push_descriptor = false;
            }
        }
        SortBindings(family.request);
        if (descriptor_limits != nullptr) {
            auto valid = ValidateDescriptorLimits(family.request, *descriptor_limits);
            if (valid.is_err()) return Err(rstd::move(valid).unwrap_err_unchecked());
        }
    }
    return Ok(rstd::move(plan));
}

} // namespace vrento::vulkan
