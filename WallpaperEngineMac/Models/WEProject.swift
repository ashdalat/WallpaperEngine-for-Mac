import Foundation

struct WEProject: Codable {
    let title: String?
    let description: String?
    let preview: String?
    let file: String?
    let type: String?
    let contentRating: String?
    let tags: [String]?
    let version: Int?
    let general: WEGeneral?
    let properties: [String: WEProperty]?

    enum CodingKeys: String, CodingKey {
        case title, description, preview, file, type, tags, version, general, properties
        case contentRating = "contentrating"
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        // Every field is decoded leniently: one
        // unconventional value in a project.json
        // must not reject the whole wallpaper.
        title = try? container.decode(String.self, forKey: .title)
        description = try? container.decode(String.self, forKey: .description)
        preview = try? container.decode(String.self, forKey: .preview)
        file = try? container.decode(String.self, forKey: .file)
        type = try? container.decode(String.self, forKey: .type)
        contentRating = try? container.decode(String.self, forKey: .contentRating)
        version = try? container.decode(Int.self, forKey: .version)
        general = try? container.decode(WEGeneral.self, forKey: .general)
        properties = try? container.decode([String: WEProperty].self, forKey: .properties)
        var tags: [String]?
        if let rawTags = try? container.decode([WEStringValue].self, forKey: .tags) {
            tags = rawTags.map { $0.string }
        }
        self.tags = tags
    }

    static func parse(data: Data) -> WEProject? {
        try? JSONDecoder().decode(WEProject.self, from: data)
    }

    /// The wallpaper's settings table. Scene
    /// wallpapers authored in Wallpaper Engine's
    /// scene editor keep their properties under
    /// "general.properties"; the classic format
    /// uses a top-level "properties" table. Both
    /// are honored, the top-level table winning
    /// on conflicts.
    var effectiveProperties: [String: WEProperty] {
        var merged = general?.properties ?? [:]
        if let topLevel = properties {
            for (key, property) in topLevel {
                merged[key] = property
            }
        }
        return merged
    }
}

/// Accepts any JSON scalar and keeps its string
/// form, so a single odd value cannot fail a decode.
struct WEStringValue: Decodable {
    let string: String

    init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        if let string = try? container.decode(String.self) {
            self.string = string
        } else if let bool = try? container.decode(Bool.self) {
            self.string = bool ? "true" : "false"
        } else if let int = try? container.decode(Int.self) {
            self.string = String(int)
        } else if let double = try? container.decode(Double.self) {
            self.string = String(double)
        } else {
            self.string = ""
        }
    }
}

struct WEGeneral: Codable {
    let properties: [String: WEProperty]?
}

/// Which control a project.json property maps to.
/// Wallpaper Engine authors properties in project.json;
/// the control kind decides how the settings sheet
/// renders them and how the value travels to the
/// renderer (uniform update, texture swap, JS call).
enum WEPropertyControl: String {
    case checkbox
    case slider
    case color
    case text
    case combo
    case button
    case texture
    case group
    case unsupported
}

struct WEProperty: Codable {
    let order: Int?
    let text: String?
    let type: String?
    let value: String?
    // Slider range/step.
    let min: Double?
    let max: Double?
    let step: Double?
    // Combo box entries.
    let options: [WEComboOption]?
    // Conditional visibility, e.g.
    // "otherProperty.value==true".
    let condition: String?

    enum CodingKeys: String, CodingKey {
        case order, text, type, value, min, max, step, options, condition
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        order = try? container.decode(Int.self, forKey: .order)
        text = try? container.decode(String.self, forKey: .text)
        type = try? container.decode(String.self, forKey: .type)
        condition = try? container.decode(String.self, forKey: .condition)
        // Wallpaper Engine writes property values as
        // strings, but checkbox properties are bools
        // and slider properties are numbers. Stringify
        // them (consumers, e.g. the web view, treat
        // values as strings) instead of rejecting the
        // whole project.json.
        if let string = try? container.decode(String.self, forKey: .value) {
            value = string
        } else if let bool = try? container.decode(Bool.self, forKey: .value) {
            value = bool ? "true" : "false"
        } else if let int = try? container.decode(Int.self, forKey: .value) {
            value = String(int)
        } else if let double = try? container.decode(Double.self, forKey: .value) {
            value = String(double)
        } else {
            value = nil
        }
        min = try? container.decode(Double.self, forKey: .min)
        max = try? container.decode(Double.self, forKey: .max)
        step = try? container.decode(Double.self, forKey: .step)
        options = try? container.decode([WEComboOption].self, forKey: .options)
    }

    var control: WEPropertyControl {
        switch type?.lowercased() {
        case "checkbox", "bool": return .checkbox
        case "slider": return .slider
        case "color": return .color
        case "textinput", "text": return .text
        case "combo": return .combo
        case "button": return .button
        case "texture", "replacetexture", "scenetexture", "file": return .texture
        case "group": return .group
        default:
            // The scene editor's format also has
            // entries without a type: they carry
            // author notes (HTML in "text"), not
            // a control.
            return type == nil ? .group : .unsupported
        }
    }

    var label: String {
        let text = self.text?.isEmpty == false ? self.text! : (type ?? "Property")
        return stripHTML(text)
    }

    /// The property's text with HTML markup
    /// removed — scene-editor labels carry
    /// <br>, <img>, <a> and friends.
    var plainText: String {
        let text = self.text?.isEmpty == false ? self.text! : label
        return stripHTML(text)
    }
}

/// Property labels and notes in the scene
/// editor's format carry HTML markup; keep
/// the plain text.
func stripHTML(_ text: String) -> String {
    var result = text
    result = result.replacingOccurrences(
        of: "<img[^>]*>", with: "", options: .regularExpression)
    result = result.replacingOccurrences(
        of: "<br\\s*/?>", with: "\n", options: .regularExpression)
    result = result.replacingOccurrences(
        of: "</(p|div|center|h[1-6])>", with: "\n",
        options: .regularExpression)
    result = result.replacingOccurrences(
        of: "<[^>]+>", with: "", options: .regularExpression)
    let entities: [String: String] = [
        "&amp;": "&", "&lt;": "<", "&gt;": ">",
        "&quot;": "\"", "&#39;": "'", "&nbsp;": " "
    ]
    for (entity, character) in entities {
        result = result.replacingOccurrences(of: entity, with: character)
    }
    let lines = result.split(separator: "\n")
        .map { $0.trimmingCharacters(in: .whitespaces) }
        .filter { !$0.isEmpty }
    return lines.joined(separator: "\n")
}

/// One entry of a combo property's "options" array.
/// The scene editor writes the visible text under
/// "label"; the classic format uses "text".
struct WEComboOption: Codable {
    let label: String?
    let text: String?
    let value: String?

    enum CodingKeys: String, CodingKey {
        case label, text, value
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        label = try? container.decode(String.self, forKey: .label)
        text = try? container.decode(String.self, forKey: .text)
        if let string = try? container.decode(String.self, forKey: .value) {
            value = string
        } else if let bool = try? container.decode(Bool.self, forKey: .value) {
            value = bool ? "true" : "false"
        } else if let int = try? container.decode(Int.self, forKey: .value) {
            value = String(int)
        } else if let double = try? container.decode(Double.self, forKey: .value) {
            value = String(double)
        } else {
            value = nil
        }
    }

    var displayText: String {
        label ?? text ?? value ?? ""
    }
}
