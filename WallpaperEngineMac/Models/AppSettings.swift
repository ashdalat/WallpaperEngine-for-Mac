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

@MainActor
final class AppSettings: ObservableObject {
    @AppStorage("steamPath") var steamPath: String = ""
    @AppStorage("pauseMode") var pauseMode: Int = PauseMode.focusOrMax.rawValue
    @AppStorage("displayMode") var displayMode: Int = DisplayMode.crop.rawValue
    @AppStorage("volume") var volume: Double = 0.5
    @AppStorage("muteAudio") var muteAudio: Bool = true
    @AppStorage("mouseInput") var mouseInput: Bool = false
    @AppStorage("pauseOnBattery") var pauseOnBattery: Bool = false
    @AppStorage("pauseBatteryPercent") var pauseBatteryPercent: Int = 20
    @AppStorage("currentWallpaperId") var currentWallpaperId: String = ""
}
