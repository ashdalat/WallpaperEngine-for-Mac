#include <rstd/macro.hpp>
import rstd;
import vrento.scene_identity;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace vrento;

int main() {
    NamedIdentityIndex<TextureId> index;
    rstd_assert(! TextureId {}.Valid());
    auto names = Vec<String>::make();
    names.push(String::make("b"_str));
    names.push(String::make("c"_str));
    index.Rebuild(names.as_slice(), u32(7));
    auto b = index.Find("b"_str).unwrap();
    auto c = index.Find("c"_str).unwrap();
    names.clear();
    names.push(String::make("a"_str));
    names.push(String::make("c"_str));
    names.push(String::make("b"_str));
    names.push(String::make("b"_str));
    index.Rebuild(names.as_slice(), u32(7));
    rstd_assert(index.Find("b"_str).unwrap() == b);
    rstd_assert(index.Find("c"_str).unwrap() == c);
    rstd_assert(index.Resolve(b).unwrap() == "b"_str);
    names.clear();
    names.push(String::make("c"_str));
    index.Rebuild(names.as_slice(), u32(7));
    rstd_assert(index.Resolve(b).is_none());
    rstd_assert(index.Find("b"_str).is_none());
    names.push(String::make("b"_str));
    index.Rebuild(names.as_slice(), u32(7));
    rstd_assert(index.Find("b"_str).unwrap() != b);
    rstd_assert(index.Resolve(b).is_none());
    rstd_assert(index.Resolve(c).unwrap() == "c"_str);
    index.Rebuild(names.as_slice(), u32(8));
    rstd_assert(index.Resolve(c).is_none());
    rstd_assert(index.Find("c"_str).unwrap().generation == u32(8));
    rstd_assert(index.Resolve(TextureId {}).is_none());
    rstd_assert(index.Resolve(TextureId { .index = u32(123), .generation = u32(8) }).is_none());
    names.clear();
    index.Rebuild(names.as_slice(), u32(8));
    rstd_assert(index.Find("c"_str).is_none());
}
