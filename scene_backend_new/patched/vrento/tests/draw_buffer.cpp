#include <rstd/macro.hpp>
import rstd;
import rstd.cppstd;
import vrento.draw_buffer;
import vrento.resource_registry;
import vrento.vulkan;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::sync::Arc;
using namespace vrento;

struct Writer {
    usize writes {};
    usize fail_on {};
    auto  UpdateBuffer(resource::BufferUseHandle, slice<u8>)
        -> Result<empty, resource::ResourceError> {
        ++writes;
        if (writes == fail_on) return Err(resource::ResourceError { .message = "failed"_Str });
        return Ok(empty {});
    }
};

int main() {
    VertexArray first({ { "position"_Str, VertexType::FLOAT3, false } }, usize(3));
    VertexArray second({ { "color"_Str, VertexType::FLOAT4, false } }, usize(2));
    (void)first.RewriteVertices([](VertexWriter& writer) {
        for (int i = 0; i < 3; ++i) rstd_assert(writer.AppendZeroedVertex().is_some());
    });
    (void)second.RewriteVertices([](VertexWriter& writer) {
        for (int i = 0; i < 2; ++i) rstd_assert(writer.AppendZeroedVertex().is_some());
    });
    IndexArray     indices(usize(3));
    const uint32_t input[] { 0, 1, 0 };
    indices.Assign(usize(), slice<uint32_t>::from_raw_parts(input, usize(3)));
    auto view = [&] {
        GeometryView geometry { .dynamic = true };
        geometry.vertices.push(first.BufferView());
        geometry.vertices.push(second.BufferView());
        return geometry;
    };
    resource_registry::PreparedResourceTable table;
    Vec<resource::BufferUseHandle>           uses;
    for (u64 i(1); i < u64(4); ++i) {
        auto use      = resource::BufferUseHandle { .index = i, .generation = u64(1) };
        auto physical = Arc<resource_registry::BufferPhysical>::make(
            vulkan::BufferAllocation {}, u64(1), u64(1));
        rstd_assert(table.Insert(resource_registry::PreparedBufferUse {
            .use = use, .buffer = { .physical = rstd::move(physical) } }));
        uses.push(rstd::move(use));
    }
    RenderBufferResolver resolver(table);
    auto                 geometry = view();
    auto                 request  = DrawBufferRequest {
        .render_item = { .index = u32(1), .generation = u64(2) },
        .geometry    = geometry,
        .buffer_uses = slice<resource::BufferUseHandle>::from_raw_parts(uses.begin(), usize(2)),
    };
    auto prepared = resolver.prepareDrawBuffers(request);
    rstd_assert(prepared.is_some());
    auto primary    = rstd::move(prepared).unwrap();
    auto reflection = resolver.prepareDrawBuffers(request).unwrap();
    rstd_assert(primary.draw_count == u32(2));
    rstd_assert(primary.allocation_generation != reflection.allocation_generation);
    Writer writer;
    auto   sink                           = dyn<resource::BufferContentWriter>::from_ref(writer);
    request.dynamic_allocation_generation = primary.allocation_generation + u64(10);
    auto stale =
        RenderBufferResolver::updateDynamicDrawBuffers(request, primary, sink.as_mut_ref());
    rstd_assert(stale.is_err());
    rstd_assert(stale.unwrap_err_unchecked().kind == DrawBufferUpdateErrorKind::NeedsReprepare);
    request.dynamic_allocation_generation = u64();
    rstd_assert(RenderBufferResolver::updateDynamicDrawBuffers(request, primary, sink.as_mut_ref())
                    .is_ok());
    rstd_assert(writer.writes == usize(2));
    rstd_assert(primary.content_confirmed);
    rstd_assert(RenderBufferResolver::updateDynamicDrawBuffers(request, primary, sink.as_mut_ref())
                    .is_ok());
    rstd_assert(writer.writes == usize(2));
    auto reprepared = resolver.prepareDrawBuffers(request).unwrap();
    rstd_assert(! reprepared.content_confirmed);
    writer.fail_on = usize(4);
    rstd_assert(
        RenderBufferResolver::updateDynamicDrawBuffers(request, reprepared, sink.as_mut_ref())
            .is_err());
    rstd_assert(! reprepared.content_confirmed);
    writer.fail_on = usize();
    rstd_assert(
        RenderBufferResolver::updateDynamicDrawBuffers(request, reprepared, sink.as_mut_ref())
            .is_ok());
    rstd_assert(writer.writes == usize(6));
    rstd_assert(reprepared.content_confirmed);
    writer.writes       = usize();
    auto old_generation = primary.vertex_keys[usize()].data_generation;
    first.ResetSize();
    second.ResetSize();
    geometry       = view();
    writer.fail_on = usize(2);
    auto failed =
        RenderBufferResolver::updateDynamicDrawBuffers(request, primary, sink.as_mut_ref());
    rstd_assert(failed.is_err());
    rstd_assert(failed.unwrap_err_unchecked().kind == DrawBufferUpdateErrorKind::UploadFailed);
    rstd_assert(primary.vertex_keys[usize()].data_generation == old_generation);
    rstd_assert(primary.draw_count == u32(2));
    writer.fail_on = usize();
    rstd_assert(RenderBufferResolver::updateDynamicDrawBuffers(request, primary, sink.as_mut_ref())
                    .is_ok());
    rstd_assert(writer.writes == usize(4));
    rstd_assert(primary.draw_count == u32());
    rstd_assert(
        RenderBufferResolver::updateDynamicDrawBuffers(request, reflection, sink.as_mut_ref())
            .is_ok());
    rstd_assert(writer.writes == usize(6));
    rstd_assert(RenderBufferResolver::updateDynamicDrawBuffers(request, primary, sink.as_mut_ref())
                    .is_ok());
    rstd_assert(writer.writes == usize(6));

    auto        storage = first.BufferView().storage_generation;
    VertexArray moved(rstd::move(first));
    rstd_assert(moved.BufferView().storage_generation == storage);
    first    = VertexArray({ { "position"_Str, VertexType::FLOAT3, false } }, usize(3));
    geometry = view();
    rstd_assert(first.BufferView().storage_generation != storage);
    auto replaced =
        RenderBufferResolver::updateDynamicDrawBuffers(request, primary, sink.as_mut_ref());
    rstd_assert(replaced.is_err());
    rstd_assert(replaced.unwrap_err_unchecked().kind == DrawBufferUpdateErrorKind::NeedsReprepare);
    rstd_assert(writer.writes == usize(6));

    geometry.index      = Some(indices.BufferView());
    request.buffer_uses = uses.as_slice();
    auto indexed        = resolver.prepareDrawBuffers(request).unwrap();
    rstd_assert(RenderBufferResolver::updateDynamicDrawBuffers(request, indexed, sink.as_mut_ref())
                    .is_ok());
    rstd_assert(writer.writes == usize(9));
    indices.SetRenderDataCount(usize(1));
    geometry.index = Some(indices.BufferView());
    rstd_assert(RenderBufferResolver::updateDynamicDrawBuffers(request, indexed, sink.as_mut_ref())
                    .is_ok());
    rstd_assert(indexed.draw_count == u32(1) && writer.writes == usize(9));
    geometry.index = None<GeometryBufferView>();
    rstd_assert(RenderBufferResolver::updateDynamicDrawBuffers(request, indexed, sink.as_mut_ref())
                    .is_err());
    rstd_assert(resolver.prepareDrawBuffers(request).is_none());
    request.buffer_uses = slice<resource::BufferUseHandle>::from_raw_parts(uses.begin(), usize(2));
    geometry.vertices[usize()].capacity = usize();
    geometry.vertices[usize()].count    = usize(1);
    rstd_assert(resolver.prepareDrawBuffers(request).is_none());

    geometry                              = view();
    geometry.dynamic                      = false;
    request.dynamic_allocation_generation = u64(99);
    auto retained                         = resolver.prepareDrawBuffers(request).unwrap();
    rstd_assert(retained.allocation_generation == u64());
    rstd_assert(RenderBufferResolver::updateDynamicDrawBuffers(request, retained, sink.as_mut_ref())
                    .is_ok());
    (void)first.RewriteVertices([](VertexWriter& writer) {
        rstd_assert(writer.AppendZeroedVertex().is_some());
    });
    geometry         = view();
    geometry.dynamic = false;
    auto changed_static =
        RenderBufferResolver::updateDynamicDrawBuffers(request, retained, sink.as_mut_ref());
    rstd_assert(changed_static.is_err());
    rstd_assert(changed_static.unwrap_err_unchecked().kind ==
                DrawBufferUpdateErrorKind::NeedsReprepare);
    rstd_assert(writer.writes == usize(9));

    GeometryView empty;
    auto         empty_request = DrawBufferRequest { .geometry = empty };
    auto         empty_buffers = resolver.prepareDrawBuffers(empty_request).unwrap();
    rstd_assert(empty_buffers.draw_count == u32());
    resource_registry::PreparedResourceTable missing;
    RenderBufferResolver                     missing_resolver(missing);
    geometry = view();
    rstd_assert(missing_resolver.prepareDrawBuffers(request).is_none());
}
