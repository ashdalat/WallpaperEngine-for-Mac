export module vrento.resource:buffer;
import rstd;
import :handle;

export namespace vrento::resource
{

using namespace rstd::prelude;

struct BufferDefinitionId {
    u32 index { u32::MAX };
    u64 generation {};

    bool Valid() const noexcept { return index != u32::MAX && generation != u64(); }

    friend bool operator==(const BufferDefinitionId&, const BufferDefinitionId&) = default;
};

enum class BufferUsage
{
    Vertex,
    Index,
    Uniform,
    Storage,
    Transfer,
};

enum class BufferLifetimeClass
{
    FrameLocal,
    Retained,
    Dynamic,
};

struct BufferDefinition {
    usize       size { 0 };
    BufferUsage usage { BufferUsage::Vertex };
    usize       alignment { 1 };

    friend bool operator==(const BufferDefinition&, const BufferDefinition&) = default;
};

struct BufferRequest {
    String                     name;
    Option<BufferDefinitionId> source;
    BufferDefinition           definition;
    BufferLifetimeClass        lifetime { BufferLifetimeClass::Retained };
    u64                        content_version { 1 };
    // Versions are local to a content source; replacing it must invalidate uploads.
    u64 content_identity {};

    auto clone() const -> BufferRequest {
        return BufferRequest {
            .name             = name.clone(),
            .source           = source,
            .definition       = definition,
            .lifetime         = lifetime,
            .content_version  = content_version,
            .content_identity = content_identity,
        };
    }
};

} // namespace vrento::resource
