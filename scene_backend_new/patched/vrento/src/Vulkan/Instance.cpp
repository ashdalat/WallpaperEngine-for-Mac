module;

#include <rstd/macro.hpp>
#include "vvk/macros.hpp"

#if __is_target_os(macos)
#    include <vulkan/vulkan.h>
#    include <vulkan/vulkan_metal.h>
#endif

module vrento.vulkan;

import rstd;
import rstd.log;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::collections::BTreeSet;
using rstd::ffi::CStr;
using rstd::ffi::CString;

#include "ExtensionNames.hpp"
using namespace vrento::vulkan;

constexpr rstd::array<InstanceLayer, 0> base_inst_layers {};

// VK_EXT_debug_utils is required everywhere. MoltenVK additionally needs
// portability enumeration; keep that extension Apple-only so Linux keeps its
// original instance extension set and create flags.
#if __is_target_os(macos)
const rstd::array<Extension, 2> base_inst_exts {
    Extension { true, CStr::from_ptr(VK_EXT_DEBUG_UTILS_EXTENSION_NAME).to_str().unwrap() },
    Extension { false, "VK_KHR_portability_enumeration"_str }
};
#else
const rstd::array<Extension, 1> base_inst_exts { Extension {
    true, CStr::from_ptr(VK_EXT_DEBUG_UTILS_EXTENSION_NAME).to_str().unwrap() } };
#endif

namespace
{

VkBool32 DebugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                     VkDebugUtilsMessageTypeFlagsEXT /*messageType*/,
                                     const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                     void* /*pUserData*/) {
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        rstd_error("validation layer: {}", pCallbackData->pMessage);
    } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        rstd_warn("validation layer: {}", pCallbackData->pMessage);
    }
    return VK_FALSE;
}

vvk::DebugUtilsMessenger SetupDebugCallback(vvk::Instance& instance) {
    return instance.CreateDebugUtilsMessenger(VkDebugUtilsMessengerCreateInfoEXT {
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext           = nullptr,
        .flags           = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
        .messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = DebugUtilsMessengerCallback,
        .pUserData       = nullptr,
    });
}

bool CreateInstance(vvk::Instance* inst, slice<CString> exts, slice<CString> layers,
                    vvk::InstanceDispatch& dld, rstd::uint32_t api_version,
                    const vvk::GlobalDispatch& global) {
    VkApplicationInfo app_info {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext              = nullptr,
        .pApplicationName   = WP_APPLICATION_NAME,
        .applicationVersion = api_version,
        .pEngineName        = "vulkan",
        .apiVersion         = api_version,
    };

    auto extension_names_c = Vec<const char*>::with_capacity(exts.len());
    for (const auto& ext : exts) extension_names_c.push(ext.as_ptr());
    auto layer_names_c = Vec<const char*>::with_capacity(layers.len());
    for (const auto& layer : layers) layer_names_c.push(layer.as_ptr());

    // VK_EXT_metal_objects requires the instance to opt in to the Metal
    // object type before vkExportMetalObjectsEXT can return the MTLDevice.
    // Keep this pNext optional so the same instance path remains valid on
    // non-Apple Vulkan implementations.
#if __is_target_os(macos)
    VkExportMetalObjectCreateInfoEXT metal_export_info {
        .sType            = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT,
        .pNext            = nullptr,
        .exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_DEVICE_BIT_EXT,
    };
    const void* instance_next = &metal_export_info;
#else
    const void* instance_next = nullptr;
#endif

    VkInstanceCreateInfo info {
        .sType               = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext               = instance_next,
        .pApplicationInfo    = &app_info,
        .enabledLayerCount   = static_cast<rstd::uint32_t>(layer_names_c.len().to_primitive()),
        .ppEnabledLayerNames = layer_names_c.data(),
        .enabledExtensionCount =
            static_cast<rstd::uint32_t>(extension_names_c.len().to_primitive()),
        .ppEnabledExtensionNames = extension_names_c.data(),
    };
#if __is_target_os(macos)
    for (const auto& ext : exts)
        if (ext.as_ref().to_str().unwrap() == "VK_KHR_portability_enumeration"_str)
            info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    auto result = vvk::Instance::Create(*inst, global, info, dld);
    if (result.is_err()) {
        const auto error = result.unwrap_err_unchecked();
        rstd_error("instance creation failed: kind={}, vk={}, command={}",
                   static_cast<int>(error.kind),
                   static_cast<int>(error.api_result),
                   error.command ? error.command : "");
        return false;
    }
    return true;
}
void EnumateExts(BTreeSet<String>& set, const vvk::GlobalDispatch& dld) {
    if (auto rv = vvk::EnumerateInstanceExtensionProperties(dld); rv.is_some()) {
        for (const auto& ext : *rv)
            set.insert(String::make(CStr::from_ptr(ext.extensionName).to_str().unwrap()));
    }
}
void EnumateLayers(BTreeSet<String>& set, const vvk::GlobalDispatch& dld) {
    if (auto rv = vvk::EnumerateInstanceLayerProperties(dld); rv.is_some()) {
        for (const auto& ext : *rv)
            set.insert(String::make(CStr::from_ptr(ext.layerName).to_str().unwrap()));
    }
}
} // namespace

bool Instance::ChoosePhysicalDevice(mut_ref<CheckGpuOp> checkgpu, slice<rstd::uint8_t> uuid) {
    auto deviceList = m_vinst.EnumeratePhysicalDevices();

    auto logGpu = [](const VkPhysicalDeviceProperties& props) {
        rstd_info("vulkan device: {}", props.deviceName);
    };

    vvk::PhysicalDevice        final_gpu;
    VkPhysicalDeviceProperties final_props;

    for (const auto& d : deviceList) {
        VkPhysicalDeviceIDProperties device_id_props {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES, .pNext = nullptr
        };
        VkPhysicalDeviceProperties2 props2 { .sType =
                                                 VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                             .pNext = &device_id_props };
        d.GetProperties2KHR(props2);
        auto& props = props2.properties;
        if (! uuid.is_empty()) {
            auto device_uuid = slice<rstd::uint8_t>::from_raw_parts(device_id_props.deviceUUID,
                                                                    usize(VK_UUID_SIZE));
            if (uuid == device_uuid) {
                final_props = props;
                final_gpu   = d;
                break;
            }
        } else {
            if (checkgpu->operator()(d)) {
                final_props = props;
                final_gpu   = d;
                break;
            }
        }
    }
    if (final_gpu) {
        logGpu(final_props);
        m_gpu = final_gpu;
        return true;
    } else {
        rstd_error("failed to find GPU with vulkan support");
        return false;
    }
}

const vvk::Instance&       Instance::inst() const { return m_vinst; }
const vvk::PhysicalDevice& Instance::gpu() const { return m_gpu; }
const vvk::SurfaceKHR&     Instance::surface() const { return m_surface; }

bool Instance::offscreen() const { return ! m_surface; }

void Instance::setSurface(VkSurfaceKHR sf) {
    m_surface = vvk::SurfaceKHR(sf, *m_vinst, m_vinst.Dispatch());
}

bool Instance::supportExt(ref<str> name) const { return m_extensions.contains(name); }
bool Instance::supportLayer(ref<str> name) const { return m_layers.contains(name); }

void Instance::Destroy() {}

bool Instance::Create(Instance& inst, slice<Extension> instExts, slice<InstanceLayer> instLayers,
                      rstd::uint32_t api_version) {
    auto loader = vvk::VulkanLoader::Open();
    if (loader.is_err()) {
        rstd_error("Vulkan loader unavailable: {}", loader.unwrap_err_unchecked().message);
        return false;
    }
    inst.m_loader      = Some(loader.unwrap_unchecked());
    const auto& global = inst.m_loader->global();

    EnumateExts(inst.m_extensions, global);
    auto requested_exts = Vec<Extension>::from(base_inst_exts.as_slice());
    requested_exts.extend_from_slice(instExts);
    auto exts = detail::SelectExtensionNames(inst.m_extensions, requested_exts.as_slice());
    if (exts.is_err()) {
        rstd_error("required vulkan instance extension \"{}\" is not supported", exts.unwrap_err());
        return false;
    }

    EnumateLayers(inst.m_layers, global);
    auto requested_layers = Vec<InstanceLayer>::from(base_inst_layers.as_slice());
    requested_layers.extend_from_slice(instLayers);
    auto layers = detail::SelectExtensionNames(inst.m_layers, requested_layers.as_slice());
    if (layers.is_err()) {
        rstd_error("required vulkan instance layer \"{}\" is not supported", layers.unwrap_err());
        return false;
    }
    if (! CreateInstance(
            &inst.m_vinst, exts->as_slice(), layers->as_slice(), inst.m_dld, api_version, global))
        return false;
    inst.m_api_version        = api_version;
    inst.m_enabled_extensions = rstd::move(exts).unwrap();

    inst.m_debug_utils = SetupDebugCallback(inst.m_vinst);
    return true;
}
