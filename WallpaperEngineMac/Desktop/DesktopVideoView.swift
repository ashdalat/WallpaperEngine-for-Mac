import AVFoundation
import AppKit

final class DesktopVideoView: NSView {
    override func makeBackingLayer() -> CALayer { AVPlayerLayer() }

    private var player: AVPlayer?
    private var loopObserver: Any?

    private var playerLayer: AVPlayerLayer { layer as! AVPlayerLayer }

    var isPlaying: Bool { player?.rate != 0 }

    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = true
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        wantsLayer = true
    }

    func load(url: URL, displayMode: DisplayMode, volume: Double, muted: Bool) {
        stop()
        let player = AVPlayer(url: url)
        self.player = player
        playerLayer.player = player
        playerLayer.videoGravity = displayMode.videoGravity
        player.isMuted = muted
        player.volume = Float(volume)

        loopObserver = NotificationCenter.default.addObserver(
            forName: .AVPlayerItemDidPlayToEndTime,
            object: player.currentItem,
            queue: .main
        ) { _ in
            player.seek(to: .zero)
            player.play()
        }
        player.play()
    }

    func play() {
        player?.play()
    }

    func pause() {
        player?.pause()
    }

    func stop() {
        if let observer = loopObserver {
            NotificationCenter.default.removeObserver(observer)
            loopObserver = nil
        }
        player?.pause()
        player = nil
        playerLayer.player = nil
    }

    func updateDisplayMode(_ mode: DisplayMode) {
        playerLayer.videoGravity = mode.videoGravity
    }

    func updateVolume(_ volume: Double, muted: Bool) {
        player?.isMuted = muted
        player?.volume = Float(volume)
    }
}
