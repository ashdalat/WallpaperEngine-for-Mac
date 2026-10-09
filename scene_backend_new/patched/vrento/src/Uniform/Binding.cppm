export module vrento.uniform_binding;
export import vrento.uniform_source;
export import vrento.uniform_buffer;
import vrento.resource;
import rstd;

using namespace rstd::prelude;

export namespace vrento
{
struct BoundUniformOutput {
    UniformOutputId output;
    usize           slot_index {};
};

struct BoundUniformSource {
    UniformSourceOwner                    owner;
    ref<dyn<UniformSource>>               source;
    i32                                   priority {};
    Vec<BoundUniformOutput>               outputs;
    Option<Box<dyn<UniformBindingLease>>> lease;
    u64                                   version {};
    bool                                  evaluated { false };
};

auto PrepareUniformSource(const UniformBufferLayout&, UniformSourceOwner, i32 priority,
                          ShaderMatrixConvention, ShaderMatrixAbi)
    -> Result<Option<BoundUniformSource>, UniformBufferUpdateError>;

struct UniformParameter {
    ref<str>         name;
    UniformValueView value;
};

class UniformBinding {
public:
    UniformBinding(resource::BufferUseHandle, UniformBufferLayout, Vec<BoundUniformSource>,
                   ShaderMatrixConvention, ShaderMatrixAbi);
    auto Buffer() const -> resource::BufferUseHandle { return m_buffer; }
    auto SetParameters(slice<UniformParameter>) -> Result<empty, UniformBufferUpdateError>;
    auto Update(ref<dyn<UniformUpdateContext>>, mut_ref<dyn<resource::BufferContentWriter>>)
        -> Result<empty, UniformBufferUpdateError>;

private:
    resource::BufferUseHandle m_buffer;
    UniformBufferLayout       m_layout;
    Vec<BoundUniformSource>   m_sources;
    Vec<u8>                   m_base;
    ShaderMatrixConvention    m_convention;
    ShaderMatrixAbi           m_abi;
    bool                      m_dirty { true };
};
} // namespace vrento
