module;

#include <rstd/macro.hpp>

module vrento.rgraph;
import rstd;

using namespace rstd::prelude;
using rstd::collections::HashMap;
using namespace rstd::literals;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;
using namespace vrento::rg;
namespace resource = vrento::resource;

namespace
{
u64 NextRenderGraphResourceGeneration() {
    static Atomic<u64> next { u64(1) };
    auto               generation = next.fetch_add(u64(1), Ordering::Relaxed);
    if (generation == u64()) generation = next.fetch_add(u64(1), Ordering::Relaxed);
    return generation;
}

auto ToTexType(TextureKind kind) -> TexNode::TexType {
    return kind == TextureKind::Temp ? TexNode::TexType::Temp : TexNode::TexType::Imported;
}

auto ToTextureKind(TexNode::TexType type) -> TextureKind {
    switch (type) {
    case TexNode::TexType::Imported: return TextureKind::Imported;
    case TexNode::TexType::Temp: return TextureKind::Temp;
    }
    return TextureKind::Imported;
}

auto ToTextureDesc(const TexNode& node) -> TextureDesc {
    return TextureDesc {
        .name = node.name.clone(),
        .key  = node.key.clone(),
        .kind = ToTextureKind(node.type),
        .request =
            node.request.is_some() ? Some(node.request->clone()) : None<resource::TextureRequest>(),
        .allocation_family = node.allocation_family.is_some()
                                 ? Some(node.allocation_family->clone())
                                 : None<String>(),
    };
}

template<typename T>
    requires rstd::Impled<T, Node>
auto NodeGraphviz(const T& node) -> String {
    return rstd::as<Node>(node).ToGraphviz();
}

auto Sorted(slice<NodeHandle> handles) -> Vec<NodeHandle> {
    auto sorted = Vec<NodeHandle>::with_capacity(handles.len());
    sorted.extend_from_slice(handles);
    rstd::slice_::sort_unstable(sorted.as_mut_slice().as_mut_ref());
    return sorted;
}
} // namespace

RenderGraph::RenderGraph(): m_resource_generation(NextRenderGraphResourceGeneration()) {}

auto RenderGraph::getPassNode(NodeHandle handle) -> Option<PassNode&> {
    auto node = m_pass_nodes.get_mut(handle);
    if (node.is_none()) return None();
    return Some<PassNode&>(**node);
}

auto RenderGraph::getPassNode(NodeHandle handle) const -> Option<const PassNode&> {
    auto node = m_pass_nodes.get(handle);
    if (node.is_none()) return None();
    return Some<const PassNode&>(**node);
}

auto RenderGraph::getTexNode(NodeHandle handle) -> Option<TexNode&> {
    auto node = m_tex_nodes.get_mut(handle);
    if (node.is_none()) return None();
    return Some<TexNode&>(**node);
}

auto RenderGraph::getTexNode(NodeHandle handle) const -> Option<const TexNode&> {
    auto node = m_tex_nodes.get(handle);
    if (node.is_none()) return None();
    return Some<const TexNode&>(**node);
}

auto RenderGraph::getPass(PassHandle handle) -> Option<Pass&> {
    auto pass = m_passes.get_mut(handle);
    if (pass.is_none()) return None();
    return Some<Pass&>((**pass)->AsPass());
}

auto RenderGraph::getPass(PassHandle handle) const -> Option<const Pass&> {
    auto pass = m_passes.get(handle);
    if (pass.is_none()) return None();
    return Some<const Pass&>((**pass)->AsConstPass());
}

auto RenderGraph::passState(NodeHandle handle) const -> Option<PassNodeState> {
    auto node = getPassNode(handle);
    if (node.is_none()) return None();
    return Some(PassNodeState {
        .handle = handle,
        .pass   = node->pass,
        .name   = node->name.clone(),
        .type   = node->type,
    });
}

void RenderGraph::ToGraphviz(ref<str> path) const {
    auto output         = "digraph framegraph {\nnode [shape=box]\n"_Str;
    using AllocationMap = HashMap<NodeHandle, String>;
    auto allocations    = AllocationMap::make();
    for (const auto& entry : resourcePlan().textures) {
        (void)allocations.insert(NodeHandle { .index = usize(entry.handle.index.to_primitive()) },
                                 entry.allocation_key.clone());
    }

    for (usize index {}; index < m_dg.NodeNum(); ++index) {
        NodeHandle handle { .index = index };
        if (auto pass = getPassNode(handle); pass.is_some()) {
            auto declaration = NodeGraphviz(*pass);
            output.push_str(declaration.as_str());
            output.push_ascii(u8('\n'));
        } else if (auto texture = getTexNode(handle); texture.is_some()) {
            auto allocation = allocations.get(handle);
            auto declaration =
                allocation.is_some()
                    ? texture->ToGraphvizWithAllocation(Some((**allocation).as_str()))
                    : NodeGraphviz(*texture);
            output.push_str(declaration.as_str());
            output.push_ascii(u8('\n'));
        }
    }

    for (usize index {}; index < m_dg.NodeNum(); ++index) {
        NodeHandle from { .index = index };
        for (auto to : Sorted(m_dg.GetNodeOut(from))) {
            ref<str> access       = "order"_str;
            auto     texture_from = getTexNode(from);
            auto     pass_to      = getPassNode(to);
            if (texture_from && pass_to) {
                if (texture_from->next) {
                    auto next = getTexNode(*texture_from->next);
                    if (next && next->writer && *next->writer == to) access = "read/version"_str;
                }
                if (access == "order"_str) access = "read"_str;
            } else if (auto pass_from = getPassNode(from); pass_from) {
                auto texture_to = getTexNode(to);
                if (texture_to && texture_to->writer && *texture_to->writer == from) {
                    access = "write"_str;
                }
            }
            auto edge =
                rstd::format("n{}->n{}[label=\"access={}\"]\n", from.index, to.index, access);
            output.push_str(edge.as_str());
        }
    }

    output.push_ascii(u8('}'));
    (void)rstd::fs::write(ref<rstd::path::Path>(path),
                          slice<u8>::from_raw_parts(output.as_raw_ptr(), output.len()));
}

auto RenderGraph::textureState(TextureNodeRef ref) const -> Option<TextureNodeState> {
    auto node = getTexNode(ref.handle);
    if (node.is_none()) return None();
    return Some(TextureNodeState {
        .ref     = ref,
        .use     = resource::TextureUseHandle { .index      = rstd::as_cast<u64>(ref.handle.index),
                                                .generation = m_resource_generation },
        .desc    = ToTextureDesc(*node),
        .version = node->version,
    });
}

auto RenderGraph::latestTexture(ref<str> key) const -> Option<TextureNodeRef> {
    auto handle = m_key_texnode.get(key);
    if (handle.is_none()) return None();
    return Some(TextureNodeRef { .handle = **handle });
}

auto RenderGraph::readTexture(NodeHandle pass_node, TextureNodeRef texture) -> bool {
    if (getPassNode(pass_node).is_none() || textureState(texture).is_none()) return false;

    RenderGraphBuilder builder(*this, pass_node);
    builder.read(texture);
    return true;
}

auto RenderGraph::topologicalOrder() const -> Result<Vec<NodeHandle>, RenderGraphOrderError> {
    auto in_degree = Vec<usize>::make();
    in_degree.resize(m_dg.NodeNum(), usize());
    for (usize index {}; index < m_dg.NodeNum(); ++index) {
        NodeHandle handle { .index = index };
        in_degree[index] = m_dg.GetNodeIn(handle).len();
    }

    auto ready = Vec<NodeHandle>::with_capacity(m_dg.NodeNum());
    for (usize index {}; index < m_dg.NodeNum(); ++index) {
        if (in_degree[index] == usize()) ready.push(NodeHandle { .index = index });
    }

    auto           pass_nodes = Vec<NodeHandle>::make();
    Option<String> active_target;
    usize          visited {};

    auto choose_ready = [&]() -> usize {
        rstd::slice_::sort_unstable(ready.as_mut_slice().as_mut_ref());

        for (usize index {}; index < ready.len(); ++index) {
            if (! isRenderPassNode(ready[index])) return index;
        }

        if (active_target) {
            for (usize index {}; index < ready.len(); ++index) {
                auto target = passWriteTarget(ready[index]);
                if (target && *target == *active_target) return index;
            }
        }

        using CountMap     = HashMap<String, usize>;
        auto target_counts = CountMap::make();
        for (auto handle : ready) {
            auto target = passWriteTarget(handle);
            if (! target) continue;
            auto count = target_counts.get_mut(*target);
            if (count)
                ++**count;
            else
                (void)target_counts.insert(target->clone(), usize(1));
        }

        usize best_index {};
        usize best_count {};
        for (usize index {}; index < ready.len(); ++index) {
            usize count {};
            auto  target = passWriteTarget(ready[index]);
            if (target) {
                auto stored = target_counts.get(*target);
                if (stored) count = **stored;
            }
            if (count > best_count) {
                best_index = index;
                best_count = count;
            }
        }
        return best_index;
    };

    while (! ready.is_empty()) {
        auto handle = ready.remove(choose_ready());
        ++visited;

        if (isRenderPassNode(handle)) {
            pass_nodes.push(NodeHandle { handle });
            active_target = passWriteTarget(handle);
        }

        for (auto next : Sorted(m_dg.GetNodeOut(handle))) {
            rstd_assert(in_degree[next.index] > usize());
            --in_degree[next.index];
            if (in_degree[next.index] == usize()) ready.push(NodeHandle { next });
        }
    }

    if (visited != m_dg.NodeNum()) return Err(RenderGraphOrderError::Cycle);
    return Ok(rstd::move(pass_nodes));
}

auto RenderGraph::isPassNode(NodeHandle handle) const -> bool {
    return m_pass_nodes.contains_key(handle);
}

auto RenderGraph::isVirtualPassNode(NodeHandle handle) const -> bool {
    auto node = getPassNode(handle);
    return node && node->type == PassNode::Type::Virtual;
}

auto RenderGraph::isRenderPassNode(NodeHandle handle) const -> bool {
    return isPassNode(handle) && ! isVirtualPassNode(handle);
}

auto RenderGraph::passWriteTarget(NodeHandle handle) const -> Option<String> {
    if (! isPassNode(handle)) return None();

    for (auto out : Sorted(m_dg.GetNodeOut(handle))) {
        auto texture = getTexNode(out);
        if (texture && texture->writer && *texture->writer == handle) {
            return Some(texture->key.clone());
        }
    }
    return None();
}

RenderGraphBuilder::RenderGraphBuilder(RenderGraph& graph, NodeHandle pass_node)
    : m_rg(graph), m_passnode_wip(pass_node) {}

void RenderGraphBuilder::markSelfWrite(TextureNodeRef ref) {
    auto state = m_rg.textureState(ref);
    rstd_assert(state.is_some());
    if (! state || state->version > usize()) return;
    m_rg.addPass<VirtualPass>(
        "virtual pass"_str, PassNode::Type::Virtual, [ref](RenderGraphBuilder& builder, auto&) {
            builder.write(ref);
        });
}

void RenderGraphBuilder::markVirtualWrite(TextureNodeRef ref) {
    auto state = m_rg.textureState(ref);
    rstd_assert(state.is_some());
    if (! state || state->version > usize() || m_rg.textureHasWriter(ref)) return;
    m_rg.addPass<VirtualPass>(
        "virtual pass"_str, PassNode::Type::Virtual, [ref](RenderGraphBuilder& builder, auto&) {
            builder.write(ref);
        });
}

void RenderGraphBuilder::reusePreviousAllocation(TextureNodeRef ref) {
    m_rg.reusePreviousAllocation(ref);
}

auto RenderGraphBuilder::createTexture(const TextureDesc& desc, bool write) -> TextureNodeRef {
    return createTextureNode(desc, write);
}

void RenderGraphBuilder::read(TextureNodeRef ref) {
    auto state = m_rg.textureState(ref);
    rstd_assert(state.is_some());
    if (state) readTextureNode(ref);
}

void RenderGraphBuilder::write(TextureNodeRef ref) {
    auto state = m_rg.textureState(ref);
    rstd_assert(state.is_some());
    if (state) writeTextureNode(ref);
}

auto RenderGraphBuilder::textureState(TextureNodeRef ref) const -> Option<TextureNodeState> {
    return m_rg.textureState(ref);
}

auto RenderGraphBuilder::createTextureNode(const TextureDesc& desc, bool write) -> TextureNodeRef {
    return m_rg.createTextureNode(desc, write);
}

void RenderGraphBuilder::readTextureNode(TextureNodeRef ref) {
    m_rg.connectTextureRead(ref, m_passnode_wip);
}

void RenderGraphBuilder::writeTextureNode(TextureNodeRef ref) {
    m_rg.connectTextureWrite(ref, m_passnode_wip);
}

auto RenderGraph::createTextureNode(const TextureDesc& desc, bool write) -> TextureNodeRef {
    TextureNodeRef ref;
    auto           current = m_key_texnode.get(desc.key);
    if (current) {
        TextureNodeRef old { .handle = **current };
        ref = write && textureHasWriter(old) ? createNewTextureNode(desc) : old;
    } else {
        ref = createNewTextureNode(desc);
    }
    rstd_assert(ref.valid());
    return ref;
}

auto RenderGraph::createNewTextureNode(const TextureDesc& desc) -> TextureNodeRef {
    auto handle  = m_dg.AddNode();
    auto current = m_key_texnode.get(desc.key);
    auto request =
        desc.request.is_some() ? Some(desc.request->clone()) : None<resource::TextureRequest>();
    if (request.is_some()) request->name = desc.key.clone();

    TexNode node {
        .handle            = handle,
        .type              = ToTexType(desc.kind),
        .key               = desc.key.clone(),
        .name              = desc.name.clone(),
        .request           = rstd::move(request),
        .allocation_family = desc.allocation_family.is_some()
                                 ? Some(desc.allocation_family->clone())
                                 : None<String>(),
    };
    if (current) {
        auto previous = getTexNode(**current);
        rstd_assert(previous.is_some());
        if (previous) {
            node.version   = previous->version + usize(1);
            node.previous  = Some(previous->handle);
            previous->next = Some(handle);
        }
    }

    (void)m_tex_nodes.insert(handle, rstd::move(node));
    (void)m_key_texnode.insert(desc.key.clone(), handle);
    return TextureNodeRef { .handle = handle };
}

void RenderGraph::reusePreviousAllocation(TextureNodeRef ref) {
    auto node = getTexNode(ref.handle);
    rstd_assert(node.is_some());
    if (node.is_none() || node->previous.is_none()) return;
    node->allocation_parent = node->previous;
}

void RenderGraph::connectTextureRead(TextureNodeRef ref, NodeHandle pass_node) {
    auto texture = getTexNode(ref.handle);
    if (! texture || getPassNode(pass_node).is_none()) return;
    (void)m_dg.Connect(ref.handle, pass_node);
    bool registered = false;
    for (auto reader : texture->readers) {
        if (reader != pass_node) continue;
        registered = true;
        break;
    }
    if (! registered) texture->readers.push(NodeHandle { .index = pass_node.index });

    if (texture->allocation_family.is_none() && texture->next) {
        auto next = getTexNode(*texture->next);
        if (next && next->writer) (void)m_dg.Connect(pass_node, *next->writer);
    }
}

void RenderGraph::connectTextureWrite(TextureNodeRef ref, NodeHandle pass_node) {
    auto node = getTexNode(ref.handle);
    if (! node || getPassNode(pass_node).is_none()) return;

    if (node->previous) {
        auto previous = *node->previous;
        auto readers  = m_dg.GetNodeOut(previous);
        for (usize index {}; index < readers.len(); ++index) {
            auto reader = readers[index];
            if (reader != pass_node && isPassNode(reader)) (void)m_dg.Connect(reader, pass_node);
        }
        if (readers.len() == usize()) (void)m_dg.Connect(previous, pass_node);
    }
    (void)m_dg.Connect(pass_node, ref.handle);
    node->writer = Some(pass_node);
}

auto RenderGraph::textureHasWriter(TextureNodeRef ref) const -> bool {
    auto node = getTexNode(ref.handle);
    return node && node->writer.is_some();
}

auto RenderGraphBuilder::workPassNode() const -> const PassNode& {
    auto node = m_rg.getPassNode(m_passnode_wip);
    rstd_assert(node.is_some());
    return *node;
}

auto RenderGraph::getLastReadTextures(slice<NodeHandle> nodes) const -> Vec<Vec<TextureNodeState>> {
    auto result = Vec<Vec<TextureNodeState>>::with_capacity(nodes.len());
    for (usize index {}; index < nodes.len(); ++index) {
        result.push(Vec<TextureNodeState>::make());
    }

    using SeenMap = HashMap<NodeHandle, bool>;
    auto seen     = SeenMap::make();
    for (usize offset = nodes.len(); offset > usize(); --offset) {
        auto index  = offset - usize(1);
        auto inputs = m_dg.GetNodeIn(nodes[index]);
        for (usize input_index {}; input_index < inputs.len(); ++input_index) {
            auto input = inputs[input_index];
            if (seen.contains_key(input)) continue;
            (void)seen.insert(input, true);
            auto state = textureState(TextureNodeRef { .handle = input });
            if (state) result[index].push(rstd::move(*state));
        }
    }
    return result;
}

auto RenderGraph::resourcePlan() const -> resource::ResourcePlan {
    using BoundaryMap     = HashMap<String, bool>;
    auto frame_boundaries = BoundaryMap::make();
    for (usize index {}; index < m_dg.NodeNum(); ++index) {
        auto node = getTexNode(NodeHandle { .index = index });
        if (node.is_none() || node->version != usize() || node->writer.is_none() ||
            ! isVirtualPassNode(*node->writer)) {
            continue;
        }

        bool has_real_reader = false;
        auto targets         = m_dg.GetNodeOut(node->handle);
        for (usize target_index {}; target_index < targets.len(); ++target_index) {
            if (isRenderPassNode(targets[target_index])) {
                has_real_reader = true;
                break;
            }
        }
        if (! has_real_reader) continue;

        auto next = node->next;
        while (next.is_some()) {
            auto version = getTexNode(*next);
            if (version.is_none()) break;
            if (version->writer.is_some() && isRenderPassNode(*version->writer)) {
                (void)frame_boundaries.insert(node->key.clone(), true);
                break;
            }
            next = version->next;
        }
    }

    resource::ResourcePlan plan { .generation = m_resource_generation };
    auto                   plan_nodes = Vec<NodeHandle>::make();
    for (usize index {}; index < m_dg.NodeNum(); ++index) {
        auto node = getTexNode(NodeHandle { .index = index });
        if (node.is_none() || node->request.is_none()) continue;

        bool read    = false;
        auto targets = m_dg.GetNodeOut(node->handle);
        for (usize target_index {}; target_index < targets.len(); ++target_index) {
            if (isPassNode(targets[target_index])) {
                read = true;
                break;
            }
        }
        auto access  = node->writer.is_some() ? (read ? resource::ResourceAccess::ReadWrite
                                                      : resource::ResourceAccess::Write)
                                              : resource::ResourceAccess::Read;
        auto request = node->request->clone();
        if (frame_boundaries.contains_key(node->key)) {
            request.lifetime = resource::TextureLifetimeClass::Retained;
            request.content |=
                resource::TextureContentFlag(resource::TextureContent::PreserveAcrossFrames);
        }
        auto allocation_key = request.name.clone();
        plan.textures.push(resource::TexturePlanEntry {
            .handle  = resource::TextureUseHandle { .index = rstd::as_cast<u64>(node->handle.index),
                                                    .generation = plan.generation },
            .request = rstd::move(request),
            .allocation_key = rstd::move(allocation_key),
            .access         = access,
            .version        = rstd::as_cast<u32>(node->version),
        });
        plan_nodes.push(NodeHandle { .index = node->handle.index });
    }

    auto ordered = topologicalOrder();
    if (ordered.is_err()) return plan;

    using PositionMap = HashMap<NodeHandle, usize>;
    auto positions    = PositionMap::make();
    auto pass_order   = rstd::move(ordered).unwrap_unchecked();
    for (usize index {}; index < pass_order.len(); ++index) {
        (void)positions.insert(pass_order[index], index);
    }

    auto parents = Vec<usize>::with_capacity(m_dg.NodeNum());
    for (usize index {}; index < m_dg.NodeNum(); ++index) {
        parents.push(usize(index.to_primitive()));
    }
    auto root_of = [&](usize index) {
        while (parents[index] != index) index = parents[index];
        return index;
    };
    for (usize entry_index {}; entry_index < plan_nodes.len(); ++entry_index) {
        auto node = getTexNode(plan_nodes[entry_index]);
        if (node.is_none() || node->allocation_family.is_none() ||
            node->allocation_parent.is_none() ||
            plan.textures[entry_index].request.lifetime !=
                resource::TextureLifetimeClass::FrameLocal) {
            continue;
        }
        auto parent = getTexNode(*node->allocation_parent);
        if (parent.is_none() || parent->allocation_family.is_none() ||
            *parent->allocation_family != *node->allocation_family) {
            continue;
        }
        auto child_root     = root_of(node->handle.index);
        auto parent_root    = root_of(parent->handle.index);
        parents[child_root] = parent_root;
    }

    struct AllocationGroup {
        String     family;
        usize      first { usize::MAX };
        usize      last {};
        Vec<usize> entries;
    };
    auto groups = Vec<Option<AllocationGroup>>::with_capacity(m_dg.NodeNum());
    for (usize index {}; index < m_dg.NodeNum(); ++index) {
        groups.push(None<AllocationGroup>());
    }

    for (usize entry_index {}; entry_index < plan_nodes.len(); ++entry_index) {
        auto node = getTexNode(plan_nodes[entry_index]);
        if (node.is_none() || node->allocation_family.is_none() ||
            plan.textures[entry_index].request.lifetime !=
                resource::TextureLifetimeClass::FrameLocal) {
            continue;
        }

        usize first { usize::MAX };
        usize last {};
        if (node->writer.is_some()) {
            auto position = positions.get(*node->writer);
            if (position.is_some()) first = last = **position;
        }
        for (auto reader : node->readers) {
            auto position = positions.get(reader);
            if (position.is_none()) continue;
            if (first == usize::MAX || **position < first) first = **position;
            if (**position > last) last = **position;
        }
        if (first == usize::MAX) first = last = usize();

        auto root = root_of(node->handle.index);
        if (groups[root].is_none()) {
            groups[root] = Some(AllocationGroup {
                .family  = node->allocation_family->clone(),
                .entries = Vec<usize>::make(),
            });
        }
        auto& group = *groups[root];
        if (first < group.first) group.first = first;
        if (last > group.last) group.last = last;
        group.entries.push(usize(entry_index.to_primitive()));
    }

    auto roots = Vec<usize>::make();
    for (usize index {}; index < groups.len(); ++index) {
        if (groups[index].is_some()) roots.push(usize(index.to_primitive()));
    }
    rstd::slice_::sort_unstable_by(roots.as_mut_slice().as_mut_ref(), [&](usize lhs, usize rhs) {
        const auto& left  = *groups[lhs];
        const auto& right = *groups[rhs];
        if (left.family != right.family) return left.family < right.family.as_str();
        if (left.first != right.first) return left.first < right.first;
        return lhs < rhs;
    });

    using SlotMap = HashMap<String, Vec<usize>>;
    auto slots    = SlotMap::make();
    for (auto root : roots) {
        auto& group      = *groups[root];
        auto  family_key = group.family.clone();
        auto  family     = slots.get_mut(group.family.as_str());
        if (family.is_none()) {
            (void)slots.insert(rstd::move(family_key), Vec<usize>::make());
            family = slots.get_mut(group.family.as_str());
        }
        rstd_assert(family.is_some());
        if (family.is_none()) continue;

        usize slot = (*family)->len();
        for (usize index {}; index < (*family)->len(); ++index) {
            if ((**family)[index] < group.first) {
                slot = index;
                break;
            }
        }
        if (slot == (*family)->len()) {
            (*family)->push(usize(group.last.to_primitive()));
        } else {
            (**family)[slot] = group.last;
        }

        auto allocation_key = rstd::format("{}::slot_{}", group.family.as_str(), slot);
        for (auto entry_index : group.entries) {
            plan.textures[entry_index].allocation_key = allocation_key.clone();
        }
    }
    return plan;
}
