#include <rstd/macro.hpp>

import rstd;
import vrento.runtime;
import vrento.uniform_source;
import vrento.uniform_buffer;
import vrento.uniform_binding;
import vrento.resource;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace vrento;

struct NodeId {
    u32  index {};
    u32  generation { 1 };
    bool Valid() const { return generation != u32(); }
};

struct State {
    Vec<u32> order;
    u32      destroyed {};
    u32      evaluated {};
    bool     fail_evaluate { false };
    u32      leases_created {};
    u32      leases_destroyed {};
};

struct Lease {
    explicit Lease(rstd::sync::Arc<State> state): state(rstd::move(state)) {
        ++this->state->leases_created;
    }
    Lease(const Lease&) = delete;
    Lease(Lease&& other): state(rstd::move(other.state)), active(other.active) {
        other.active = false;
    }
    ~Lease() {
        if (active) ++state->leases_destroyed;
    }
    void                   KeepAlive() const {}
    rstd::sync::Arc<State> state;
    bool                   active { true };
};

struct System {
    rstd::sync::Arc<State> state;
    u32                    index;
    void                   Update(ref<FrameContext> frame) {
        rstd_assert(frame->index > u64());
        state->order.push(u32(index));
    }
};

struct Source {
    explicit Source(rstd::sync::Arc<State> state): state(rstd::move(state)) {}
    Source(const Source&) = delete;
    Source(Source&& other): state(rstd::move(other.state)), active(other.active) {
        other.active = false;
    }
    ~Source() {
        if (active) ++state->destroyed;
    }
    auto Describe(mut_ref<dyn<UniformBindingSink>> sink) const -> Result<empty, UniformError> {
        auto bound =
            sink->Bind(UniformOutputId { u32(0) }, "value"_str, UniformValueShape::Float(u32(1)));
        if (bound.is_err()) return Err(rstd::move(bound).unwrap_err_unchecked());
        return Ok(empty {});
    }
    auto Version(ref<dyn<UniformUpdateContext>> context) const -> u64 {
        return context->Frame()->revision;
    }
    auto Evaluate(ref<dyn<UniformUpdateContext>>, mut_ref<dyn<UniformValueSink>> sink) const
        -> Result<empty, UniformError> {
        ++state->evaluated;
        if (state->fail_evaluate)
            return Err(UniformError { String::make("evaluation failed"_str) });
        float value = 7.0f;
        return sink->Write(
            UniformOutputId { u32(0) },
            UniformValueView { &value, usize(1), UniformValueLayout::Linear(usize(1)) });
    }
    auto AcquireBindingLease() const -> Option<Box<dyn<UniformBindingLease>>> {
        return Some(Box<dyn<UniformBindingLease>>::make(Lease(state.clone())));
    }
    rstd::sync::Arc<State> state;
    bool                   active { true };
};

namespace rstd
{
template<>
struct Impl<RuntimeSystem, System> : ImplBase<System> {
    void Update(ref<FrameContext> frame) { this->self().Update(frame); }
};
template<>
struct Impl<UniformSource, Source> : ImplBase<Source> {
    auto Describe(mut_ref<dyn<UniformBindingSink>> sink) const -> Result<empty, UniformError> {
        return this->self().Describe(sink);
    }
    auto Version(ref<dyn<UniformUpdateContext>> context) const -> u64 {
        return this->self().Version(context);
    }
    auto Evaluate(ref<dyn<UniformUpdateContext>> context, mut_ref<dyn<UniformValueSink>> sink) const
        -> Result<empty, UniformError> {
        return this->self().Evaluate(context, sink);
    }
    auto AcquireBindingLease() const -> Option<Box<dyn<UniformBindingLease>>> {
        return this->self().AcquireBindingLease();
    }
};
} // namespace rstd

static void RuntimeOrder() {
    auto    state = rstd::sync::Arc<State>::make();
    Runtime runtime;
    runtime.RegisterSystem(System { state.clone(), u32(1) });
    runtime.RegisterSystem(System { state.clone(), u32(2) });
    runtime.RegisterSystem(System { state.clone(), u32(3) }, RuntimeSchedule::BeforeRender);
    runtime.Advance(f64(0.25));
    rstd_assert(state->order.len() == usize(2));
    runtime.BeforeRender();
    rstd_assert(state->order.len() == usize(3));
    for (usize index {}; index < state->order.len(); ++index)
        rstd_assert(state->order[index] == rstd::as_cast<u32>(index) + u32(1));
    rstd_assert(runtime.Frame().elapsed == f64(0.25));
    rstd_assert(runtime.Frame().index == u64(1));
}

static void SourceOwnership() {
    auto                       state = rstd::sync::Arc<State>::make();
    Option<UniformSourceOwner> retained;
    {
        UniformRegistry<NodeId> registry;
        auto source = registry.Register(Box<dyn<UniformSource>>::make(Source(state.clone())));
        rstd_assert(registry.AttachGlobal(source, i32(4)));
        rstd_assert(registry.AttachNode(NodeId {}, source, i32(2)));
        rstd_assert(registry.AttachGlobal(source, i32(7)));
        rstd_assert(registry.GlobalSources().len() == usize(1));
        rstd_assert(registry.GlobalSources()[usize()].priority == i32(7));
        auto ranked = RankUniformSources(registry.GlobalSources(), registry.NodeSources(NodeId {}));
        rstd_assert(ranked.len() == usize(1));
        rstd_assert(ranked[usize()].priority == i32(2));
        retained = registry.Retain(source);
        rstd_assert(retained.is_some());
        registry.Reset();
        rstd_assert(registry.Resolve(source).is_none());
        rstd_assert(registry.Retain(source).is_none());
        rstd_assert(registry.GlobalSources().is_empty());
        rstd_assert(registry.NodeSources(NodeId {}).is_empty());
        rstd_assert(state->destroyed == u32());
    }
    rstd_assert(state->destroyed == u32());
    retained = None();
    rstd_assert(state->destroyed == u32(1));
}

static void ValuesAndLayout() {
    rstd::array<float, 20> values {};
    for (usize index {}; index < values.len(); ++index) values[index] = float(index.to_primitive());
    UniformValue first(values);
    UniformValue copy(first);
    Vec<float>   source;
    source.push(2.0f);
    source.push(3.0f);
    UniformValue from_slice(source.as_slice());
    source.clear();
    rstd_assert(from_slice.size() == usize(2));
    rstd_assert(from_slice[usize(1)] == 3.0f);
    first[usize(17)] = -1.0f;
    rstd_assert(copy[usize(17)] == 17.0f);

    resource::ShaderArtifactUniformBlock block;
    block.size = usize(32);
    block.members.push(resource::ShaderArtifactUniformMember {
        .name           = String::make("matrix"_str),
        .offset         = u32(),
        .size           = usize(32),
        .count          = usize(1),
        .scalar_kind    = ShaderScalarKind::Float,
        .scalar_width   = u32(32),
        .matrix_rows    = u32(2),
        .matrix_columns = u32(2),
        .matrix_stride  = u32(16),
        .matrix_major   = ShaderMatrixMajor::Column,
    });
    auto result = CompileUniformBufferLayout(block);
    rstd_assert(result.is_ok());
    auto layout = rstd::move(result).unwrap_unchecked();
    auto matrix = UniformValue::fromMatrixArray(
        values.data(), u32(2), u32(2), usize(1), UniformMatrixStorage::ColumnMajor);
    auto bytes = Vec<u8>::with_capacity(usize(32));
    bytes.resize(usize(32), u8(255));
    rstd_assert(SerializeUniformValue(bytes.as_mut_slice().as_mut_ref(),
                                      layout.slots[usize()],
                                      matrix.View(),
                                      ShaderMatrixConvention::ColumnVector)
                    .is_ok());
    for (usize index { 8 }; index < usize(16); ++index) rstd_assert(bytes[index] == u8());
    for (usize index { 24 }; index < usize(32); ++index) rstd_assert(bytes[index] == u8());
    layout.slots[usize()].offset = usize(1);
    rstd_assert(SerializeUniformValue(bytes.as_mut_slice().as_mut_ref(),
                                      layout.slots[usize()],
                                      matrix.View(),
                                      ShaderMatrixConvention::ColumnVector)
                    .is_err());
    block.members[usize()].offset = u32(1);
    rstd_assert(CompileUniformBufferLayout(block).is_err());
}

struct Resources {
    auto Texture(usize) const -> Option<UniformTextureView> { return None(); }
    auto Viewport() const -> rstd::array<float, 2> { return { 64.0f, 64.0f }; }
    auto TexelSize() const -> rstd::array<float, 2> { return { 1.0f / 64, 1.0f / 64 }; }
};

struct Context {
    ref<FrameContext>             frame;
    ref<dyn<UniformResourceView>> resources;
    auto                          Frame() const -> ref<FrameContext> { return frame; }
    auto Resources() const -> ref<dyn<UniformResourceView>> { return resources; }
    auto RenderView() const -> RenderViewKind { return RenderViewKind::Primary; }
};

struct Writer {
    bool    fail { false };
    u32     calls {};
    Vec<u8> bytes;
    auto    UpdateBuffer(resource::BufferUseHandle, slice<u8> content)
        -> Result<empty, resource::ResourceError> {
        ++calls;
        if (fail)
            return Err(resource::ResourceError {
                .kind    = resource::ResourceErrorKind::BackendFailure,
                .message = String::make("upload failed"_str),
            });
        bytes = Vec<u8>::from(content);
        return Ok(empty {});
    }
};

static void BindingRetriesAndOwnsSources() {
    auto                state = rstd::sync::Arc<State>::make();
    UniformBufferLayout layout { .size = usize(4) };
    layout.slots.push(UniformSlot { .name         = String::make("value"_str),
                                    .size         = usize(4),
                                    .scalar_kind  = ShaderScalarKind::Float,
                                    .scalar_width = u32(32) });
    auto sources = Vec<BoundUniformSource>::make();
    {
        UniformRegistry<NodeId> registry;
        auto id       = registry.Register(Box<dyn<UniformSource>>::make(Source(state.clone())));
        auto source   = registry.Retain(id).unwrap();
        auto prepared = PrepareUniformSource(layout,
                                             rstd::move(source),
                                             i32(),
                                             ShaderMatrixConvention::ColumnVector,
                                             ShaderMatrixAbi::NativeSpirv);
        rstd_assert(prepared.is_ok());
        auto bound = rstd::move(prepared).unwrap_unchecked();
        rstd_assert(bound.is_some());
        sources.push(rstd::move(bound).unwrap_unchecked());
    }
    rstd_assert(state->destroyed == u32());
    rstd_assert(state->leases_created == u32(1));
    rstd_assert(state->leases_destroyed == u32());
    {
        UniformBinding binding({},
                               rstd::move(layout),
                               rstd::move(sources),
                               ShaderMatrixConvention::ColumnVector,
                               ShaderMatrixAbi::NativeSpirv);
        FrameContext   frame;
        Resources      resources;
        auto           resource_view = dyn<UniformResourceView>::from_ref(resources);
        Context context { ref<FrameContext>::from_raw_parts(&frame), resource_view.as_ref() };
        auto    view = dyn<UniformUpdateContext>::from_ref(context);
        Writer  writer;
        auto    sink = dyn<resource::BufferContentWriter>::from_ref(writer);
        rstd_assert(binding.Update(view.as_ref(), sink.as_mut_ref()).is_ok());
        rstd_assert(writer.calls == u32(1));
        rstd_assert(binding.Update(view.as_ref(), sink.as_mut_ref()).is_ok());
        rstd_assert(writer.calls == u32(1));
        ++frame.revision;
        writer.fail = true;
        rstd_assert(binding.Update(view.as_ref(), sink.as_mut_ref()).is_err());
        rstd_assert(writer.calls == u32(2));
        writer.fail = false;
        rstd_assert(binding.Update(view.as_ref(), sink.as_mut_ref()).is_ok());
        rstd_assert(writer.calls == u32(3));
        ++frame.revision;
        state->fail_evaluate = true;
        rstd_assert(binding.Update(view.as_ref(), sink.as_mut_ref()).is_err());
        rstd_assert(writer.calls == u32(3));
        state->fail_evaluate = false;
        rstd_assert(binding.Update(view.as_ref(), sink.as_mut_ref()).is_ok());
        rstd_assert(writer.calls == u32(4));
    }
    rstd_assert(state->destroyed == u32(1));
    rstd_assert(state->leases_destroyed == u32(1));
}

int main() {
    RuntimeOrder();
    SourceOwnership();
    ValuesAndLayout();
    BindingRetriesAndOwnsSources();
}
