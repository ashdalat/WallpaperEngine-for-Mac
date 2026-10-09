import AppKit
import CoreGraphics
import Foundation

final class MouseEventTap {
    private var eventTap: CFMachPort?
    private var runLoopSource: CFRunLoopSource?
    var onEvent: ((CGEvent) -> Void)?

    private let mask: CGEventMask =
        (1 << CGEventType.mouseMoved.rawValue) |
        (1 << CGEventType.leftMouseDown.rawValue) |
        (1 << CGEventType.leftMouseUp.rawValue) |
        (1 << CGEventType.rightMouseDown.rawValue) |
        (1 << CGEventType.rightMouseUp.rawValue)

    @discardableResult
    func start() -> Bool {
        guard eventTap == nil else { return true }

        let callback: CGEventTapCallBack = { _, _, event, refcon in
            guard let refcon = refcon else { return Unmanaged.passRetained(event) }
            let tap = Unmanaged<MouseEventTap>.fromOpaque(refcon).takeUnretainedValue()
            tap.onEvent?(event)
            return Unmanaged.passRetained(event)
        }

        let refcon = Unmanaged.passUnretained(self).toOpaque()
        guard let tap = CGEvent.tapCreate(
            tap: .cghidEventTap,
            place: .headInsertEventTap,
            options: .listenOnly,
            eventsOfInterest: mask,
            callback: callback,
            userInfo: refcon
        ) else {
            return false
        }
        eventTap = tap
        runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0)
        if let source = runLoopSource {
            CFRunLoopAddSource(CFRunLoopGetMain(), source, .commonModes)
        }
        CGEvent.tapEnable(tap: tap, enable: true)
        return true
    }

    /// A cghid event tap only works with Accessibility permission.
    /// Ad-hoc signatures change on every rebuild, which resets the
    /// grant, so failures are common after an update — surface
    /// them instead of failing silently.
    func ensureAccessibility(prompt: Bool) {
        if eventTap != nil { return }
        if prompt {
            let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue(): true] as CFDictionary
            AXIsProcessTrustedWithOptions(options)
        }
        fputs("WEM: mouse event tap unavailable — grant Accessibility permission in System Settings > Privacy & Security > Accessibility\n", stderr)
    }

    func stop() {
        if let tap = eventTap {
            CGEvent.tapEnable(tap: tap, enable: false)
        }
        if let source = runLoopSource {
            CFRunLoopRemoveSource(CFRunLoopGetMain(), source, .commonModes)
        }
        runLoopSource = nil
        eventTap = nil
    }
}
