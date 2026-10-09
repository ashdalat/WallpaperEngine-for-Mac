#include <rstd/macro.hpp>
import rstd;
import vrento.node;

using namespace rstd::prelude;
using rstd::sync::Arc;

// Scalar composition isolates relationship/cache behavior from the host's math
// library.
struct ScalarMatrix {
    int                 value { 1 };
    static ScalarMatrix Identity() { return {}; }
    ScalarMatrix operator*(const ScalarMatrix& other) const { return { value * other.value }; }
};

struct Node {
    vrento::NodeState<Node, ScalarMatrix> state { *this };
    auto&                                 NodeState() { return state; }
    const auto&                           NodeState() const { return state; }
};

int main() {
    auto root  = Arc<Node>::make();
    auto child = Arc<Node>::make();
    auto leaf  = Arc<Node>::make();
    Node anchor;
    root->state.SetLocalMatrix({ 2 });
    child->state.SetLocalMatrix({ 3 });
    leaf->state.SetLocalMatrix({ 5 });
    rstd_assert(root->state.AppendChild(child.clone()));
    rstd_assert(child->state.AppendChild(leaf.clone()));
    rstd_assert(anchor.state.SetTransformParent(child.as_ptr()));
    leaf->state.UpdateWorld();
    anchor.state.UpdateWorld();
    rstd_assert(leaf->state.WorldMatrix().value == 30);
    rstd_assert(anchor.state.WorldMatrix().value == 6);
    rstd_assert(child->state.Children().len() == usize(1));
    auto revision = leaf->state.WorldRevision();
    leaf->state.UpdateWorld();
    rstd_assert(leaf->state.WorldRevision() == revision);
    root->state.SetLocalMatrix({ 7 });
    leaf->state.UpdateWorld();
    anchor.state.UpdateWorld();
    rstd_assert(leaf->state.WorldMatrix().value == 105);
    rstd_assert(anchor.state.WorldMatrix().value == 21);
    rstd_assert(! root->state.SetTransformParent(leaf.as_ptr()));
    rstd_assert(! leaf->state.AppendChild(root.clone()));
    rstd_assert(root->state.Parent() == nullptr);

    rstd_assert(root->state.AppendChild(leaf.clone()));
    rstd_assert(child->state.Children().is_empty());
    rstd_assert(root->state.Children().len() == usize(2));
    rstd_assert(root->state.AppendChild(leaf.clone()));
    rstd_assert(root->state.Children().len() == usize(2));
    rstd_assert(root->state.MoveChild(*leaf, usize()));
    rstd_assert(root->state.Children()[usize()].as_ptr() == leaf.as_ptr());
    rstd_assert(! root->state.MoveChild(*leaf, usize(2)));
    leaf->state.UpdateWorld();
    rstd_assert(leaf->state.WorldMatrix().value == 35);
    rstd_assert(root->state.RemoveChild(*leaf));
    leaf->state.UpdateWorld();
    rstd_assert(leaf->state.WorldMatrix().value == 5);
    rstd_assert(leaf->state.Parent() == nullptr && leaf->state.Owner() == nullptr);
    rstd_assert(! root->state.RemoveChild(*leaf));

    Node other;
    other.state.SetLocalMatrix({ 11 });
    rstd_assert(child->state.SetTransformParent(&other));
    root->state.ClearChildren();
    rstd_assert(child->state.Owner() == nullptr && child->state.Parent() == &other);
    child->state.UpdateWorld();
    rstd_assert(child->state.WorldMatrix().value == 33);
    child->state.SetVisible(false);
    rstd_assert(! child->state.Visible());

    auto survivor = Arc<Node>::make();
    Node dependent;
    survivor->state.SetLocalMatrix({ 13 });
    {
        Node parent;
        parent.state.SetLocalMatrix({ 2 });
        parent.state.AppendChild(survivor.clone());
        dependent.state.SetTransformParent(&parent);
        survivor->state.UpdateWorld();
        dependent.state.UpdateWorld();
        rstd_assert(survivor->state.WorldMatrix().value == 26);
    }
    rstd_assert(survivor->state.Parent() == nullptr && survivor->state.Owner() == nullptr);
    survivor->state.UpdateWorld();
    dependent.state.UpdateWorld();
    rstd_assert(survivor->state.WorldMatrix().value == 13);
    rstd_assert(dependent.state.WorldMatrix().value == 1);

    // Transform detachment must not allow a cycle in the owning tree.
    root->state.AppendChild(child.clone());
    child->state.SetTransformParent(nullptr);
    rstd_assert(! child->state.AppendChild(root.clone()));
}
