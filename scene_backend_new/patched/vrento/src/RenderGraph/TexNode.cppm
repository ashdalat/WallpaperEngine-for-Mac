export module vrento.rgraph:tex_node;
import rstd;
import vrento.resource;
import :dependency_graph;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace vrento::rg
{

struct TexNode {
    enum class TexType
    {
        Imported,
        Temp
    };

    NodeHandle                       handle;
    TexType                          type { TexType::Imported };
    String                           key;
    String                           name { "unknown tex"_Str };
    Option<resource::TextureRequest> request;
    Option<String>                   allocation_family;
    Option<NodeHandle>               allocation_parent;
    usize                            version { 0 };
    Option<NodeHandle>               previous;
    Option<NodeHandle>               next;
    Option<NodeHandle>               writer;
    Vec<NodeHandle>                  readers;

    auto Handle() const noexcept -> NodeHandle { return handle; }
    auto ToGraphviz() const -> String;
    auto ToGraphvizWithAllocation(Option<ref<str>> allocation_key) const -> String;
};

} // namespace vrento::rg
