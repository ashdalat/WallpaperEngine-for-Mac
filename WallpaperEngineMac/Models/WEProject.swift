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

    static func parse(data: Data) -> WEProject? {
        try? JSONDecoder().decode(WEProject.self, from: data)
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
}
