import AppKit
import Foundation

// Manual entry point: with no MainMenu.nib in the bundle, @main does not
// connect the AppDelegate, so wire it up here before running the event loop.
// GUI-launched apps have nowhere to write stderr, so the debug output goes
// to a log file — every fputs(..., stderr) in the app lands there. It is
// truncated once it grows past ~2 MB.
let logDirectory = FileManager.default
    .urls(for: .libraryDirectory, in: .userDomainMask)[0]
    .appendingPathComponent("Logs")
try? FileManager.default.createDirectory(
    at: logDirectory, withIntermediateDirectories: true)
let logFile = logDirectory.appendingPathComponent("WallpaperEngineMac.log")
let mode: String = {
    let size = (try? logFile.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0
    return size > 2_000_000 ? "w" : "a"
}()
freopen(logFile.path, mode, stderr)

MainActor.assumeIsolated {
    let app = NSApplication.shared
    let delegate = AppDelegate()
    app.delegate = delegate
    app.run()
}
