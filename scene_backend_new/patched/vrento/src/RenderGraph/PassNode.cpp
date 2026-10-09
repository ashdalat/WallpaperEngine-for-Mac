module vrento.rgraph;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace vrento::rg;

namespace
{
auto DotEscape(ref<str> value) -> String {
    auto out = String::make();
    for (auto value_byte : value) {
        switch (static_cast<char>(value_byte.to_primitive())) {
        case '\\': out.push_str("\\\\"_str); break;
        case '"': out.push_str("\\\""_str); break;
        case '\n': out.push_str("\\n"_str); break;
        case '\r': break;
        default: out.push_ascii(value_byte); break;
        }
    }
    return out;
}

auto PassTypeName(PassNode::Type type) -> ref<str> {
    switch (type) {
    case PassNode::Type::CustomShader: return "CustomShader"_str;
    case PassNode::Type::Copy: return "Copy"_str;
    case PassNode::Type::Virtual: return "Virtual"_str;
    }
    return "Unknown"_str;
}
} // namespace

auto PassNode::ToGraphviz() const -> String {
    auto graph_id = rstd::format("n{}", handle.index);
    auto escaped  = DotEscape(name.as_str());
    return rstd::format("{}[label=\"ref={}\\npass-ref=p{}\\npass: {}\\ntype={}\"]",
                        graph_id.as_str(),
                        graph_id.as_str(),
                        pass.index,
                        escaped.as_str(),
                        PassTypeName(type));
}
