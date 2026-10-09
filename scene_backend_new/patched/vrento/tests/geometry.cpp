#include <rstd/macro.hpp>

import rstd;
import vrento.geometry;

using namespace rstd::literals;

using namespace rstd::prelude;
using namespace vrento;

int main() {
    auto attributes = Vec<VertexArray::VertexAttribute>::with_capacity(usize(2));
    attributes.push({ "position"_Str, VertexType::FLOAT3, true });
    attributes.push({ "uv"_Str, VertexType::FLOAT2, false });
    VertexArray layout(rstd::move(attributes), usize(2));
    rstd_assert(layout.Attributes().len() == usize(2));
    rstd_assert(layout.Attributes()[usize(1)].name.as_str() == "uv"_str);
    rstd_assert(layout.OneSize() == usize(6));
    rstd_assert(layout.AttributeOffset("position"_str).unwrap() == usize());
    rstd_assert(layout.AttributeOffset("uv"_str).unwrap() == usize(4 * sizeof(float)));
    rstd_assert(layout.AttributeOffset("missing"_str).is_none());
    const array<float, 10> packed { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f };
    rstd_assert(layout.AddVertex(packed.begin()));
    rstd_assert(layout.AddVertex(packed.begin() + 5));
    rstd_assert(layout.Data()[3] == 0.0f && layout.Data()[9] == 0.0f);
    const array<float, 4> uv { 0.25f, 0.5f, 0.75f, 1.0f };
    auto                  layout_generation = layout.DataGeneration();
    rstd_assert(layout.SetVertex("uv"_str, uv.as_slice()));
    rstd_assert(layout.DataGeneration() == layout_generation + u64(1));
    rstd_assert(layout.Data()[4] == 0.25f && layout.Data()[10] == 0.75f);
    rstd_assert(layout.Data()[0] == 1.0f && layout.Data()[6] == 6.0f);
    VertexArray duplicate(
        { { "uv"_Str, VertexType::FLOAT2, false }, { "uv"_Str, VertexType::FLOAT4, true } },
        usize(1));
    rstd_assert(duplicate.AttributeOffset("uv"_str).unwrap() == usize(2 * sizeof(float)));

    VertexArray vertices({ { "position"_Str, VertexType::FLOAT3, false } }, usize(2));
    rstd_assert(vertices.ID() == u32::MAX);
    auto generation = vertices.DataGeneration();
    auto written    = vertices.RewriteVertices([](VertexWriter& writer) {
        auto first = writer.AppendZeroedVertex();
        rstd_assert(first.is_some());
        (*first)[usize()] = 1.0f;
        auto second       = writer.AppendZeroedVertex();
        rstd_assert(second.is_some());
        (*second)[usize()] = 2.0f;
        rstd_assert(writer.AppendZeroedVertex().is_none());
    });
    rstd_assert(written.vertex_count == usize(2) && written.overflowed);
    rstd_assert(vertices.DataGeneration() == generation + u64(1));
    rstd_assert(vertices.VertexCount() == usize(2));
    rstd_assert(vertices.Data()[3] == 2.0f);
    const float malformed[] { 1.0f, 2.0f };
    generation = vertices.DataGeneration();
    rstd_assert(
        ! vertices.SetVertex("position"_str, slice<float>::from_raw_parts(malformed, usize(2))));
    rstd_assert(vertices.DataGeneration() == generation);
    rstd_assert(! vertices.SetVertexs(usize::MAX, {}));
    auto*       data = vertices.Data();
    VertexArray moved(rstd::move(vertices));
    rstd_assert(vertices.BufferView().bytes.is_empty());
    rstd_assert(vertices.BufferView().count == usize());
    rstd_assert(moved.Data() == data);
    rstd_assert(moved.DataGeneration() == generation);
    moved.ResetSize();
    rstd_assert(moved.VertexCount() == usize());
    rstd_assert(moved.CapacitySize() == usize(6));

    VertexArray empty({}, usize(2));
    auto        result = empty.RewriteVertices([](VertexWriter& writer) {
        rstd_assert(writer.AppendZeroedVertex().is_none());
    });
    rstd_assert(result.vertex_count == usize() && result.overflowed);

    IndexArray     indices(usize(3));
    const uint32_t input[] { 0, 1, 0 };
    auto           view = slice<uint32_t>::from_raw_parts(input, usize(3));
    indices.Assign(usize(), view);
    rstd_assert(indices.DataCount() == usize(3));
    rstd_assert(indices.RenderDataCount() == usize(3));
    indices.SetRenderDataCount(usize(2));
    rstd_assert(indices.RenderDataCount() == usize(2));
    generation = indices.DataGeneration();
    indices.Assign(usize::MAX, view);
    rstd_assert(indices.DataGeneration() == generation);
    IndexArray moved_indices(rstd::move(indices));
    rstd_assert(indices.BufferView().bytes.is_empty());
    rstd_assert(indices.BufferView().count == usize());
    rstd_assert(moved_indices.Data()[1] == 1);
    rstd_assert(moved_indices.RenderDataCount() == usize(2));
}
