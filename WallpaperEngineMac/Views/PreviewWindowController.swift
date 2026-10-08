import AppKit

@MainActor
final class PreviewWindowController: NSWindowController, NSWindowDelegate {
    var onClose: (() -> Void)?
    private let renderer = WallpaperRenderer()

    init(item: WallpaperItem, settings: AppSettings) {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 1280, height: 720),
            styleMask: [.titled, .closable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = item.title
        window.minSize = NSSize(width: 480, height: 270)
        window.isReleasedWhenClosed = false
        super.init(window: window)
        window.delegate = self
        renderer.load(item: item, in: window, settings: settings)
        window.center()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    func windowWillClose(_ notification: Notification) {
        renderer.unload()
        onClose?()
    }
}
