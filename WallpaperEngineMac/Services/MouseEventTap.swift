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

    func start() {
        guard eventTap == nil else { return }

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
            return
        }
        eventTap = tap
        runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0)
        if let source = runLoopSource {
            CFRunLoopAddSource(CFRunLoopGetMain(), source, .commonModes)
        }
        CGEvent.tapEnable(tap: tap, enable: true)
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
