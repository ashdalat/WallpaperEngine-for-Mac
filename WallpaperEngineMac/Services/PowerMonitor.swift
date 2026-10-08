import Foundation
import IOKit.ps

final class PowerMonitor {
    private var runLoopSource: CFRunLoopSource?
    var onChange: ((Bool, Int) -> Void)?

    init() {
        start()
    }

    deinit {
        stop()
    }

    func start() {
        let context = Unmanaged.passUnretained(self).toOpaque()
        runLoopSource = IOPSNotificationCreateRunLoopSource({ context in
            guard let context = context else { return }
            let monitor = Unmanaged<PowerMonitor>.fromOpaque(context).takeUnretainedValue()
            monitor.poll()
        }, context)?.takeRetainedValue()
        if let source = runLoopSource {
            CFRunLoopAddSource(CFRunLoopGetMain(), source, .defaultMode)
        }
        poll()
    }

    func stop() {
        if let source = runLoopSource {
            CFRunLoopRemoveSource(CFRunLoopGetMain(), source, .defaultMode)
        }
        runLoopSource = nil
    }

    func poll() {
        let state = PowerMonitor.currentPowerState()
        onChange?(state.onBattery, state.percent)
    }

    static func currentPowerState() -> (onBattery: Bool, percent: Int) {
        let snapshot = IOPSCopyPowerSourcesInfo().takeRetainedValue()
        guard let sources = IOPSCopyPowerSourcesList(snapshot).takeRetainedValue() as? [CFTypeRef] else {
            return (false, 100)
        }
        var onBattery = false
        var percent = 100
        for source in sources {
            guard let desc = IOPSGetPowerSourceDescription(snapshot, source).takeUnretainedValue() as? [String: Any] else {
                continue
            }
            if let type = desc[kIOPSTypeKey] as? String, type == kIOPSInternalBatteryType {
                if let state = desc[kIOPSPowerSourceStateKey] as? String, state == kIOPSBatteryPowerValue {
                    onBattery = true
                }
                if let p = desc[kIOPSCurrentCapacityKey] as? Int {
                    percent = p
                }
            }
        }
        return (onBattery, percent)
    }
}
