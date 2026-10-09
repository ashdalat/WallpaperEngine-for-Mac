export module vrento.rgraph:dependency_graph;
import rstd;

using namespace rstd::prelude;
using rstd::collections::HashMap;

export namespace vrento::rg
{

struct NodeHandle {
    usize index { usize::MAX };

    bool valid() const noexcept { return index != usize::MAX; }

    friend auto operator<=>(const NodeHandle&, const NodeHandle&) = default;
};

} // namespace vrento::rg

export namespace rstd
{

template<>
struct Impl<Copy, vrento::rg::NodeHandle> {};

template<>
struct Impl<hash::Hash, vrento::rg::NodeHandle> : ImplBase<vrento::rg::NodeHandle> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::hash_into(this->self().index, state);
    }
};

} // namespace rstd

export namespace vrento::rg
{

struct Node {
    using Trait                  = Node;
    static constexpr bool direct = false;

    template<typename Self, typename = void>
    struct Api {
        using Trait = Node;

        auto Handle() const noexcept -> NodeHandle { return rstd::trait_call<0>(this); }
        auto ToGraphviz() const -> String { return rstd::trait_call<1>(this); }
    };

    template<typename T>
    using Funcs = rstd::TraitFuncs<&T::Handle, &T::ToGraphviz>;
};

struct NodeLinks {
    Vec<NodeHandle> incoming;
    Vec<NodeHandle> outgoing;
};

struct DependencyGraph {
    auto AddNode() -> NodeHandle;
    auto Connect(NodeHandle from, NodeHandle to) -> bool;

    auto Contains(NodeHandle handle) const -> bool;
    auto NodeNum() const noexcept -> usize;
    auto EdgeNum() const noexcept -> usize;
    auto GetNodeOut(NodeHandle handle) const -> slice<NodeHandle>;
    auto GetNodeIn(NodeHandle handle) const -> slice<NodeHandle>;

    auto HasCycle() const -> bool;
    auto TopologicalOrder() const -> Vec<NodeHandle>;

private:
    using LinkMap = HashMap<NodeHandle, NodeLinks>;

    usize   m_next_index { 0 };
    LinkMap m_nodes;
};

} // namespace vrento::rg
