#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

NS_ASSUME_NONNULL_BEGIN

/// Renders Wallpaper Engine "scene" (3D) wallpapers with the
/// wallpaper-scene-renderer engine (Vulkan, translated to Metal by
/// MoltenVK). The engine owns its instance/device/swapchain and its
/// render threads; every method here only posts a message to the
/// engine's internal loopers, so all methods are thread-safe and
/// non-blocking.
@interface SceneRendererBridge : NSObject

/// Creates the Vulkan instance, physical/logical device and the
/// onscreen swapchain bound to `metalLayer`.
/// `width`/`height` are the layer's size in PIXELS (points x
/// backingScaleFactor).
- (instancetype)initWithMetalLayer:(CAMetalLayer *)metalLayer
                            width:(uint32_t)width
                           height:(uint32_t)height;

/// Loads a Wallpaper Engine scene wallpaper.
/// @param contentURL  the wallpaper's scene.json
///                    (WallpaperItem.contentURL)
/// @param assetsURL   Wallpaper Engine shared assets directory
///                    (steamapps/common/wallpaper_engine/assets).
///                    Fully self-contained scenes work with an empty
///                    directory.
/// @param cacheURL    directory the engine caches decompressed data in
- (void)loadWallpaperAtURL:(NSURL *)contentURL
                 assetsURL:(NSURL *)assetsURL
                 cacheURL:(NSURL *)cacheURL;

/// Start / stop the render loop (the engine keeps rendering once a
/// scene is loaded; these pause/resume the frame timer).
- (void)play;
- (void)pause;

/// Normalized mouse position, each component in 0..1.
/// `y` is measured from the TOP of the view (engine convention).
- (void)mouseInputX:(double)x y:(double)y;

/// Mouse button state. `button` is the CGMouseButton
/// index (0 = left, 1 = right, 2 = middle).
- (void)mouseButton:(int)button down:(BOOL)down;

/// Reports the cursor entering/leaving the wallpaper
/// window. Scenes gate pointer interactions on this
/// flag, so the host must report changes.
- (void)mouseEnter:(BOOL)inWindow;

/// Engine property setters. See wallpaper::SceneWallpaper for the
/// property names/semantics.
- (void)setFps:(int32_t)fps;          // >= 5
- (void)setFillMode:(int32_t)fillMode; // 0=stretch, 1=aspect-fit, 2=aspect-crop
- (void)setSpeed:(float)speed;
- (void)setVolume:(float)volume;
- (void)setMuted:(BOOL)muted;

/// Applies one Wallpaper Engine user property (from the
/// project.json "properties" table) to the scene: shader
/// uniforms, textures, clear color. `value` is the wire
/// string form, e.g. "0.5" (slider), "true"/"false"
/// (checkbox), "0.2 0.4 0.9" (color).
- (void)setUserProperty:(NSString *)key value:(NSString *)value;

/// Generic property passthrough.
- (void)setPropertyString:(NSString *)name value:(NSString *)value;
- (void)setPropertyBool:(NSString *)name value:(BOOL)value;
- (void)setPropertyInt32:(NSString *)name value:(int32_t)value;
- (void)setPropertyFloat:(NSString *)name value:(float)value;

@end

NS_ASSUME_NONNULL_END
