export module vrento.rgraph:pass_node;
import rstd;

import :dependency_graph;
import :pass;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace vrento::rg
{

struct PassNode {
    enum class Type
    {
        CustomShader,
        Copy,
        Virtual
    };

    NodeHandle handle;
    PassHandle pass;
    Type       type { Type::CustomShader };
    String     name { "unknown pass"_Str };

    auto Handle() const noexcept -> NodeHandle { return handle; }
    auto ToGraphviz() const -> String;
};

} // namespace vrento::rg
