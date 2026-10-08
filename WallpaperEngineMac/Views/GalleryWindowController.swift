import AppKit
import SwiftUI

@MainActor
final class GalleryWindowController: NSWindowController {
    init(state: AppState) {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 1040, height: 720),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "Wallpaper Engine — Gallery"
        window.minSize = NSSize(width: 720, height: 480)
        window.isReleasedWhenClosed = false
        let hosting = NSHostingController(rootView: GalleryView().environmentObject(state))
        window.contentViewController = hosting
        super.init(window: window)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }
}
