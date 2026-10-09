export module vrento.camera;
import rstd;

using namespace rstd::prelude;

export namespace vrento
{
// Matrix is an owning value with Identity(), exact equality and multiplication.
// Column vectors are transformed by projection * view; clip conventions belong to the caller.
template<typename Matrix>
struct CameraSnapshot {
    Matrix view { Matrix::Identity() };
    Matrix projection { Matrix::Identity() };
    Matrix view_projection { Matrix::Identity() };
    u64    revision { 1 };
};

template<typename Matrix>
class CameraState {
public:
    CameraState()                   = default;
    CameraState(const CameraState&) = default;
    auto operator=(const CameraState& other) -> CameraState& {
        if (this != &other) Set(other.View(), other.Projection());
        return *this;
    }

    const Matrix& View() const { return m_state.view; }
    const Matrix& Projection() const { return m_state.projection; }
    const Matrix& ViewProjection() const { return m_state.view_projection; }
    u64           Revision() const { return m_state.revision; }
    auto          Snapshot() const -> CameraSnapshot<Matrix> { return m_state; }

    bool Set(Matrix view, Matrix projection) {
        if (m_state.view == view && m_state.projection == projection) return false;
        Matrix combined         = projection * view;
        m_state.view            = rstd::move(view);
        m_state.projection      = rstd::move(projection);
        m_state.view_projection = rstd::move(combined);
        ++m_state.revision;
        if (m_state.revision == u64()) m_state.revision = u64(1);
        return true;
    }

private:
    CameraSnapshot<Matrix> m_state;
};
} // namespace vrento
