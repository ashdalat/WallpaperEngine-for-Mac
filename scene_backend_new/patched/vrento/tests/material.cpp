#include <rstd/macro.hpp>
import rstd;
import vrento.material;
using namespace rstd::prelude;
using namespace vrento;

int main() {
    MaterialPipelineState state;
    const auto            initial = state.Snapshot();
    rstd_assert(initial.revision == u64(1));
    rstd_assert(initial.value.blend_mode == BlendMode::Disable);
    rstd_assert(initial.value.alpha_write.is_none());
    rstd_assert(! state.Set(initial.value));
    auto value        = initial.value;
    value.alpha_write = Some(false);
    rstd_assert(state.Set(value));
    rstd_assert(state.Revision() == u64(2));
    rstd_assert(! state.Set(value));
    rstd_assert(state.Revision() == u64(2));
    value.blend_mode          = BlendMode::Additive;
    value.depth_test          = true;
    value.depth_write         = true;
    value.depth_compare       = CompareOp::Greater;
    value.cull_mode           = CullMode::Front;
    value.depth_clamp         = true;
    value.depth_bias          = true;
    value.depth_bias_constant = 1.0f;
    value.depth_bias_clamp    = 2.0f;
    value.depth_bias_slope    = -4.0f;
    rstd_assert(state.Set(value));
    const auto captured = state.Snapshot();
    rstd_assert(captured.revision == u64(3) && captured.value == value);
    rstd_assert(initial.value.blend_mode == BlendMode::Disable);
    MaterialPipelineState clone(state);
    value.depth_test = false;
    rstd_assert(clone.Set(value));
    rstd_assert(state.Value().depth_test);
    rstd_assert(captured.value.depth_test);
    auto revision = clone.Revision();
    clone         = state;
    rstd_assert(clone.Revision() == revision + u64(1));
    rstd_assert(clone.Value() == state.Value());
    clone = state;
    rstd_assert(clone.Revision() == revision + u64(1));
    auto check_change = [&](auto edit) {
        auto next = state.Value();
        edit(next);
        auto before = state.Revision();
        rstd_assert(state.Set(next));
        rstd_assert(state.Revision() == before + u64(1));
        rstd_assert(! state.Set(next));
    };
    check_change([](auto& next) {
        next.alpha_write = None<bool>();
    });
    check_change([](auto& next) {
        next.blend_mode = BlendMode::Normal;
    });
    check_change([](auto& next) {
        next.depth_test = false;
    });
    check_change([](auto& next) {
        next.depth_write = false;
    });
    check_change([](auto& next) {
        next.depth_compare = CompareOp::Less;
    });
    check_change([](auto& next) {
        next.cull_mode = CullMode::Back;
    });
    check_change([](auto& next) {
        next.depth_clamp = false;
    });
    check_change([](auto& next) {
        next.depth_clip = Some(true);
    });
    check_change([](auto& next) {
        next.depth_clip = Some(false);
    });
    check_change([](auto& next) {
        next.depth_clip = None();
    });
    check_change([](auto& next) {
        next.depth_bias = false;
    });
    check_change([](auto& next) {
        next.depth_bias_constant = 5.0f;
    });
    check_change([](auto& next) {
        next.depth_bias_clamp = 6.0f;
    });
    check_change([](auto& next) {
        next.depth_bias_slope = -7.0f;
    });
    rstd_assert(captured.value.depth_bias_slope == -4.0f);
}
