module;

export module vrento.shader_compile;
export import vvk;
export import vrento.shader_types;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::collections::BTreeMap;

export namespace vrento::vulkan
{

// ---------- Spv.hpp ----------

struct ShaderSpv {
    String     entry_point { "main"_Str };
    ShaderType stage;

    ShaderCode spirv;

    auto clone() const -> ShaderSpv {
        return ShaderSpv { entry_point.clone(), stage, spirv.clone() };
    }
    void clone_from(const ShaderSpv& other) { *this = other.clone(); }
};

using Uni_ShaderSpv = Box<ShaderSpv>;

// ---------- ShaderReflect.hpp ----------

struct ShaderReflected {
    struct BlockedUniform {
        int               block_index;
        unsigned          offset;
        usize             size { 0 };
        usize             num { 1 };
        ShaderScalarKind  scalar_kind { ShaderScalarKind::Unknown };
        unsigned          scalar_width {};
        unsigned          vector_components { 1 };
        unsigned          matrix_rows {};
        unsigned          matrix_columns {};
        unsigned          matrix_stride {};
        ShaderMatrixMajor matrix_major { ShaderMatrixMajor::None };
        unsigned          array_stride {};
        Vec<u32>          array_dimensions;
    };
    struct Block {
        int      index;
        unsigned size;
        String   name;
        unsigned set { 0 };
        unsigned binding { 0 };

        BTreeMap<String, BlockedUniform> member_map;
    };
    Vec<Block> blocks;

    struct Binding {
        unsigned                     set { 0 };
        VkDescriptorSetLayoutBinding layout;
    };
    BTreeMap<String, Binding> binding_map;

    struct Input {
        unsigned location;
        VkFormat format;
    };
    BTreeMap<String, Input> input_location_map;
};

// ---------- ShaderComp.hpp ----------

enum class VulkanTarget : unsigned
{
    Vulkan_1_0,
    Vulkan_1_1,
    Vulkan_1_2,
    Vulkan_1_3,
};

enum class SourceLang : unsigned
{
    Glsl,
    Hlsl,
};

struct ShaderCompUnit {
    ShaderType stage;
    String     src;
    String     entry_point; // if empty, "main" (GLSL) or "main_<stage>" (HLSL).
    SourceLang lang { SourceLang::Glsl };
};

struct ShaderCompOpt {
    VulkanTarget target { VulkanTarget::Vulkan_1_1 };
    bool         optimize { false };
};

struct ShaderBackend {
    using Trait                  = ShaderBackend;
    static constexpr bool direct = false;

    template<typename Self, typename = void>
    struct Api {
        using Trait = ShaderBackend;

        // On failure, callers discard the output arguments.
        bool Preprocess(ref<str> src, ShaderType stage, SourceLang lang, String& out) const {
            return rstd::trait_call<0>(this, src, stage, lang, out);
        }

        bool CompileAndLinkShaderUnits(slice<ShaderCompUnit> units, const ShaderCompOpt& opt,
                                       Vec<Uni_ShaderSpv>& spvs) const {
            return rstd::trait_call<1>(this, units, opt, spvs);
        }

        bool GenReflect(slice<ShaderCode> codes, Vec<Uni_ShaderSpv>& spvs,
                        ShaderReflected& reflected) const {
            return rstd::trait_call<2>(this, codes, spvs, reflected);
        }
    };

    template<typename T>
    using Funcs = rstd::TraitFuncs<&T::Preprocess, &T::CompileAndLinkShaderUnits, &T::GenReflect>;
};

} // namespace vrento::vulkan
