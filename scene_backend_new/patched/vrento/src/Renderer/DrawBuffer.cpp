module vrento.draw_buffer;
import rstd;
import vrento.resource_registry;
using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;

namespace vrento
{
namespace
{
u64 next_allocation_generation() {
    static Atomic<u64> next { u64(1) };
    return next.fetch_add(u64(1), Ordering::Relaxed);
}

bool ValidBuffer(const GeometryBufferView& view) {
    return view.bytes.len() <= view.capacity && view.count <= usize(u32::MAX.to_primitive()) &&
           (view.stride != usize() ? view.count <= view.bytes.len() / view.stride
                                   : view.count == usize() && view.bytes.is_empty());
}

auto DrawCount(const GeometryView& geometry) -> Option<u32> {
    usize count = geometry.vertices.is_empty() ? usize() : usize::MAX;
    for (const auto& vertex : geometry.vertices) {
        if (! ValidBuffer(vertex)) return None();
        if (vertex.count < count) count = vertex.count;
    }
    if (geometry.index.is_some()) {
        if (! ValidBuffer(*geometry.index)) return None();
        count = geometry.index->count;
    }
    return Some(rstd::as_cast<u32>(count));
}

bool SameStorage(const DrawBufferKey& key, const GeometryBufferView& view) {
    return key.storage_generation == view.storage_generation && key.capacity == view.capacity &&
           key.stride == view.stride;
}
} // namespace

String BuildDrawBufferResourceName(DrawId draw, DrawBufferRole role, u32 stream) {
    if (! draw.Valid()) return {};
    const char* name = "uniform";
    if (role == DrawBufferRole::Vertex) name = "vertex";
    if (role == DrawBufferRole::Index) name = "index";
    return rstd::format("draw:{}:{}:{}:{}", draw.generation, draw.index, name, stream);
}

auto BuildDrawBufferKeys(const DrawBufferRequest& request, u64 allocation_generation)
    -> Vec<DrawBufferKey> {
    Vec<DrawBufferKey> keys;
    auto append = [&](const GeometryBufferView& view, DrawBufferRole role, u32 stream) {
        keys.push(DrawBufferKey {
            .render_item           = request.render_item,
            .role                  = role,
            .submesh_index         = request.submesh_index,
            .stream_index          = stream,
            .data_generation       = view.data_generation,
            .allocation_generation = request.geometry.dynamic ? allocation_generation : u64(),
            .storage_generation    = view.storage_generation,
            .capacity              = view.capacity,
            .stride                = view.stride,
        });
    };
    for (usize i {}; i < request.geometry.vertices.len(); ++i)
        append(request.geometry.vertices[i], DrawBufferRole::Vertex, rstd::as_cast<u32>(i));
    if (request.geometry.index.is_some())
        append(*request.geometry.index, DrawBufferRole::Index, u32());
    return keys;
}

auto RenderBufferResolver::prepareDrawBuffers(const DrawBufferRequest& request)
    -> Option<DrawBufferRefs> {
    auto count = DrawCount(request.geometry);
    if (count.is_none()) return None();
    const auto streams  = request.geometry.vertices.len();
    const auto required = streams + (request.geometry.index.is_some() ? usize(1) : usize());
    if (request.buffer_uses.len() != required) return None();
    for (auto use : request.buffer_uses)
        if (m_resources.Resolve(use).is_none()) return None();

    DrawBufferRefs out;
    out.render_item = request.render_item;
    out.dynamic     = request.geometry.dynamic;
    // Reused dynamic allocations can still contain an earlier geometry version.
    out.content_confirmed = ! out.dynamic;
    out.draw_count        = *count;
    if (out.dynamic)
        out.allocation_generation = request.dynamic_allocation_generation != u64()
                                        ? request.dynamic_allocation_generation
                                        : next_allocation_generation();
    auto keys = BuildDrawBufferKeys(request, out.allocation_generation);
    for (usize i {}; i < streams; ++i) {
        out.vertex_keys.push(rstd::move(keys[i]));
        out.vertices.push(resource::BufferUseHandle(request.buffer_uses[i]));
    }
    if (request.geometry.index.is_some()) {
        out.index_key = Some(rstd::move(keys[streams]));
        out.index     = Some(resource::BufferUseHandle(request.buffer_uses[streams]));
    }
    return Some(rstd::move(out));
}

auto RenderBufferResolver::updateDynamicDrawBuffers(
    const DrawBufferRequest& request, DrawBufferRefs& buffers,
    mut_ref<dyn<resource::BufferContentWriter>> writer) -> Result<empty, DrawBufferUpdateError> {
    const auto& geometry = request.geometry;
    auto        count    = DrawCount(geometry);
    if (count.is_none())
        return Err(DrawBufferUpdateError { DrawBufferUpdateErrorKind::InvalidGeometry,
                                           "invalid geometry range"_Str });
    auto reprepare = [] {
        return Err(DrawBufferUpdateError { DrawBufferUpdateErrorKind::NeedsReprepare,
                                           "geometry binding changed"_Str });
    };
    if (buffers.render_item != request.render_item || buffers.dynamic != geometry.dynamic ||
        (buffers.dynamic && request.dynamic_allocation_generation != u64() &&
         request.dynamic_allocation_generation != buffers.allocation_generation) ||
        buffers.vertices.len() != geometry.vertices.len() ||
        buffers.vertex_keys.len() != geometry.vertices.len() ||
        buffers.index.is_some() != geometry.index.is_some() ||
        buffers.index_key.is_some() != geometry.index.is_some())
        return reprepare();
    for (usize i {}; i < geometry.vertices.len(); ++i) {
        const auto& key = buffers.vertex_keys[i];
        if (key.submesh_index != request.submesh_index || ! SameStorage(key, geometry.vertices[i]))
            return reprepare();
    }
    if (geometry.index.is_some() && (buffers.index_key->submesh_index != request.submesh_index ||
                                     ! SameStorage(*buffers.index_key, *geometry.index)))
        return reprepare();

    for (usize i {}; i < geometry.vertices.len(); ++i) {
        const auto& view = geometry.vertices[i];
        if (buffers.content_confirmed &&
            buffers.vertex_keys[i].data_generation == view.data_generation)
            continue;
        if (! buffers.dynamic) return reprepare();
        auto updated = writer->UpdateBuffer(buffers.vertices[i], view.bytes);
        if (updated.is_err())
            return Err(
                DrawBufferUpdateError { DrawBufferUpdateErrorKind::UploadFailed,
                                        rstd::move(updated).unwrap_err_unchecked().message });
    }
    if (geometry.index.is_some() &&
        (! buffers.content_confirmed ||
         buffers.index_key->data_generation != geometry.index->data_generation)) {
        if (! buffers.dynamic) return reprepare();
        auto updated = writer->UpdateBuffer(*buffers.index, geometry.index->bytes);
        if (updated.is_err())
            return Err(
                DrawBufferUpdateError { DrawBufferUpdateErrorKind::UploadFailed,
                                        rstd::move(updated).unwrap_err_unchecked().message });
    }
    // Confirm only after all writes succeed; a partial failure retries unchanged input.
    for (usize i {}; i < geometry.vertices.len(); ++i)
        buffers.vertex_keys[i].data_generation = geometry.vertices[i].data_generation;
    if (geometry.index.is_some())
        buffers.index_key->data_generation = geometry.index->data_generation;
    buffers.draw_count        = *count;
    buffers.content_confirmed = true;
    return Ok(empty {});
}
} // namespace vrento
