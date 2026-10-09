#import "SceneRendererBridge.h"

// The engine's C API (compiled into libwescene-renderer.a
// from the open-wallpaper-engine sources — see
// scene_backend_new/). The engine itself is C++20
// modules; this file only talks to the C bridge.
#include "wescene_bridge.h"

// MoltenVK logging — surface creation failures are silent
// at the default log level, so turn it up while debugging.
#include "MoltenVK/mvk_deprecated_api.h"

static void enableMoltenVKLogging() {
    static bool done = false;
    if (done) return;
    done = true;
    // Read the current config first: vkSetMoltenVKConfigurationMVK
    // copies the WHOLE struct over the global config, so a
    // zero-initialized struct would clear advertiseExtensions
    // (default MVK_CONFIG_ADVERTISE_EXTENSIONS_ALL) and MoltenVK
    // would stop advertising any extensions at all.
    MVKConfiguration cfg = {};
    size_t cfgSize = sizeof(cfg);
    if (vkGetMoltenVKConfigurationMVK(VK_NULL_HANDLE, &cfg, &cfgSize) == VK_SUCCESS) {
        cfg.logLevel = MVK_CONFIG_LOG_LEVEL_INFO;
        vkSetMoltenVKConfigurationMVK(VK_NULL_HANDLE, &cfg, &cfgSize);
    }
}

@implementation SceneRendererBridge {
    void* _bridge;
}

- (instancetype)initWithMetalLayer:(CAMetalLayer *)metalLayer
                            width:(uint32_t)width
                           height:(uint32_t)height {
    self = [super init];
    if (self) {
        enableMoltenVKLogging();
        // Engine messages (scene load, shader cache,
        // parse errors) go through rstd-log; install
        // the environment logger so RSTD_LOG controls
        // them. No-op after the first renderer.
        wescene_bridge_enable_env_logging();
        _bridge = wescene_bridge_create((__bridge void *)metalLayer,
                                        width, height);
        if (_bridge == nullptr) {
            fputs("WEM: scene renderer init failed\n", stderr);
        }
    }
    return self;
}

- (void)dealloc {
    // ~SceneWallpaper stops the engine's loopers on
    // the way out.
    if (_bridge != nullptr) {
        wescene_bridge_destroy(_bridge);
        _bridge = nullptr;
    }
}

- (void)loadWallpaperAtURL:(NSURL *)contentURL
                 assetsURL:(NSURL *)assetsURL
                 cacheURL:(NSURL *)cacheURL {
    if (_bridge == nullptr) return;
    // The engine mounts the scene.pkg archive (derived
    // from the scene.json path) over the assets directory
    // and starts loading the scene.
    wescene_bridge_load(_bridge,
                        contentURL.path.UTF8String,
                        assetsURL.path.UTF8String,
                        cacheURL.path.UTF8String);
}

- (void)play {
    wescene_bridge_play(_bridge);
}

- (void)pause {
    wescene_bridge_pause(_bridge);
}

- (void)mouseInputX:(double)x y:(double)y {
    wescene_bridge_mouse_input(_bridge, x, y);
}

- (void)setFps:(int32_t)fps {
    wescene_bridge_set_fps(_bridge, static_cast<unsigned>(fps));
}

- (void)setFillMode:(int32_t)fillMode {
    // 0 = stretch, 1 = aspect fit, 2 = aspect crop —
    // the engine's FillMode enum order.
    wescene_bridge_set_fill_mode(_bridge, static_cast<int>(fillMode));
}

- (void)setSpeed:(float)speed {
    wescene_bridge_set_speed(_bridge, speed);
}

- (void)setVolume:(float)volume {
    wescene_bridge_set_volume(_bridge, volume);
}

- (void)setMuted:(BOOL)muted {
    wescene_bridge_set_muted(_bridge, muted ? 1 : 0);
}

// The new engine has no generic property API (the old
// wallpaper-scene-renderer took untyped setProperty*
// calls). Nothing in the app calls these yet; keep them
// as logged no-ops so a future caller is visible.
- (void)setPropertyString:(NSString *)name value:(NSString *)value {
    fputs("WEM: setPropertyString is a no-op with the new engine: ",
          stderr);
    fputs(name.UTF8String, stderr);
    fputs("\n", stderr);
}

- (void)setPropertyBool:(NSString *)name value:(BOOL)value {
    fputs("WEM: setPropertyBool is a no-op with the new engine: ",
          stderr);
    fputs(name.UTF8String, stderr);
    fputs("\n", stderr);
}

- (void)setPropertyInt32:(NSString *)name value:(int32_t)value {
    fputs("WEM: setPropertyInt32 is a no-op with the new engine: ",
          stderr);
    fputs(name.UTF8String, stderr);
    fputs("\n", stderr);
}

- (void)setPropertyFloat:(NSString *)name value:(float)value {
    fputs("WEM: setPropertyFloat is a no-op with the new engine: ",
          stderr);
    fputs(name.UTF8String, stderr);
    fputs("\n", stderr);
}

@end
