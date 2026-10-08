import Foundation

enum SteamPathManager {
    static let workshopAppId = "431960"

    static func candidateSteamDirectories() -> [URL] {
        let fm = FileManager.default
        let home = fm.homeDirectoryForCurrentUser
        var candidates: [URL] = []
        if let appSupport = fm.urls(for: .applicationSupportDirectory, in: .userDomainMask).first {
            candidates.append(appSupport.appendingPathComponent("Steam"))
        }
        candidates.append(contentsOf: [
            home.appendingPathComponent("Games/Steam"),
            home.appendingPathComponent("Steam"),
            home.appendingPathComponent("SteamLibrary"),
            URL(fileURLWithPath: "/Library/Application Support/Steam")
        ])
        var seen = Set<String>()
        return candidates.filter { url in
            let isDir = (try? url.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) ?? false
            guard isDir, seen.insert(url.path).inserted else { return false }
            return true
        }
    }

    static func steamappsDirectories(steamRoot: URL) -> [URL] {
        var dirs = [steamRoot.appendingPathComponent("steamapps")]
        let vdf = steamRoot.appendingPathComponent("steamapps/libraryfolders.vdf")
        if let extra = parseLibraryFoldersVDF(vdf) {
            dirs.append(contentsOf: extra)
        }
        return dirs
    }

    static func parseLibraryFoldersVDF(_ url: URL) -> [URL]? {
        guard let text = try? String(contentsOf: url, encoding: .utf8) else { return nil }
        var result: [URL] = []
        let pattern = "\"path\"\\s+\"([^\"]+)\""
        if let regex = try? NSRegularExpression(pattern: pattern) {
            let matches = regex.matches(in: text, range: NSRange(text.startIndex..., in: text))
            for match in matches {
                if let range = Range(match.range(at: 1), in: text) {
                    let path = String(text[range])
                    let steamapps = path.hasSuffix("steamapps")
                        ? URL(fileURLWithPath: path)
                        : URL(fileURLWithPath: path).appendingPathComponent("steamapps")
                    result.append(steamapps)
                }
            }
        }
        return result
    }

    static func workshopContentDirectories(steamRoots: [URL]) -> [URL] {
        steamRoots
            .flatMap { steamappsDirectories(steamRoot: $0) }
            .map { $0.appendingPathComponent("workshop/content/\(workshopAppId)") }
            .filter { FileManager.default.fileExists(atPath: $0.path) }
    }

    /// Wallpaper Engine's shared assets directories
    /// (steamapps/common/wallpaper_engine/assets) — scene wallpapers
    /// reference models and textures from here.
    static func wallpaperEngineAssetsDirectories(steamRoots: [URL]) -> [URL] {
        steamRoots
            .flatMap { steamappsDirectories(steamRoot: $0) }
            .map { $0.appendingPathComponent("common/wallpaper_engine/assets") }
            .filter { FileManager.default.fileExists(atPath: $0.path) }
    }

    static func defaultProjectsDirectories(steamRoots: [URL]) -> [URL] {
        steamRoots
            .flatMap { steamappsDirectories(steamRoot: $0) }
            .map { $0.appendingPathComponent("common/wallpaper_engine/projects/defaultprojects") }
            .filter { FileManager.default.fileExists(atPath: $0.path) }
    }

    static func myProjectsDirectories(steamRoots: [URL]) -> [URL] {
        steamRoots
            .flatMap { steamappsDirectories(steamRoot: $0) }
            .map { $0.appendingPathComponent("common/wallpaper_engine/projects/myprojects") }
            .filter { FileManager.default.fileExists(atPath: $0.path) }
    }
}
