module;
#include <rstd/macro.hpp>

export module vrento.scene_identity;
import rstd;

using namespace rstd::prelude;
using rstd::collections::HashMap;

export namespace vrento
{
struct RenderItemId {
    u32         index { u32::MAX };
    u64         generation {};
    bool        Valid() const noexcept { return index != u32::MAX && generation != u64(); }
    friend bool operator==(const RenderItemId&, const RenderItemId&) = default;
};

template<typename Tag>
struct SceneId {
    u32         index { u32::MAX };
    u32         generation {};
    bool        Valid() const noexcept { return index != u32::MAX && generation != u32(); }
    friend bool operator==(const SceneId&, const SceneId&) = default;
};

struct NodeIdTag;
struct GeometryIdTag;
struct MaterialIdTag;
struct DrawIdTag;
struct TextureIdTag;
struct RenderTargetIdTag;
struct CameraIdTag;
using NodeId         = SceneId<NodeIdTag>;
using GeometryId     = SceneId<GeometryIdTag>;
using MaterialId     = SceneId<MaterialIdTag>;
using DrawId         = SceneId<DrawIdTag>;
using TextureId      = SceneId<TextureIdTag>;
using RenderTargetId = SceneId<RenderTargetIdTag>;
using CameraId       = SceneId<CameraIdTag>;

// The scene supplies a unique nonzero generation. Removed slots are not reused.
template<typename Id>
class NamedIdentityIndex {
public:
    void Rebuild(slice<String> names, u32 generation) {
        rstd_assert(generation != u32());
        if (generation != m_generation) {
            m_keys.clear();
            m_ids.clear();
            m_generation = generation;
        }
        m_present.clear();
        m_present.resize(m_keys.len(), u8());
        for (const auto& name : names) {
            auto found = m_ids.get(name.as_str());
            if (found.is_some()) {
                m_present[usize((**found).index.to_primitive())] = u8(1);
                continue;
            }
            rstd_assert(m_keys.len() < usize(u32::MAX.to_primitive()));
            Id id { .index = rstd::as_cast<u32>(m_keys.len()), .generation = generation };
            m_keys.push(name.clone());
            m_present.push(u8(1));
            (void)m_ids.insert(name.clone(), rstd::move(id));
        }
        m_ids.retain([this](const String&, Id& id) {
            return m_present[usize(id.index.to_primitive())] != u8();
        });
        for (usize index {}; index < m_keys.len(); ++index)
            if (m_present[index] == u8()) m_keys[index] = String {};
    }

    auto Find(ref<str> name) const -> Option<Id> {
        auto found = m_ids.get(name);
        return found.is_some() ? Some(Id(**found)) : None<Id>();
    }
    auto Resolve(Id id) const -> Option<ref<str>> {
        if (! id.Valid() || id.generation != m_generation) return None();
        auto index = usize(id.index.to_primitive());
        if (index >= m_keys.len() || m_present[index] == u8()) return None();
        return Some(m_keys[index].as_str());
    }

private:
    u32                 m_generation {};
    Vec<String>         m_keys;
    Vec<u8>             m_present;
    HashMap<String, Id> m_ids;
};
} // namespace vrento
