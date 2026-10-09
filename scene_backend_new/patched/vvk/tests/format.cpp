#include <rstd/test/gtest.hpp>
import rstd;
import vvk;
using namespace rstd::prelude;
using namespace rstd::literals;

TEST(Format, VulkanDisplayAndToString) {
    EXPECT_TRUE(rstd::format("{}", VK_SUCCESS) == "VK_SUCCESS"_str);
    EXPECT_TRUE(rstd::to_string(VK_ERROR_DEVICE_LOST) == "VK_ERROR_DEVICE_LOST"_str);
    EXPECT_TRUE(rstd::format("{}", VK_FORMAT_R8G8B8A8_UNORM) == "VK_FORMAT_R8G8B8A8_UNORM"_str);
    EXPECT_TRUE(rstd::to_string(VK_FORMAT_D16_UNORM) == "VK_FORMAT_D16_UNORM"_str);
    EXPECT_TRUE(rstd::format("{}", VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) ==
                "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR"_str);
    EXPECT_TRUE(rstd::to_string(VkColorSpaceKHR::VK_COLOR_SPACE_HDR10_ST2084_EXT) ==
                "VK_COLOR_SPACE_HDR10_ST2084_EXT"_str);
}

TEST(Format, UnknownVulkanValues) {
    EXPECT_TRUE(rstd::to_string(static_cast<VkResult>(-123456)) == "VK_RESULT_UNKNOWN"_str);
    EXPECT_TRUE(rstd::to_string(static_cast<VkFormat>(123456)) == "VK_FORMAT_UNKNOWN"_str);
    EXPECT_TRUE(rstd::to_string(static_cast<VkColorSpaceKHR>(123456)) ==
                "VK_COLOR_SPACE_UNKNOWN"_str);
}
