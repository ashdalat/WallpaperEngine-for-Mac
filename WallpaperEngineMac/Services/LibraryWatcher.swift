import Foundation

/// Watches the directories that hold wallpapers and
/// fires a debounced callback when their contents
/// change — ported from Waywallen's "rescan when a
/// library directory changes on disk": a Workshop
/// item the Steam client finished downloading shows
/// up without a manual refresh. Waywallen stats the
/// directories once a minute; on macOS the vnode
/// event source does it without the polling.
@MainActor
final class LibraryWatcher {
    /// Fired on the main queue after the last event
    /// in a burst settles. The observer should
    /// rescan and then call watch() again with the
    /// fresh directory list — Steam may have moved
    /// the watched directories around, and the old
    /// watchers are torn down when that happens.
    var onChange: (() -> Void)?

    private var sources: [DispatchSourceFileSystemObject] = []
    private var debounce: DispatchWorkItem?
    private var directories: [URL] = []

    /// Steam writes dozens of files per item
    /// (project.json, scene.pkg, previews...), each
    /// one a directory change — wait out the burst.
    private static let debounceInterval: TimeInterval = 5

    func watch(_ directories: [URL]) {
        sources.forEach { $0.cancel() }
        sources.removeAll()
        self.directories = directories

        for directory in directories {
            let fd = open(directory.path, O_EVTONLY)
            guard fd >= 0 else { continue }
            let source = DispatchSource.makeFileSystemObjectSource(
                fileDescriptor: fd,
                eventMask: [.write, .delete, .rename, .link],
                queue: .main)
            source.setEventHandler { [weak self] in
                self?.directoryChanged(source)
            }
            source.setCancelHandler { close(fd) }
            source.resume()
            sources.append(source)
        }
    }

    private func directoryChanged(_ source: DispatchSourceFileSystemObject) {
        // A delete/rename of the watched directory
        // itself (Steam moving things around) leaves
        // every fd stale — drop them all and let the
        // observer re-establish the watch set.
        if source.data.contains([.delete, .rename]) {
            sources.forEach { $0.cancel() }
            sources.removeAll()
        }
        debounce?.cancel()
        let work = DispatchWorkItem { [weak self] in
            self?.onChange?()
        }
        debounce = work
        DispatchQueue.main.asyncAfter(
            deadline: .now() + Self.debounceInterval, execute: work)
    }
}
