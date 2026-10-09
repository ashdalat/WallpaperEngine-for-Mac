export module vrento.draw_buffer;
export import vrento.geometry;
export import vrento.scene_identity;
export import vrento.resource;
import vrento.resource_registry;
import rstd;

using namespace rstd::prelude;

export namespace vrento
{
enum class DrawBufferRole
{
    Uniform,
    Vertex,
    Index
};

struct DrawBufferKey {
    RenderItemId   render_item;
    DrawBufferRole role { DrawBufferRole::Vertex };
    u32            submesh_index {};
    u32            stream_index {};
    u64            data_generation {};
    u64            allocation_generation {};
    u64            storage_generation {};
    usize          capacity {};
    usize          stride {};
};

struct DrawBufferRefs {
    RenderItemId                      render_item;
    u64                               allocation_generation {};
    bool                              dynamic { false };
    bool                              content_confirmed { false };
    u32                               draw_count {};
    Vec<DrawBufferKey>                vertex_keys;
    Option<DrawBufferKey>             index_key;
    Vec<resource::BufferUseHandle>    vertices;
    Option<resource::BufferUseHandle> index;
    bool                              hasIndex() const { return index.is_some(); }
};

// The request and geometry bytes are borrowed only for the duration of each call.
struct DrawBufferRequest {
    RenderItemId                     render_item;
    const GeometryView&              geometry;
    u32                              submesh_index {};
    u64                              dynamic_allocation_generation {};
    slice<resource::BufferUseHandle> buffer_uses;
};

enum class DrawBufferUpdateErrorKind
{
    InvalidGeometry,
    NeedsReprepare,
    UploadFailed
};
struct DrawBufferUpdateError {
    DrawBufferUpdateErrorKind kind;
    String                    message;
};

String BuildDrawBufferResourceName(DrawId, DrawBufferRole, u32 stream_index = u32());
auto   BuildDrawBufferKeys(const DrawBufferRequest&, u64 allocation_generation = u64())
    -> Vec<DrawBufferKey>;

class RenderBufferResolver {
public:
    explicit RenderBufferResolver(const resource_registry::PreparedResourceTable& resources)
        : m_resources(resources) {}
    auto        prepareDrawBuffers(const DrawBufferRequest&) -> Option<DrawBufferRefs>;
    static auto updateDynamicDrawBuffers(const DrawBufferRequest&, DrawBufferRefs&,
                                         mut_ref<dyn<resource::BufferContentWriter>>)
        -> Result<empty, DrawBufferUpdateError>;

private:
    const resource_registry::PreparedResourceTable& m_resources;
};
} // namespace vrento
