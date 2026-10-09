import AVFoundation
import CoreMedia

/// Which video codecs the wallpaper window
/// can render, plus the tools to detect and
/// convert the others.
enum VideoCodecSupport {
    /// Codecs that render reliably in an
    /// AVPlayerLayer on every supported Mac.
    /// AV1 ("av01") is the notable exclusion:
    /// it only decodes on Apple Silicon M3 and
    /// newer. On older Macs AVPlayer reports
    /// the item as playing (rate 1.0) while
    /// the layer stays black, so it must be
    /// converted to H.264 to be usable.
    private static let supportedCodecs: Set<String> = [
        "avc1", "avc3",  // H.264
        "hvc1", "hev1",  // HEVC
        "vp09",          // VP9
        "mp4v",          // MPEG-4 Part 2
    ]

    /// Four-character codec tag of the video
    /// track ("avc1"), or nil if it can't
    /// be read.
    static func codecName(videoURL: URL) -> String? {
        let asset = AVURLAsset(url: videoURL)
        guard let track = asset.tracks(withMediaType: .video).first,
              let desc = track.formatDescriptions.first
        else { return nil }
        let formatDesc = desc as! CMVideoFormatDescription
        let subtype = CMFormatDescriptionGetMediaSubType(formatDesc)
        let char = { (shift: Int) in
            UnicodeScalar(UInt32(truncatingIfNeeded: subtype >> shift) & 0xff)!
        }
        return String(char(24)) + String(char(16))
            + String(char(8)) + String(char(0))
    }

    static func isSupported(_ codecName: String) -> Bool {
        supportedCodecs.contains(codecName.lowercased())
    }

    /// Friendly name for the codec tag
    /// ("av01" -> "AV1").
    static func displayName(_ codecName: String) -> String {
        switch codecName.lowercased() {
        case "av01": return "AV1"
        case "vp09": return "VP9"
        case "mp4v": return "MPEG-4"
        default: return codecName.uppercased()
        }
    }

    /// Looks for an ffmpeg the user has
    /// installed (Homebrew, MacPorts or the
    /// system copy) to transcode codecs the
    /// wallpaper window can't render.
    static func findFFmpeg() -> URL? {
        let candidates = [
            "/opt/homebrew/bin/ffmpeg",
            "/usr/local/bin/ffmpeg",
            "/opt/local/bin/ffmpeg",
            "/usr/bin/ffmpeg",
        ]
        return candidates
            .first { FileManager.default.isExecutableFile(atPath: $0) }
            .map { URL(fileURLWithPath: $0) }
    }

    /// Transcodes `videoURL` to H.264 with the
    /// hardware VideoToolbox encoder. Returns
    /// true when the output file is usable.
    static func transcodeToH264(videoURL: URL, outputURL: URL) -> Bool {
        guard let ffmpeg = findFFmpeg() else { return false }
        let process = Process()
        process.executableURL = ffmpeg
        process.arguments = [
            "-y", "-i", videoURL.path,
            "-c:v", "h264_videotoolbox", "-b:v", "8M",
            "-pix_fmt", "yuv420p",
            "-c:a", "aac", "-b:a", "128k",
            "-movflags", "+faststart",
            outputURL.path,
        ]
        do {
            try process.run()
            process.waitUntilExit()
            return process.terminationStatus == 0
                && FileManager.default.fileExists(atPath: outputURL.path)
        } catch {
            fputs("WEM: ffmpeg failed: \(error)\n", stderr)
            return false
        }
    }
}
