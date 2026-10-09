#include <rstd/macro.hpp>

import rstd;
import vrento.pipeline_layout;
import vrento.resource;
import vrento.vulkan;

using namespace rstd::prelude;
using namespace vrento::vulkan;

static auto Requirement(u64 index, u32 set_index, bool shared) -> PipelineLayoutRequirement {
    PipelineLayoutRequirement requirement {
        .pipeline = { .index = index, .generation = u64(1) },
    };
    PipelineLayoutSetRequirement set { .set = set_index };
    set.bindings.push(PipelineLayoutBindingRequirement {
        .binding          = u32(1),
        .descriptor_type  = u32(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
        .descriptor_count = u32(1),
        .stage_flags      = u32(VK_SHADER_STAGE_VERTEX_BIT),
        .shared_identity  = shared ? Some(u64(42)) : None<u64>(),
    });
    requirement.descriptor_sets.push(rstd::move(set));
    return requirement;
}

int main() {
    auto requirements = Vec<PipelineLayoutRequirement>::make();
    requirements.push(Requirement(u64(1), u32(), false));
    auto local = PlanPipelineLayouts(requirements.as_slice(), {}, false);
    rstd_assert(local.is_ok());
    rstd_assert(local->global_bindings.is_empty());
    rstd_assert(local->families.len() == usize(1));
    rstd_assert(local->families[usize()].request.descriptor_sets.len() == usize(1));

    requirements.clear();
    requirements.push(Requirement(u64(1), u32(2), true));
    requirements.push(Requirement(u64(2), u32(2), true));
    PipelineLayoutPolicy policy { .shared_set = Some(u32(2)) };
    auto                 shared = PlanPipelineLayouts(requirements.as_slice(), policy, false);
    rstd_assert(shared.is_ok());
    rstd_assert(shared->global_bindings.len() == usize(1));
    rstd_assert(shared->families.len() == usize(1));
    const auto& sets = shared->families[usize()].request.descriptor_sets;
    rstd_assert(sets.len() == usize(3));
    rstd_assert(sets[usize()].bindings.is_empty());
    rstd_assert(sets[usize(2)].bindings.len() == usize(1));
    rstd_assert(PlanPipelineLayouts(requirements.as_slice(), {}, false).is_err());
    requirements[usize(1)].descriptor_sets[usize()].bindings[usize()].shared_identity =
        Some(u64(43));
    rstd_assert(PlanPipelineLayouts(requirements.as_slice(), policy, false).is_err());

    requirements.clear();
    requirements.push(Requirement(u64(1), u32(), false));
    requirements[usize()].descriptor_sets[usize()].push_descriptor = true;
    auto push = PlanPipelineLayouts(requirements.as_slice(), {}, true, u32(32));
    rstd_assert(push.is_ok());
    rstd_assert(push->families[usize()].request.descriptor_sets[usize()].push_descriptor);
    auto fallback = PlanPipelineLayouts(requirements.as_slice(), {}, false);
    rstd_assert(fallback.is_ok());
    rstd_assert(! fallback->families[usize()].request.descriptor_sets[usize()].push_descriptor);
}
