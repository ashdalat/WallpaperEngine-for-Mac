export module vrento.material;
export import vrento.graphics_types;
export import vrento.texture_types;
import rstd;

using namespace rstd::prelude;

export namespace vrento
{
struct MaterialPipelineDesc {
    BlendMode    blend_mode { BlendMode::Disable };
    Option<bool> alpha_write;
    bool         depth_test { false };
    bool         depth_write { false };
    CompareOp    depth_compare { CompareOp::LessEqual };
    CullMode     cull_mode { CullMode::None };
    bool         depth_clamp { false };
    Option<bool> depth_clip;
    bool         depth_bias { false };
    float        depth_bias_constant {};
    float        depth_bias_clamp {};
    float        depth_bias_slope {};

    friend bool operator==(const MaterialPipelineDesc& a, const MaterialPipelineDesc& b) {
        return a.blend_mode == b.blend_mode && a.alpha_write.is_some() == b.alpha_write.is_some() &&
               (a.alpha_write.is_none() || *a.alpha_write == *b.alpha_write) &&
               a.depth_test == b.depth_test && a.depth_write == b.depth_write &&
               a.depth_compare == b.depth_compare && a.cull_mode == b.cull_mode &&
               a.depth_clamp == b.depth_clamp && a.depth_clip == b.depth_clip &&
               a.depth_bias == b.depth_bias && a.depth_bias_constant == b.depth_bias_constant &&
               a.depth_bias_clamp == b.depth_bias_clamp && a.depth_bias_slope == b.depth_bias_slope;
    }
};

struct MaterialPipelineSnapshot {
    MaterialPipelineDesc value;
    u64                  revision {};
};

class MaterialPipelineState {
public:
    MaterialPipelineState()                             = default;
    MaterialPipelineState(const MaterialPipelineState&) = default;
    auto operator=(const MaterialPipelineState& other) -> MaterialPipelineState& {
        if (this != &other) Set(other.Value());
        return *this;
    }
    const MaterialPipelineDesc& Value() const { return m_value; }
    u64                         Revision() const { return m_revision; }
    auto Snapshot() const -> MaterialPipelineSnapshot { return { m_value, m_revision }; }
    bool Set(MaterialPipelineDesc value) {
        if (m_value == value) return false;
        m_value = rstd::move(value);
        ++m_revision;
        if (m_revision == u64()) m_revision = u64(1);
        return true;
    }

private:
    MaterialPipelineDesc m_value;
    u64                  m_revision { 1 };
};
} // namespace vrento
