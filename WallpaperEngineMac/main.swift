import AppKit

// Manual entry point: with no MainMenu.nib in the bundle, @main does not
// connect the AppDelegate, so wire it up here before running the event loop.
MainActor.assumeIsolated {
    let app = NSApplication.shared
    let delegate = AppDelegate()
    app.delegate = delegate
    app.run()
}
