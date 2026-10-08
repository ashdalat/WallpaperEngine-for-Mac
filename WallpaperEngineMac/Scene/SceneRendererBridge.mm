#import "SceneRendererBridge.h"

// VK_EXT_metal_surface is the supported way to create a Vulkan
// surface from a CAMetalLayer. Define the platform guard before
// any Vulkan header so vulkan_core.h pulls in vulkan_metal.h
// with the VkMetalSurfaceCreateInfoEXT struct and the
// vkCreateMetalSurfaceEXT declaration.
#define VK_USE_PLATFORM_METAL_EXT 1

// The engine's public API (compiled into libwescene-renderer.a).
#include "SceneWallpaper.hpp"
#include "SceneWallpaperSurface.hpp"

#include <memory>
#include <string>

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

using wallpaper::RenderInitInfo;
using wallpaper::SceneWallpaper;

@implementation SceneRendererBridge {
    std::unique_ptr<SceneWallpaper> _scene;
}

- (instancetype)initWithMetalLayer:(CAMetalLayer *)metalLayer
                            width:(uint32_t)width
                           height:(uint32_t)height {
    self = [super init];
    if (self) {
        _scene = std::make_unique<SceneWallpaper>();
        _scene->init();
        enableMoltenVKLogging();

        RenderInitInfo info;
        info.width = static_cast<uint16_t>(width);
        info.height = static_cast<uint16_t>(height);
        info.offscreen = false; // onscreen desktop rendering

        // The engine runs this on its render thread to create
        // the Vulkan surface that presents into our
        // CAMetalLayer. vkCreateMetalSurfaceEXT takes a
        // VkMetalSurfaceCreateInfoEXT struct whose pLayer
        // field holds the layer — NOT the layer directly.
        info.surface_info.createSurfaceOp =
            [metalLayer](VkInstance instance, VkSurfaceKHR *surface) -> VkResult {
            VkMetalSurfaceCreateInfoEXT createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
            createInfo.pLayer = metalLayer;
            return vkCreateMetalSurfaceEXT(instance, &createInfo, nullptr, surface);
        };

        // Instance extensions the surface needs. The engine's
        // GLFW example passes glfwGetRequiredInstanceExtensions,
        // which always includes VK_KHR_surface plus the platform
        // surface extension; do the equivalent here.
        // (VK_KHR_portability_enumeration is deliberately absent:
        // the engine links libMoltenVK.dylib directly, with no
        // Vulkan loader, and MoltenVK does not even list that
        // extension in vkEnumerateInstanceExtensionProperties.)
        info.surface_info.instanceExts = {
            "VK_KHR_surface",
            "VK_EXT_metal_surface",
        };

        // initVulkan posts the work to the engine's render looper;
        // it returns immediately.
        _scene->initVulkan(info);
    }
    return self;
}

- (void)dealloc {
    // ~SceneWallpaper is trivial; the loopers are stopped by
    // looper::~Looper on the way out.
    _scene.reset();
}

- (void)loadWallpaperAtURL:(NSURL *)contentURL
                 assetsURL:(NSURL *)assetsURL
                 cacheURL:(NSURL *)cacheURL {
    // Setting "source" and "assets" makes the engine mount the
    // scene.pkg archive (derived from the scene.json path) over the
    // assets directory and start loading the scene.
    _scene->setPropertyString("source", contentURL.path.UTF8String);
    _scene->setPropertyString("assets", assetsURL.path.UTF8String);
    _scene->setPropertyString("cache_path", cacheURL.path.UTF8String);
}

- (void)play {
    _scene->play();
}

- (void)pause {
    _scene->pause();
}

- (void)mouseInputX:(double)x y:(double)y {
    _scene->mouseInput(x, y);
}

- (void)setFps:(int32_t)fps {
    _scene->setPropertyInt32("fps", fps);
}

- (void)setFillMode:(int32_t)fillMode {
    _scene->setPropertyInt32("fillmode", fillMode);
}

- (void)setSpeed:(float)speed {
    _scene->setPropertyFloat("speed", speed);
}

- (void)setVolume:(float)volume {
    _scene->setPropertyFloat("volume", volume);
}

- (void)setMuted:(BOOL)muted {
    _scene->setPropertyBool("muted", muted);
}

- (void)setPropertyString:(NSString *)name value:(NSString *)value {
    _scene->setPropertyString(name.UTF8String, value.UTF8String);
}

- (void)setPropertyBool:(NSString *)name value:(BOOL)value {
    _scene->setPropertyBool(name.UTF8String, value);
}

- (void)setPropertyInt32:(NSString *)name value:(int32_t)value {
    _scene->setPropertyInt32(name.UTF8String, value);
}

- (void)setPropertyFloat:(NSString *)name value:(float)value {
    _scene->setPropertyFloat(name.UTF8String, value);
}

@end
