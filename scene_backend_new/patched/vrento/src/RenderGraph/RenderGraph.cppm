export module vrento.rgraph:render_graph;
import rstd;
import vrento.resource;

import :dependency_graph;
import :pass;
import :pass_node;
import :tex_node;

using namespace rstd::prelude;
using rstd::collections::HashMap;

export namespace vrento::rg
{

class RenderGraph;

struct TextureNodeRef {
    NodeHandle handle;

    bool valid() const noexcept { return handle.valid(); }
};

enum class TextureKind
{
    Imported,
    Temp,
};

struct TextureDesc {
    String                           name;
    String                           key;
    TextureKind                      kind { TextureKind::Imported };
    Option<resource::TextureRequest> request;
    Option<String>                   allocation_family;
};

struct TextureNodeState {
    TextureNodeRef             ref;
    resource::TextureUseHandle use;
    TextureDesc                desc;
    usize                      version { 0 };
};

struct PassNodeState {
    NodeHandle     handle;
    PassHandle     pass;
    String         name;
    PassNode::Type type { PassNode::Type::CustomShader };
};

enum class RenderGraphOrderError
{
    Cycle,
};

struct RenderGraphBuilder {
    auto createTexture(const TextureDesc&, bool write = false) -> TextureNodeRef;
    void read(TextureNodeRef);
    void write(TextureNodeRef);
    auto textureState(TextureNodeRef) const -> Option<TextureNodeState>;
    auto workPassNode() const -> const PassNode&;
    void markSelfWrite(TextureNodeRef);
    void markVirtualWrite(TextureNodeRef);
    void reusePreviousAllocation(TextureNodeRef);

private:
    friend class RenderGraph;

    RenderGraphBuilder(RenderGraph&, NodeHandle);

    auto createTextureNode(const TextureDesc&, bool write) -> TextureNodeRef;
    void readTextureNode(TextureNodeRef);
    void writeTextureNode(TextureNodeRef);

    RenderGraph& m_rg;
    NodeHandle   m_passnode_wip;
};

class RenderGraph {
public:
    RenderGraph();

    auto getPass(PassHandle) -> Option<Pass&>;
    auto getPass(PassHandle) const -> Option<const Pass&>;
    auto passState(NodeHandle) const -> Option<PassNodeState>;
    auto textureState(TextureNodeRef) const -> Option<TextureNodeState>;
    auto latestTexture(ref<str> key) const -> Option<TextureNodeRef>;
    auto readTexture(NodeHandle pass_node, TextureNodeRef texture) -> bool;

    auto topologicalOrder() const -> Result<Vec<NodeHandle>, RenderGraphOrderError>;
    auto getLastReadTextures(slice<NodeHandle>) const -> Vec<Vec<TextureNodeState>>;
    auto resourcePlan() const -> resource::ResourcePlan;

    void ToGraphviz(ref<str> path) const;

    template<typename TPass, typename CB>
    auto addPass(ref<str> name, PassNode::Type type, CB&& callback) -> NodeHandle {
        using Desc = typename TPass::Desc;

        auto node_handle = m_dg.AddNode();
        auto pass_handle = PassHandle { .index = m_next_pass_index++ };
        (void)m_pass_nodes.insert(node_handle,
                                  PassNode {
                                      .handle = node_handle,
                                      .pass   = pass_handle,
                                      .type   = type,
                                      .name   = String::make(name),
                                  });

        RenderGraphBuilder builder(*this, node_handle);
        Desc               desc {};
        callback(builder, desc);

        auto pass = Box<TPass>::make(rstd::move(desc));
        auto owned =
            Box<dyn<PassObject>>::from_raw(dyn<PassObject>::from_ptr(rstd::move(pass).into_raw()));
        (void)m_passes.insert(pass_handle, rstd::move(owned));
        return node_handle;
    }

private:
    friend struct RenderGraphBuilder;

    using PassNodeMap   = HashMap<NodeHandle, PassNode>;
    using TexNodeMap    = HashMap<NodeHandle, TexNode>;
    using PassMap       = HashMap<PassHandle, Box<dyn<PassObject>>>;
    using TextureKeyMap = HashMap<String, NodeHandle>;

    auto getPassNode(NodeHandle) -> Option<PassNode&>;
    auto getPassNode(NodeHandle) const -> Option<const PassNode&>;
    auto getTexNode(NodeHandle) -> Option<TexNode&>;
    auto getTexNode(NodeHandle) const -> Option<const TexNode&>;
    auto createTextureNode(const TextureDesc&, bool write) -> TextureNodeRef;
    auto createNewTextureNode(const TextureDesc&) -> TextureNodeRef;
    void connectTextureRead(TextureNodeRef, NodeHandle pass_node);
    void connectTextureWrite(TextureNodeRef, NodeHandle pass_node);
    auto textureHasWriter(TextureNodeRef) const -> bool;
    void reusePreviousAllocation(TextureNodeRef);
    auto isPassNode(NodeHandle) const -> bool;
    auto isVirtualPassNode(NodeHandle) const -> bool;
    auto isRenderPassNode(NodeHandle) const -> bool;
    auto passWriteTarget(NodeHandle) const -> Option<String>;

    usize           m_next_pass_index { 0 };
    u64             m_resource_generation { 0 };
    DependencyGraph m_dg;
    PassNodeMap     m_pass_nodes;
    TexNodeMap      m_tex_nodes;
    PassMap         m_passes;
    TextureKeyMap   m_key_texnode;
};

} // namespace vrento::rg
