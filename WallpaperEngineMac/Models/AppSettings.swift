import AVFoundation
import Foundation
import SwiftUI

enum PauseMode: Int, CaseIterable, Identifiable {
    case never = 0
    case any
    case max
    case focus
    case focusOrMax
    case fullScreen

    var id: Int { rawValue }

    var displayName: String {
        switch self {
        case .never: return "Never"
        case .any: return "Any Window"
        case .max: return "Maximized Window"
        case .focus: return "Focus Window"
        case .focusOrMax: return "Focus or Maximized"
        case .fullScreen: return "Full Screen"
        }
    }
}

enum DisplayMode: Int, CaseIterable, Identifiable {
    case aspect = 0
    case crop
    case scale

    var id: Int { rawValue }

    var displayName: String {
        switch self {
        case .aspect: return "Keep Aspect Ratio"
        case .crop: return "Scale and Crop"
        case .scale: return "Scale to Fill"
        }
    }

    var videoGravity: AVLayerVideoGravity {
        switch self {
        case .aspect: return .resizeAspect
        case .crop: return .resizeAspectFill
        case .scale: return .resize
        }
    }

    /// wallpaper::FillMode used by the scene renderer:
    /// 0 = stretch, 1 = aspect fit, 2 = aspect crop.
    var fillModeRawValue: Int {
        switch self {
        case .aspect: return 1
        case .crop: return 2
        case .scale: return 0
        }
    }
}

enum DisplayRotation: Int, CaseIterable, Identifiable {
    case normal = 0
    case cw90
    case cw180
    case cw270

    var id: Int { rawValue }

    var displayName: String {
        switch self {
        case .normal: return "Normal"
        case .cw90: return "90° Clockwise"
        case .cw180: return "180°"
        case .cw270: return "270° Clockwise"
        }
    }

    /// Clockwise rotation, in radians. CoreGraphics
    /// measures positive angles counter-clockwise.
    var angle: CGFloat {
        switch self {
        case .normal: return 0
        case .cw90: return -.pi / 2
        case .cw180: return .pi
        case .cw270: return .pi / 2
        }
    }
}

enum DisplayFlip: Int, CaseIterable, Identifiable {
    case none = 0
    case horizontal
    case vertical
    case both

    var id: Int { rawValue }

    var displayName: String {
        switch self {
        case .none: return "None"
        case .horizontal: return "Horizontal"
        case .vertical: return "Vertical"
        case .both: return "Both"
        }
    }

    var scaleX: CGFloat {
        switch self {
        case .horizontal, .both: return -1
        default: return 1
        }
    }

    var scaleY: CGFloat {
        switch self {
        case .vertical, .both: return -1
        default: return 1
        }
    }
}

@MainActor
final class AppSettings: ObservableObject {
    @AppStorage("steamPath") var steamPath: String = ""
    @AppStorage("pauseMode") var pauseMode: Int = PauseMode.focusOrMax.rawValue
    @AppStorage("displayMode") var displayMode: Int = DisplayMode.crop.rawValue
    @AppStorage("rotation") var rotation: Int = DisplayRotation.normal.rawValue
    @AppStorage("flip") var flip: Int = DisplayFlip.none.rawValue
    /// Waywallen "Location" — position offset
    /// in percent (0...100, 50 = centered).
    /// Only shifts the content when rotation
    /// leaves room on screen.
    @AppStorage("offsetX") var offsetX: Double = 50
    @AppStorage("offsetY") var offsetY: Double = 50
    @AppStorage("volume") var volume: Double = 0.5
    // Wallpaper Engine plays wallpaper audio by
    // default; muting by default made every
    // wallpaper silently "not play audio".
    @AppStorage("muteAudio") var muteAudio: Bool = false
    @AppStorage("mouseInput") var mouseInput: Bool = false
    @AppStorage("pauseOnBattery") var pauseOnBattery: Bool = false
    @AppStorage("pauseBatteryPercent") var pauseBatteryPercent: Int = 20
    @AppStorage("currentWallpaperId") var currentWallpaperId: String = ""
    /// Launch the app automatically at
    /// login (SMAppService.mainApp).
    @AppStorage("launchAtLogin") var launchAtLogin: Bool = true
}
