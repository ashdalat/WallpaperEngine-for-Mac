module vvk;
import rstd;
using namespace rstd::literals;

namespace rstd
{
auto Impl<fmt::Display, VkResult>::fmt(fmt::Formatter& formatter) const -> bool {
#define X(str) \
    case VkResult::VK_##str: return formatter.write_str("VK_" #str ""_str);
    switch (this->self()) {
        X(SUCCESS)
        X(NOT_READY)
        X(TIMEOUT)
        X(EVENT_SET)
        X(EVENT_RESET)
        X(INCOMPLETE)
        X(ERROR_OUT_OF_HOST_MEMORY)
        X(ERROR_OUT_OF_DEVICE_MEMORY)
        X(ERROR_INITIALIZATION_FAILED)
        X(ERROR_DEVICE_LOST)
        X(ERROR_MEMORY_MAP_FAILED)
        X(ERROR_LAYER_NOT_PRESENT)
        X(ERROR_EXTENSION_NOT_PRESENT)
        X(ERROR_FEATURE_NOT_PRESENT)
        X(ERROR_INCOMPATIBLE_DRIVER)
        X(ERROR_TOO_MANY_OBJECTS)
        X(ERROR_FORMAT_NOT_SUPPORTED)
        X(ERROR_FRAGMENTED_POOL)
        X(ERROR_UNKNOWN)
        X(ERROR_OUT_OF_POOL_MEMORY)
        X(ERROR_INVALID_EXTERNAL_HANDLE)

        X(ERROR_SURFACE_LOST_KHR)
        X(ERROR_NATIVE_WINDOW_IN_USE_KHR)
        X(ERROR_VALIDATION_FAILED_EXT)
        X(SUBOPTIMAL_KHR)

    default: return formatter.write_str("VK_RESULT_UNKNOWN"_str);
    }
#undef X
}
auto Impl<fmt::Display, VkFormat>::fmt(fmt::Formatter& formatter) const -> bool {
#define X(str) \
    case VkFormat::VK_FORMAT_##str: return formatter.write_str("VK_FORMAT_" #str ""_str);
    switch (this->self()) {
        X(UNDEFINED);
        X(R4G4_UNORM_PACK8);
        X(R4G4B4A4_UNORM_PACK16);
        X(B4G4R4A4_UNORM_PACK16);
        X(R5G6B5_UNORM_PACK16);
        X(B5G6R5_UNORM_PACK16);
        X(R5G5B5A1_UNORM_PACK16);
        X(B5G5R5A1_UNORM_PACK16);
        X(A1R5G5B5_UNORM_PACK16);
        X(R8_UNORM);
        X(R8_SNORM);
        X(R8_USCALED);
        X(R8_SSCALED);
        X(R8_UINT);
        X(R8_SINT);
        X(R8_SRGB);
        X(R8G8_UNORM);
        X(R8G8_SNORM);
        X(R8G8_USCALED);
        X(R8G8_SSCALED);
        X(R8G8_UINT);
        X(R8G8_SINT);
        X(R8G8_SRGB);
        X(R8G8B8_UNORM);
        X(R8G8B8_SNORM);
        X(R8G8B8_USCALED);
        X(R8G8B8_SSCALED);
        X(R8G8B8_UINT);
        X(R8G8B8_SINT);
        X(R8G8B8_SRGB);
        X(B8G8R8_UNORM);
        X(B8G8R8_SNORM);
        X(B8G8R8_USCALED);
        X(B8G8R8_SSCALED);
        X(B8G8R8_UINT);
        X(B8G8R8_SINT);
        X(B8G8R8_SRGB);
        X(R8G8B8A8_UNORM);
        X(R8G8B8A8_SNORM);
        X(R8G8B8A8_USCALED);
        X(R8G8B8A8_SSCALED);
        X(R8G8B8A8_UINT);
        X(R8G8B8A8_SINT);
        X(R8G8B8A8_SRGB);
        X(B8G8R8A8_UNORM);
        X(B8G8R8A8_SNORM);
        X(B8G8R8A8_USCALED);
        X(B8G8R8A8_SSCALED);
        X(B8G8R8A8_UINT);
        X(B8G8R8A8_SINT);
        X(B8G8R8A8_SRGB);
        X(A8B8G8R8_UNORM_PACK32);
        X(A8B8G8R8_SNORM_PACK32);
        X(A8B8G8R8_USCALED_PACK32);
        X(A8B8G8R8_SSCALED_PACK32);
        X(A8B8G8R8_UINT_PACK32);
        X(A8B8G8R8_SINT_PACK32);
        X(A8B8G8R8_SRGB_PACK32);
        X(A2R10G10B10_UNORM_PACK32);
        X(A2R10G10B10_SNORM_PACK32);
        X(A2R10G10B10_USCALED_PACK32);
        X(A2R10G10B10_SSCALED_PACK32);
        X(A2R10G10B10_UINT_PACK32);
        X(A2R10G10B10_SINT_PACK32);
        X(A2B10G10R10_UNORM_PACK32);
        X(A2B10G10R10_SNORM_PACK32);
        X(A2B10G10R10_USCALED_PACK32);
        X(A2B10G10R10_SSCALED_PACK32);
        X(A2B10G10R10_UINT_PACK32);
        X(A2B10G10R10_SINT_PACK32);
        X(R16_UNORM);
        X(R16_SNORM);
        X(R16_USCALED);
        X(R16_SSCALED);
        X(R16_UINT);
        X(R16_SINT);
        X(R16_SFLOAT);
        X(R16G16_UNORM);
        X(R16G16_SNORM);
        X(R16G16_USCALED);
        X(R16G16_SSCALED);
        X(R16G16_UINT);
        X(R16G16_SINT);
        X(R16G16_SFLOAT);
        X(R16G16B16_UNORM);
        X(R16G16B16_SNORM);
        X(R16G16B16_USCALED);
        X(R16G16B16_SSCALED);
        X(R16G16B16_UINT);
        X(R16G16B16_SINT);
        X(R16G16B16_SFLOAT);
        X(R16G16B16A16_UNORM);
        X(R16G16B16A16_SNORM);
        X(R16G16B16A16_USCALED);
        X(R16G16B16A16_SSCALED);
        X(R16G16B16A16_UINT);
        X(R16G16B16A16_SINT);
        X(R16G16B16A16_SFLOAT);
        X(R32_UINT);
        X(R32_SINT);
        X(R32_SFLOAT);
        X(R32G32_UINT);
        X(R32G32_SINT);
        X(R32G32_SFLOAT);
        X(R32G32B32_UINT);
        X(R32G32B32_SINT);
        X(R32G32B32_SFLOAT);
        X(R32G32B32A32_UINT);
        X(R32G32B32A32_SINT);
        X(R32G32B32A32_SFLOAT);
        X(R64_UINT);
        X(R64_SINT);
        X(R64_SFLOAT);
        X(R64G64_UINT);
        X(R64G64_SINT);
        X(R64G64_SFLOAT);
        X(R64G64B64_UINT);
        X(R64G64B64_SINT);
        X(R64G64B64_SFLOAT);
        X(R64G64B64A64_UINT);
        X(R64G64B64A64_SINT);
        X(R64G64B64A64_SFLOAT);
        X(B10G11R11_UFLOAT_PACK32);
        X(E5B9G9R9_UFLOAT_PACK32);
        X(D16_UNORM);
        X(X8_D24_UNORM_PACK32);
        X(D32_SFLOAT);
        X(S8_UINT);
        X(D16_UNORM_S8_UINT);
        X(D24_UNORM_S8_UINT);
        X(D32_SFLOAT_S8_UINT);
        X(BC1_RGB_UNORM_BLOCK);
        X(BC1_RGB_SRGB_BLOCK);
        X(BC1_RGBA_UNORM_BLOCK);
        X(BC1_RGBA_SRGB_BLOCK);
        X(BC2_UNORM_BLOCK);
        X(BC2_SRGB_BLOCK);
        X(BC3_UNORM_BLOCK);
        X(BC3_SRGB_BLOCK);
        X(BC4_UNORM_BLOCK);
        X(BC4_SNORM_BLOCK);
        X(BC5_UNORM_BLOCK);
        X(BC5_SNORM_BLOCK);
        X(BC6H_UFLOAT_BLOCK);
        X(BC6H_SFLOAT_BLOCK);
        X(BC7_UNORM_BLOCK);
        X(BC7_SRGB_BLOCK);
        X(MAX_ENUM);

    default: return formatter.write_str("VK_FORMAT_UNKNOWN"_str);
    }
#undef X
}
auto Impl<fmt::Display, VkColorSpaceKHR>::fmt(fmt::Formatter& formatter) const -> bool {
#define X(str) \
    case VkColorSpaceKHR::VK_##str: return formatter.write_str("VK_" #str ""_str);
    switch (this->self()) {
        X(COLOR_SPACE_SRGB_NONLINEAR_KHR);
        X(COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT);
        X(COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT);
        X(COLOR_SPACE_DISPLAY_P3_LINEAR_EXT);
        X(COLOR_SPACE_DCI_P3_NONLINEAR_EXT);
        X(COLOR_SPACE_BT709_LINEAR_EXT);
        X(COLOR_SPACE_BT709_NONLINEAR_EXT);
        X(COLOR_SPACE_BT2020_LINEAR_EXT);
        X(COLOR_SPACE_HDR10_ST2084_EXT);
        X(COLOR_SPACE_DOLBYVISION_EXT);
        X(COLOR_SPACE_HDR10_HLG_EXT);
        X(COLOR_SPACE_ADOBERGB_LINEAR_EXT);
        X(COLOR_SPACE_ADOBERGB_NONLINEAR_EXT);
        X(COLOR_SPACE_PASS_THROUGH_EXT);
        X(COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT);
        X(COLOR_SPACE_DISPLAY_NATIVE_AMD);

    default: return formatter.write_str("VK_COLOR_SPACE_UNKNOWN"_str);
    }
#undef X
}
} // namespace rstd
