import AppKit
import CoreGraphics
import WebKit

final class DesktopWebView: NSView, WKNavigationDelegate {
    private var webView: WKWebView?
    private var item: WallpaperItem?
    private var mouseEnabled = false

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

        let webView = WKWebView(frame: bounds, configuration: config)
        webView.autoresizingMask = [.width, .height]
        webView.underPageBackgroundColor = .clear
        webView.wantsLayer = true
        webView.layer?.backgroundColor = NSColor.clear.cgColor
        webView.navigationDelegate = self
        self.webView = webView
        addSubview(webView)

        webView.loadFileURL(contentURL, allowingReadAccessTo: item.folderURL)
    }

    func setMouseEnabled(_ enabled: Bool) {
        mouseEnabled = enabled
    }

    // MARK: - WKNavigationDelegate

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        injectBridge()
        applyGeneralProperties()
        applyUserProperties()
        makeBackgroundTransparent()
    }

    // MARK: - Wallpaper Engine JS bridge
    //
    // Wallpaper Engine web wallpapers expect these global functions to exist
    // (same protocol the Plasma6 plugin implements via QWebChannel):
    //   window.wallpaperPropertyListener.applyGeneralProperties(props)
    //   window.wallpaperPropertyListener.applyUserProperties(props)
    //   window.wallpaperRegisterAudioListener(listener)
    //   window.wallpaperRequestRandomFileForProperty(prop, cb)

    private func injectBridge() {
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
        webView?.evaluateJavaScript(bridge)
    }

    private func applyGeneralProperties() {
        guard let properties = item?.project?.general?.properties, !properties.isEmpty else { return }
        let js = "if (window.wallpaperPropertyListener) { wallpaperPropertyListener.applyGeneralProperties(\(propertiesJSON(properties))); }"
        webView?.evaluateJavaScript(js)
    }

    /// Wallpaper Engine applies the saved user property
    /// values when a wallpaper loads, not the authored
    /// defaults, so read them from the property store.
    private func applyUserProperties() {
        guard let item = item,
              let properties = item.project?.properties,
              !properties.isEmpty else { return }
        let values = WallpaperPropertyStore.shared.values(for: item)
        setUserPropertyValues(values, types: propertyTypes(properties))
    }

    /// Pushes user property updates to the page — the
    /// same call Wallpaper Engine makes when the user
    /// changes a setting in its UI.
    func setUserPropertyValues(_ values: [String: String],
                                  types: [String: String]) {
        guard !values.isEmpty else { return }
        var json: [String: [String: String]] = [:]
        for (key, value) in values {
            json[key] = ["value": value, "type": types[key] ?? "text"]
        }
        guard let data = try? JSONSerialization.data(withJSONObject: json),
              let str = String(data: data, encoding: .utf8) else { return }
        let js = "if (window.wallpaperPropertyListener) { wallpaperPropertyListener.applyUserProperties(\(str)); }"
        webView?.evaluateJavaScript(js)
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

    private func makeBackgroundTransparent() {
        let css = """
        (function() {
            var style = document.createElement('style');
            style.textContent = 'html, body { background: transparent !important; }';
            document.head.appendChild(style);
        })();
        """
        webView?.evaluateJavaScript(css)
    }

    // MARK: - Mouse forwarding

    func handleMouseEvent(_ event: CGEvent, window: NSWindow) {
        guard mouseEnabled, let webView = webView else { return }

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
