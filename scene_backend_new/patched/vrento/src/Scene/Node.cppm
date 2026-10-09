export module vrento.node;
import rstd;

using namespace rstd::prelude;
using rstd::sync::Arc;

export namespace vrento
{

// Node exposes NodeState(); Matrix supplies Identity() and parent * local
// composition. Relationships and matrix updates are confined to the owning
// scene thread.
template<typename Node, typename Matrix>
class NodeState {
public:
    explicit NodeState(Node& self): m_self(rstd::addressof(self)) {}
    NodeState(const NodeState&)            = delete;
    NodeState& operator=(const NodeState&) = delete;
    NodeState(NodeState&&)                 = delete;
    NodeState& operator=(NodeState&&)      = delete;

    ~NodeState() {
        UnlinkTransformParent();
        for (auto* dependent : m_dependents) {
            auto& state    = dependent->NodeState();
            state.m_parent = nullptr;
            state.Invalidate();
        }
        for (auto& child : m_children) child->NodeState().m_owner = nullptr;
    }

    Node*                 Parent() const { return m_parent; }
    Node*                 Owner() const { return m_owner; }
    const Vec<Arc<Node>>& Children() const { return m_children; }
    const Matrix&         LocalMatrix() const { return m_local; }
    const Matrix&         WorldMatrix() const { return m_world; }
    u64                   WorldRevision() const { return m_revision; }
    bool                  Visible() const { return m_visible; }
    void                  SetVisible(bool value) { m_visible = value; }

    void SetLocalMatrix(Matrix matrix) {
        m_local = rstd::move(matrix);
        Invalidate();
    }

    void UpdateWorld() {
        if (! m_dirty) return;
        if (m_parent) {
            auto& parent = m_parent->NodeState();
            parent.UpdateWorld();
            m_world = parent.WorldMatrix() * m_local;
        } else {
            m_world = m_local;
        }
        m_dirty = false;
        ++m_revision;
    }

    bool SetTransformParent(Node* parent) {
        if (! CanParent(parent)) return false;
        if (m_parent == parent) return true;
        UnlinkTransformParent();
        m_parent = parent;
        if (m_parent) m_parent->NodeState().m_dependents.push(static_cast<Node*>(m_self));
        Invalidate();
        return true;
    }

    bool AppendChild(Arc<Node> child) {
        auto& state = child->NodeState();
        if (! state.CanParent(m_self)) return false;
        for (auto* ancestor = m_self; ancestor; ancestor = ancestor->NodeState().m_owner)
            if (ancestor == child.as_ptr()) return false;
        if (state.m_owner == m_self) return state.SetTransformParent(m_self);
        if (state.m_owner) state.m_owner->NodeState().RemoveChild(*child);
        state.SetTransformParent(m_self);
        state.m_owner = m_self;
        m_children.push(rstd::move(child));
        return true;
    }

    auto ChildIndex(const Node& child) const -> Option<usize> {
        for (usize index {}; index < m_children.len(); ++index)
            if (m_children[index].as_ptr() == rstd::addressof(child)) return Some(index);
        return None();
    }

    bool RemoveChild(Node& child) {
        auto index = ChildIndex(child);
        if (index.is_none()) return false;
        auto& state   = child.NodeState();
        state.m_owner = nullptr;
        if (state.m_parent == m_self) state.SetTransformParent(nullptr);
        auto* pointer = rstd::addressof(child);
        m_children.retain([pointer](const Arc<Node>& item) {
            return item.as_ptr() != pointer;
        });
        return true;
    }

    void ClearChildren() {
        for (auto& child : m_children) {
            auto& state   = child->NodeState();
            state.m_owner = nullptr;
            if (state.m_parent == m_self) state.SetTransformParent(nullptr);
        }
        m_children.clear();
    }

    bool MoveChild(const Node& child, usize index) {
        if (index >= m_children.len()) return false;
        auto current = ChildIndex(child);
        if (current.is_none() || *current == index) return false;
        auto moving = rstd::move(m_children[*current]);
        if (*current < index) {
            for (auto cursor = *current; cursor < index; ++cursor)
                m_children[cursor] = rstd::move(m_children[cursor + usize(1)]);
        } else {
            for (auto cursor = *current; cursor > index; --cursor)
                m_children[cursor] = rstd::move(m_children[cursor - usize(1)]);
        }
        m_children[index] = rstd::move(moving);
        return true;
    }

private:
    bool CanParent(Node* parent) const {
        for (auto* ancestor = parent; ancestor; ancestor = ancestor->NodeState().m_parent)
            if (ancestor == m_self) return false;
        return true;
    }
    void UnlinkTransformParent() {
        if (! m_parent) return;
        m_parent->NodeState().m_dependents.retain([this](Node* item) {
            return item != m_self;
        });
        m_parent = nullptr;
    }
    void Invalidate() {
        if (m_dirty) return;
        m_dirty = true;
        for (auto* dependent : m_dependents) dependent->NodeState().Invalidate();
    }

    Node*          m_self;
    Node*          m_parent { nullptr };
    Node*          m_owner { nullptr };
    Vec<Arc<Node>> m_children;
    Vec<Node*>     m_dependents;
    Matrix         m_local { Matrix::Identity() };
    Matrix         m_world { Matrix::Identity() };
    u64            m_revision {};
    bool           m_dirty { true };
    bool           m_visible { true };
};
} // namespace vrento
