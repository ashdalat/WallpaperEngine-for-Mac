import Foundation

enum WallpaperType: String, CaseIterable {
    case video
    case web
    case scene
    case unknown

    init(weType: String?) {
        switch weType?.lowercased() {
        case "video": self = .video
        case "web": self = .web
        case "scene": self = .scene
        default: self = .unknown
        }
    }

    var displayName: String {
        switch self {
        case .video: return "Video"
        case .web: return "Web"
        case .scene: return "Scene"
        case .unknown: return "Unknown"
        }
    }
}
