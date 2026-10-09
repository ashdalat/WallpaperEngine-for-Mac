import AppKit
import CoreGraphics
import Foundation

/// Debug counter: how many events the tap
/// callback has seen. Proves whether the
/// window server delivers events to the
/// tap at all — if this stays 0 while the
/// tap exists, the tap is dead.
private var tapCallbackCount = 0

final class MouseEventTap {
    private var eventTap: CFMachPort?
    private var runLoopSource: CFRunLoopSource?
    var onEvent: ((CGEvent) -> Void)?

    private let mask: CGEventMask =
        (1 << CGEventType.mouseMoved.rawValue) |
        (1 << CGEventType.leftMouseDown.rawValue) |
        (1 << CGEventType.rightMouseDown.rawValue) |
        (1 << CGEventType.leftMouseUp.rawValue) |
        (1 << CGEventType.rightMouseUp.rawValue)

    @discardableResult
    func start() -> Bool {
        // A cghidEventTap can be created even when this
        // process lacks the Accessibility grant — the
        // window server then keeps the tap but never
        // delivers a single event to it, so the failure
        // is silent. Check the trust first, and tear down
        // any tap created before the grant was lost (each
        // rebuild changes the ad-hoc signature and resets
        // it), so the caller can prompt and retry once
        // the permission is granted.
        guard AXIsProcessTrusted() else {
            stop()
            return false
        }
        guard eventTap == nil else { return true }

        let callback: CGEventTapCallBack = { _, type, event, refcon in
            if tapCallbackCount < 10 {
                tapCallbackCount += 1
                fputs("WEM: tap callback fired type=\(type.rawValue)\n", stderr)
            }
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
