#include <rstd/test/gtest.hpp>

import rstd.bench;
import wescene.load_bench;

using namespace rstd::literals;

namespace
{

auto ProbeLabel(const owe::SceneLoadBenchContext& context, rstd::bench::probe::ProbeId id)
    -> rstd::ref<rstd::str> {
    auto schema = context.schema_owner();
    return schema->label(id).unwrap();
}

TEST(SceneLoadBench, EmptyOutputDisablesCollection) {
    EXPECT_TRUE(owe::CreateSceneLoadBench(""_str).is_none());
}

TEST(SceneLoadBench, RegistersStableCatalog) {
    auto context = owe::CreateSceneLoadBench("/tmp/owe-load-bench-test.txt"_str);
    ASSERT_TRUE(context.is_some());
    auto& bench = **context;

    auto schema = bench.schema_owner();
    EXPECT_EQ(schema->len().to_primitive(), 47u);
    EXPECT_EQ(ProbeLabel(bench, bench.ids().preload_scene_document), "preload.scene_document"_str);
    EXPECT_EQ(ProbeLabel(bench, bench.ids().parse_object_particle), "parse.object.particle"_str);
    EXPECT_EQ(ProbeLabel(bench, bench.ids().load_initial_properties),
              "load.initial_properties"_str);
    EXPECT_EQ(ProbeLabel(bench, bench.ids().render_user_property_graph_rebuild),
              "render.user_property.graph_rebuild"_str);
    EXPECT_EQ(ProbeLabel(bench, bench.ids().render_first_frame_prepare),
              "render.first_frame.prepare"_str);
    EXPECT_EQ(ProbeLabel(bench, bench.ids().render_first_draw), "render.first_draw"_str);
}

TEST(SceneLoadBench, AssignsIncreasingRunIds) {
    auto first  = owe::CreateSceneLoadBench("/tmp/owe-load-bench-first.txt"_str);
    auto second = owe::CreateSceneLoadBench("/tmp/owe-load-bench-second.txt"_str);

    ASSERT_TRUE(first.is_some());
    ASSERT_TRUE(second.is_some());
    EXPECT_LT((**first).run_id().to_primitive(), (**second).run_id().to_primitive());
}

TEST(SceneLoadBench, MovesPreloadBatchOnce) {
    auto context = owe::CreateSceneLoadBench("/tmp/owe-load-bench-test.txt"_str);
    ASSERT_TRUE(context.is_some());
    auto& bench = **context;

    auto recorder = bench.session().recorder();
    {
        auto span = recorder.span(bench.ids().preload_scene_document);
    }
    auto drained = recorder.drain();
    ASSERT_TRUE(drained.is_ok());
    bench.add_preload_batch(rstd::move(drained).unwrap_unchecked());

    auto batches = bench.take_preload_batches();
    ASSERT_EQ(batches.len().to_primitive(), 1u);
    EXPECT_TRUE(bench.take_preload_batches().is_empty());

    auto collector = rstd::bench::probe::ProbeCollector::new_(bench.schema_owner());
    ASSERT_TRUE(collector.ingest(batches[rstd::usize()]).is_ok());
    auto report = rstd::move(collector).finish();
    ASSERT_EQ(report.overall().len().to_primitive(), 1u);
    EXPECT_EQ(report.overall()[rstd::usize()].count.to_primitive(), 1u);
    EXPECT_EQ(report.dropped_samples().to_primitive(), 0u);
    EXPECT_TRUE(report.diagnostics().is_empty());
}

} // namespace
