export module wescene.scene:visibility;
import rstd;
import wescene.json;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace owe
{

struct SceneUserVisibilityBinding {
    String key;
    Json   condition;
    bool   has_condition { false };

    bool empty() const { return key.is_empty(); }
};

inline const Json& SceneUserPropertyPayload(const Json& property) {
    if (auto value = property.get("value"_str); value.is_some()) return **value;
    return property;
}

inline auto SceneJsonScalarString(const Json& value) -> Option<String> {
    if (value.is_string()) return Some(String::make(*value.as_str()));
    if (value.is_boolean()) return Some(String::make(*value.as_bool() ? "true"_str : "false"_str));
    if (value.is_number()) return Some(DumpString(value));
    return None();
}

inline bool SceneJsonScalarEquals(const Json& a, const Json& b) {
    if (a == b) return true;
    auto as = SceneJsonScalarString(a);
    auto bs = SceneJsonScalarString(b);
    if (! as || ! bs) return false;
    if (as->as_str() == bs->as_str()) return true;
    if (a.is_boolean() && b.is_string()) {
        auto s = *b.as_str();
        return (*a.as_bool() && s == "1"_str) || (! *a.as_bool() && s == "0"_str);
    }
    if (a.is_string() && b.is_boolean()) {
        auto s = *a.as_str();
        return (*b.as_bool() && s == "1"_str) || (! *b.as_bool() && s == "0"_str);
    }
    return false;
}

inline Option<bool> ResolveSceneUserVisibilityBinding(const SceneUserVisibilityBinding& binding,
                                                      const Json&                       property) {
    if (binding.empty()) return None();
    const auto& value = SceneUserPropertyPayload(property);
    if (binding.has_condition) return Some(SceneJsonScalarEquals(value, binding.condition));
    return value.as_bool();
}

inline Option<bool> ResolveSceneUserVisibilityBinding(const SceneUserVisibilityBinding& binding,
                                                      ref<str> key, const Json& property) {
    if (binding.key.as_str() != key) return None();
    return ResolveSceneUserVisibilityBinding(binding, property);
}

} // namespace owe
