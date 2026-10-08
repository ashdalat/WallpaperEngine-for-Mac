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

struct WEProperty: Codable {
    let order: Int?
    let text: String?
    let type: String?
    let value: String?

    enum CodingKeys: String, CodingKey {
        case order, text, type, value
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        order = try? container.decode(Int.self, forKey: .order)
        text = try? container.decode(String.self, forKey: .text)
        type = try? container.decode(String.self, forKey: .type)
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
    }
}
