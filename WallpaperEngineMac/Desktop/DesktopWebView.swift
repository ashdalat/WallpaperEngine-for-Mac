import AppKit
import CoreGraphics
import WebKit

final class DesktopWebView: NSView, WKNavigationDelegate {
    /// The page currently on screen.
    private var webView: WKWebView?
    /// The next page being loaded on top — it fades
    /// in over `webView` and only after the fade
    /// completes does `webView` get removed, so the
    /// old page never disappears before the new one
    /// is ready (no black gap between web
    /// wallpapers, same principle as the video swap).
    private var incomingWebView: WKWebView?
    private var item: WallpaperItem?
    private var mouseEnabled = false

    /// Fired once after the page finishes loading and
    /// has had a beat to draw its first frame — the
    /// static-wallpaper snapshot hooks in here.
    var onFirstRender: (() -> Void)?

    override init(frame: NSRect) {
        super.init(frame: frame)
        wantsLayer = true
        layer?.backgroundColor = NSColor.clear.cgColor
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        wantsLayer = true
        layer?.backgroundColor = NSColor.clear.cgColor
    }

    func load(item: WallpaperItem, mouseEnabled: Bool) {
        self.item = item
        self.mouseEnabled = mouseEnabled
        guard let contentURL = item.contentURL else { return }

        let config = WKWebViewConfiguration()
        config.preferences.javaScriptEnabled = true
        config.preferences.javaScriptCanOpenWindowsAutomatically = false

        // Web→web switch: build the new page ON TOP of
        // the running one, invisible, and fade it in
        // from didFinish. First load: visible at once.
        let replacing = webView != nil
        let newWebView = WKWebView(frame: bounds, configuration: config)
        newWebView.autoresizingMask = [.width, .height]
        newWebView.underPageBackgroundColor = .clear
        newWebView.wantsLayer = true
        newWebView.layer?.backgroundColor = NSColor.clear.cgColor
        newWebView.alphaValue = replacing ? 0 : 1
        newWebView.navigationDelegate = self
        if replacing {
            addSubview(newWebView, positioned: .above, relativeTo: nil)
            incomingWebView = newWebView
        } else {
            addSubview(newWebView)
            webView = newWebView
        }

        newWebView.loadFileURL(contentURL, allowingReadAccessTo: item.folderURL)
    }

    func setMouseEnabled(_ enabled: Bool) {
        mouseEnabled = enabled
    }

    // MARK: - WKNavigationDelegate

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        injectBridge(webView)
        applyGeneralProperties(webView)
        applyUserProperties(webView)
        makeBackgroundTransparent(webView)

        if webView === incomingWebView {
            // Crossfade the new page in above the old
            // one; only when the fade completes does
            // the old page leave the hierarchy.
            NSAnimationContext.runAnimationGroup({ context in
                context.duration = 0.35
                webView.animator().alphaValue = 1
            }, completionHandler: { [weak self] in
                guard let self else { return }
                self.webView?.removeFromSuperview()
                self.webView = self.incomingWebView
                self.incomingWebView = nil
                fputs("WEM: web crossfade done\n", stderr)
            })
        }

        // Snapshoting immediately after didFinish can
        // catch the page before its first canvas/JS
        // draw, so give it a moment.
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) { [weak self] in
            self?.onFirstRender?()
            self?.onFirstRender = nil
        }
    }

    func webView(_ webView: WKWebView, didFail navigation: WKNavigation!,
                 withError error: Error) {
        // A failed incoming page must not take the old
        // one down — keep showing what worked.
        guard webView === incomingWebView else { return }
        fputs("WEM: web crossfade target failed: \(error.localizedDescription)\n", stderr)
        incomingWebView = nil
        webView.removeFromSuperview()
    }

    /// Full-resolution capture of the page content
    /// for the static desktop wallpaper.
    func takeSnapshot(completion: @escaping (CGImage?) -> Void) {
        guard let active = incomingWebView ?? webView else {
            completion(nil)
            return
        }
        active.takeSnapshot(with: nil) { image, _ in
            completion(image?.cgImage(forProposedRect: nil, context: nil, hints: nil))
        }
    }

    // MARK: - Wallpaper Engine JS bridge
    //
    // Wallpaper Engine web wallpapers expect these global functions to exist
    // (same protocol the Plasma6 plugin implements via QWebChannel):
    //   window.wallpaperPropertyListener.applyGeneralProperties(props)
    //   window.wallpaperPropertyListener.applyUserProperties(props)
    //   window.wallpaperRegisterAudioListener(listener)
    //   window.wallpaperRequestRandomFileForProperty(prop, cb)

    private func injectBridge(_ target: WKWebView) {
        let bridge = """
        (function() {
            if (window.__wpeBridgeInstalled) return;
            window.__wpeBridgeInstalled = true;
            window.wallpaperPropertyListener = {
                applyGeneralProperties: function(props) {
                    window.__wpeGeneralProperties = props;
                },
                applyUserProperties: function(props) {
                    window.__wpeUserProperties = props;
                    if (window.__wpeOnUserProperties) window.__wpeOnUserProperties(props);
                }
            };
            window.wallpaperRegisterAudioListener = function(listener) {
                window.__wpeAudioListener = listener;
            };
            window.wallpaperRequestRandomFileForProperty = function(prop, cb) {
                if (cb) cb(null);
            };
        })();
        """
        target.evaluateJavaScript(bridge)
    }

    private func applyGeneralProperties(_ target: WKWebView) {
        guard let properties = item?.project?.effectiveProperties, !properties.isEmpty else { return }
        let js = "if (window.wallpaperPropertyListener) { wallpaperPropertyListener.applyGeneralProperties(\(propertiesJSON(properties))); }"
        target.evaluateJavaScript(js)
    }

    /// Wallpaper Engine applies the saved user property
    /// values when a wallpaper loads, not the authored
    /// defaults, so read them from the property store.
    private func applyUserProperties(_ target: WKWebView) {
        guard let item = item,
              let properties = item.project?.effectiveProperties,
              !properties.isEmpty else { return }
        let values = WallpaperPropertyStore.shared.values(for: item)
        setUserPropertyValues(values, types: propertyTypes(properties), on: target)
    }

    /// Pushes user property updates to the page — the
    /// same call Wallpaper Engine makes when the user
    /// changes a setting in its UI.
    func setUserPropertyValues(_ values: [String: String],
                                  types: [String: String]) {
        guard let active = incomingWebView ?? webView, !values.isEmpty else { return }
        setUserPropertyValues(values, types: types, on: active)
    }

    private func setUserPropertyValues(_ values: [String: String],
                                  types: [String: String],
                                  on target: WKWebView) {
        guard !values.isEmpty else { return }
        var json: [String: [String: String]] = [:]
        for (key, value) in values {
            json[key] = ["value": value, "type": types[key] ?? "text"]
        }
        guard let data = try? JSONSerialization.data(withJSONObject: json),
              let str = String(data: data, encoding: .utf8) else { return }
        let js = "if (window.wallpaperPropertyListener) { wallpaperPropertyListener.applyUserProperties(\(str)); }"
        target.evaluateJavaScript(js)
    }

    private func propertyTypes(_ properties: [String: WEProperty]) -> [String: String] {
        properties.mapValues { $0.type ?? "text" }
    }

    private func propertiesJSON(_ properties: [String: WEProperty]) -> String {
        var json: [String: [String: String]] = [:]
        for (key, prop) in properties {
            json[key] = ["value": prop.value ?? "", "type": prop.type ?? "text"]
        }
        guard let data = try? JSONSerialization.data(withJSONObject: json),
              let str = String(data: data, encoding: .utf8) else { return "{}" }
        return str
    }

    private func makeBackgroundTransparent(_ target: WKWebView) {
        let css = """
        (function() {
            var style = document.createElement('style');
            style.textContent = 'html, body { background: transparent !important; }';
            document.head.appendChild(style);
        })();
        """
        target.evaluateJavaScript(css)
    }

    // MARK: - Mouse forwarding

    func handleMouseEvent(_ event: CGEvent, window: NSWindow) {
        guard mouseEnabled, let webView = incomingWebView ?? webView else { return }

        // NSEvent.mouseLocation is in Cocoa screen coordinates (origin at bottom-left).
        let mouse = NSEvent.mouseLocation
        let windowPoint = window.convertPoint(fromScreen: mouse)
        let viewPoint = convert(windowPoint, from: nil)
        let domX = viewPoint.x
        let domY = bounds.height - viewPoint.y

        let type: String
        switch event.type {
        case .leftMouseDown, .rightMouseDown: type = "mousedown"
        case .leftMouseUp, .rightMouseUp: type = "mouseup"
        default: type = "mousemove"
        }

        let js = """
        (function() {
            var target = document.elementFromPoint(\(domX), \(domY)) || document.body;
            var ev = new MouseEvent('\(type)', {
                clientX: \(domX), clientY: \(domY),
                bubbles: true, cancelable: true, view: window
            });
            target.dispatchEvent(ev);
        })();
        """
        webView.evaluateJavaScript(js)
    }

    func play() {
        isHidden = false
    }

    func pause() {
        isHidden = true
    }
}
