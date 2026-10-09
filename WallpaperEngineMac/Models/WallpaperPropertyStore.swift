import Foundation

/// Per-wallpaper user property values (the settings a
/// wallpaper defines in its project.json "properties"
/// table). Wallpaper Engine keeps these per wallpaper;
/// here they live in UserDefaults keyed by wallpaper id,
/// so a wallpaper remembers its settings across launches.
///
/// Values are stored in project.json's wire form: strings
/// like "0.5" (slider), "true"/"false" (checkbox),
/// "0.2 0.4 0.9" (color). Renderers consume that form
/// directly — the scene engine takes it as a user
/// property update, the web view forwards it to
/// wallpaperPropertyListener.applyUserProperties().
/// Not actor-isolated: it is a thin UserDefaults
/// wrapper (thread-safe), and callers include
/// nonisolated view code.
final class WallpaperPropertyStore: ObservableObject {
    static let shared = WallpaperPropertyStore()

    private let defaults = UserDefaults.standard

    private init() {}

    /// The effective value of one property: the stored
    /// value if the user changed it, otherwise the
    /// value authored in project.json.
    func value(for item: WallpaperItem, key: String, property: WEProperty) -> String {
        defaults.string(forKey: storageKey(item: item, key: key))
            ?? property.value
            ?? fallbackValue(for: property)
    }

    func setValue(_ value: String, for item: WallpaperItem, key: String) {
        defaults.set(value, forKey: storageKey(item: item, key: key))
    }

    /// Every property of a wallpaper with its effective
    /// value — the state each renderer applies when the
    /// wallpaper loads.
    func values(for item: WallpaperItem) -> [String: String] {
        guard let properties = item.project?.properties else { return [:] }
        var values: [String: String] = [:]
        for (key, property) in properties {
            values[key] = value(for: item, key: key, property: property)
        }
        return values
    }

    /// Removes saved settings for a wallpaper (e.g. after
    /// the wallpaper is deleted from the library).
    func clearValues(for item: WallpaperItem) {
        guard let properties = item.project?.properties else { return }
        for key in properties.keys {
            defaults.removeObject(forKey: storageKey(item: item, key: key))
        }
    }

    private func storageKey(item: WallpaperItem, key: String) -> String {
        "wallpaperProperties.\(item.id).\(key)"
    }

    private func fallbackValue(for property: WEProperty) -> String {
        switch property.control {
        case .checkbox: return "false"
        case .color: return "0 0 0 1"
        case .combo: return property.options?.first?.value ?? ""
        default: return ""
        }
    }
}
