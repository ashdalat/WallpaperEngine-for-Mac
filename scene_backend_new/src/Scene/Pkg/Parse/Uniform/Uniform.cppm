export module wescene.pkg.parse:uniform;
import rstd;
import wescene.json;

using namespace rstd::prelude;
using rstd::collections::HashMap;

export namespace owe

{
namespace wpscene
{

// Texture-uniform metadata. WE's per-sampler `// {json}` annotation is parsed
// into this struct. Fields not used by the renderer (label / group / ...) are
// stored anyway so future editor / material UI consumers can read them back.
struct UniformTex {
    struct Component {
        String label;
        String combo;

        auto clone() const -> Component {
            return { .label = label.clone(), .combo = combo.clone() };
        }
    };
    String               material; // unique key for material override
    String               label;    // editor display name
    String               default_; // default texture path or `_rt_*`
    String               mode;     // opacitymask / rgbmask / flowmask
    String               combo;    // bound? `#define <IDENT> 1` else 0
    array<float, 4>      paintdefaultcolor { 0.0f, 0.0f, 0.0f, 1.0f };
    Vec<Component>       components;
    bool                 requireany { false };
    HashMap<String, i32> require;

    // Corpus-observed extras (parsed, not yet consumed).
    bool   hidden { false };
    bool   nonremovable { false };
    String group;
    bool   linked { false };
    String format; // "normalmap" etc.
    bool   formatcombo { false };
    bool   direction { false };
    String conversion; // "startdelta" etc.
    i32    order {};

    bool FromJson(const Json&);

    auto clone() const -> UniformTex {
        Vec<Component> cloned_components;
        cloned_components.reserve(components.len());
        for (const auto& component : components) cloned_components.push(component.clone());
        return {
            .material          = material.clone(),
            .label             = label.clone(),
            .default_          = default_.clone(),
            .mode              = mode.clone(),
            .combo             = combo.clone(),
            .paintdefaultcolor = paintdefaultcolor,
            .components        = rstd::move(cloned_components),
            .requireany        = requireany,
            .require           = require.clone(),
            .hidden            = hidden,
            .nonremovable      = nonremovable,
            .group             = group.clone(),
            .linked            = linked,
            .format            = format.clone(),
            .formatcombo       = formatcombo,
            .direction         = direction,
            .conversion        = conversion.clone(),
            .order             = order,
        };
    }
};

// Scalar / vec / color / UV uniform metadata. Covers `g_*` user-controlled
// scalars (e.g. `g_Brightness`) and `u_*` user-variable convention.
struct UniformVar {
    String          name;     // GLSL identifier (e.g. "g_Brightness")
    String          material; // UI key for editor / project bindings
    String          label;
    String          group;
    String          type; // "color" | "" (UV picker uses position:true)
    bool            position { false };
    bool            linked { false };
    bool            nobindings { false };
    bool            is_user { false }; // true iff name starts with "u_"
    array<float, 2> range { 0.0f, 1.0f };
    bool            has_range { false };

    // Default value as raw JSON. Host coerces to float / vec2 / vec3 / vec4
    // depending on uniform type at upload time.
    Json default_value;

    bool FromJson(const Json&, String uniform_name);

    auto clone() const -> UniformVar {
        return {
            .name          = name.clone(),
            .material      = material.clone(),
            .label         = label.clone(),
            .group         = group.clone(),
            .type          = type.clone(),
            .position      = position,
            .linked        = linked,
            .nobindings    = nobindings,
            .is_user       = is_user,
            .range         = range,
            .has_range     = has_range,
            .default_value = default_value.clone(),
        };
    }
};

// [COMBO] preprocessor switch declaration. `combo` is the IDENT that gets
// `#define`'d in the GLSL prologue, with value `default_` (or whichever
// option the user picked in the editor).
struct Combo {
    String               material; // editor display name
    String               combo;    // IDENT injected as #define
    String               type;     // "options" | "" (checkbox)
    i32                  default_ {};
    HashMap<String, i32> options; // label → value (combo box mode)
    HashMap<String, i32> require; // gating combos

    bool FromJson(const Json&);

    auto clone() const -> Combo {
        return {
            .material = material.clone(),
            .combo    = combo.clone(),
            .type     = type.clone(),
            .default_ = default_,
            .options  = options.clone(),
            .require  = require.clone(),
        };
    }
};

} // namespace wpscene
} // namespace owe
