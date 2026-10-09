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

auto TextureTypeName(TexNode::TexType type) -> ref<str> {
    switch (type) {
    case TexNode::TexType::Imported: return "Imported"_str;
    case TexNode::TexType::Temp: return "Temp"_str;
    }
    return "Unknown"_str;
}
} // namespace

auto TexNode::ToGraphviz() const -> String { return ToGraphvizWithAllocation(None<ref<str>>()); }

auto TexNode::ToGraphvizWithAllocation(Option<ref<str>> allocation_key) const -> String {
    auto graph_id     = rstd::format("n{}", handle.index);
    auto escaped_key  = DotEscape(key.as_str());
    auto escaped_name = DotEscape(name.as_str());
    auto label = rstd::format("{}[label=\"ref={}\\nresource: {}\\nkey={}\\nkind={}\\nversion={}",
                              graph_id.as_str(),
                              graph_id.as_str(),
                              escaped_name.as_str(),
                              escaped_key.as_str(),
                              TextureTypeName(type),
                              version);
    if (writer) {
        auto field = rstd::format("\\nwriter=n{}", writer->index);
        label.push_str(field.as_str());
    }
    if (previous) {
        auto field = rstd::format("\\nprev=n{}", previous->index);
        label.push_str(field.as_str());
    }
    if (next) {
        auto field = rstd::format("\\nnext=n{}", next->index);
        label.push_str(field.as_str());
    }
    if (allocation_family) {
        auto field = rstd::format("\\nfamily={}", allocation_family->as_str());
        label.push_str(field.as_str());
    }
    if (allocation_parent) {
        auto field = rstd::format("\\nallocation_parent=n{}", allocation_parent->index);
        label.push_str(field.as_str());
    }
    if (allocation_key) {
        auto field = rstd::format("\\nphysical={}", *allocation_key);
        label.push_str(field.as_str());
    }
    label.push_str("\" shape=ellipse]"_str);
    return label;
}
