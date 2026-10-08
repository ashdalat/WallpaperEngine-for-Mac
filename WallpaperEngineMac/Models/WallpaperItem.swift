import Foundation

struct WallpaperItem: Identifiable, Hashable {
    let id: String
    let folderURL: URL
    let title: String
    let type: WallpaperType
    let previewURL: URL?
    let contentURL: URL?
    let tags: [String]
    let contentRating: String
    let descriptionText: String
    let modified: Date
    let project: WEProject?

    static func == (lhs: WallpaperItem, rhs: WallpaperItem) -> Bool {
        lhs.id == rhs.id
    }

    func hash(into hasher: inout Hasher) {
        hasher.combine(id)
    }
}
