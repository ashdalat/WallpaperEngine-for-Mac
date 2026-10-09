#include <rstd/macro.hpp>
import rstd;
import vrento.camera;

using namespace rstd::prelude;

// Affine transforms make multiplication order observable without a math dependency.
struct Affine {
    double        scale { 1 };
    double        offset {};
    static Affine Identity() { return {}; }
    bool          operator==(const Affine&) const = default;
    auto          operator*(const Affine& other) const -> Affine {
        return { scale * other.scale, scale * other.offset + offset };
    }
};

int main() {
    vrento::CameraState<Affine> camera;
    const auto                  initial = camera.Snapshot();
    rstd_assert(initial.revision == u64(1));
    rstd_assert(initial.view_projection == Affine::Identity());
    rstd_assert(! camera.Set({}, {}));
    rstd_assert(camera.Set({ 1, 3 }, { 2, 0 }));
    rstd_assert(camera.ViewProjection().scale == 2);
    rstd_assert(camera.ViewProjection().offset == 6);
    rstd_assert(camera.Revision() == u64(2));
    const auto captured = camera.Snapshot();
    rstd_assert(! camera.Set({ 1, 3 }, { 2, 0 }));
    rstd_assert(camera.Set({ 1, 4 }, { 2, 0 }));
    rstd_assert(camera.Revision() == u64(3));
    rstd_assert(camera.Set({ 1, 4 }, { 3, 0 }));
    rstd_assert(camera.Revision() == u64(4));
    rstd_assert(captured.view_projection.offset == 6);

    auto clone = camera;
    rstd_assert(clone.Set({}, {}));
    const auto revision = clone.Revision();
    clone               = camera;
    rstd_assert(clone.Revision() == revision + u64(1));
    clone = camera;
    rstd_assert(clone.Revision() == revision + u64(1));
    rstd_assert(camera.Revision() == u64(4));

    const auto retained = [] {
        vrento::CameraState<Affine> temporary;
        temporary.Set({ 1, 5 }, { 4, 0 });
        return temporary.Snapshot();
    }();
    rstd_assert(retained.view_projection.offset == 20);
    // Different inputs must publish even when their product is unchanged.
    rstd_assert(camera.Set({ 1, 0 }, { 1, 12 }));
    rstd_assert(camera.Set({ 1, 12 }, { 1, 0 }));
}
