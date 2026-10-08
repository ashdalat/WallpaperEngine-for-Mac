import Foundation

@MainActor
final class WallpaperLibrary: ObservableObject {
    @Published var items: [WallpaperItem] = []
    @Published var isScanning = false
    @Published var searchText = ""
    @Published var enabledTypes: Set<WallpaperType> = [.video, .web]

    private var steamRoots: [URL] = []

    var filteredItems: [WallpaperItem] {
        items.filter { item in
            enabledTypes.contains(item.type) &&
            (searchText.isEmpty || item.title.localizedCaseInsensitiveContains(searchText))
        }
    }

    func setSteamRoots(_ roots: [URL]) {
        steamRoots = roots
    }

    func scan() async {
        isScanning = true
        let roots = steamRoots
        let scanned = await Task.detached(priority: .userInitiated) { () -> [WallpaperItem] in
            var items: [WallpaperItem] = []
            let dirs = SteamPathManager.workshopContentDirectories(steamRoots: roots)
                + SteamPathManager.defaultProjectsDirectories(steamRoots: roots)
                + SteamPathManager.myProjectsDirectories(steamRoots: roots)
            for dir in dirs {
                items.append(contentsOf: Self.scanDirectory(dir))
            }
            return items
        }.value
        var seen = Set<String>()
        let unique = scanned.filter { seen.insert($0.id).inserted }
        items = unique.sorted { $0.modified > $1.modified }
        isScanning = false
    }

    nonisolated static func scanDirectory(_ dir: URL) -> [WallpaperItem] {
        let fm = FileManager.default
        guard let entries = try? fm.contentsOfDirectory(
            at: dir,
            includingPropertiesForKeys: [.isDirectoryKey, .contentModificationDateKey]
        ) else {
            return []
        }
        var items: [WallpaperItem] = []
        for entry in entries {
            let isDir = (try? entry.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) ?? false
            guard isDir else { continue }
            if let item = parseWallpaperFolder(entry) {
                items.append(item)
            }
        }
        return items
    }

    nonisolated static func parseWallpaperFolder(_ folder: URL) -> WallpaperItem? {
        let projectURL = folder.appendingPathComponent("project.json")
        guard let data = try? Data(contentsOf: projectURL),
              let project = WEProject.parse(data: data) else { return nil }

        let id = folder.lastPathComponent
        let type = WallpaperType(weType: project.type)

        var previewURL: URL?
        if let preview = project.preview, !preview.isEmpty {
            let url = folder.appendingPathComponent(preview)
            if FileManager.default.fileExists(atPath: url.path) { previewURL = url }
        }
        if previewURL == nil {
            let fallback = folder.appendingPathComponent("preview.jpg")
            if FileManager.default.fileExists(atPath: fallback.path) { previewURL = fallback }
        }

        var contentURL: URL?
        if let file = project.file, !file.isEmpty {
            let url = folder.appendingPathComponent(file)
            if FileManager.default.fileExists(atPath: url.path) {
                contentURL = url
            } else {
                // Scene wallpapers store scene.json inside scene.pkg;
                // the engine derives the .pkg path from the source path,
                // so the .json itself does not need to exist on disk.
                let pkgURL = url.deletingPathExtension().appendingPathExtension("pkg")
                if FileManager.default.fileExists(atPath: pkgURL.path) {
                    contentURL = url
                }
            }
        }

        let modified = (try? folder.resourceValues(forKeys: [.contentModificationDateKey])
            .contentModificationDate) ?? Date()

        return WallpaperItem(
            id: id,
            folderURL: folder,
            title: project.title ?? "Unknown",
            type: type,
            previewURL: previewURL,
            contentURL: contentURL,
            tags: project.tags ?? [],
            contentRating: project.contentRating ?? "Everyone",
            descriptionText: project.description ?? "",
            modified: modified,
            project: project
        )
    }
}
