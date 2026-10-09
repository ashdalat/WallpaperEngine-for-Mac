#include <rstd/macro.hpp>

import rstd;
import vrento.vulkan;

#include "../src/Vulkan/ExtensionNames.hpp"

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::collections::BTreeSet;
using rstd::ffi::CString;
using vrento::vulkan::Extension;
using vrento::vulkan::detail::SelectExtensionNames;

int main() {
    auto supported = BTreeSet<String>::make();
    supported.insert("VK_test_b"_Str);
    supported.insert("VK_test_a"_Str);
    Vec<CString> owned;
    {
        auto                dynamic_name = "VK_test_b"_Str;
        array<Extension, 4> requested {
            Extension { true, dynamic_name.as_str() },
            Extension { false, "VK_test_missing"_str },
            Extension { false, "VK_test_a"_str },
            Extension { true, "VK_test_b"_str },
        };
        auto selected = SelectExtensionNames(supported, requested.as_slice()).unwrap();
        rstd_assert(selected.len() == usize(2));
        owned = selected.clone();
    }
    rstd_assert(owned[usize()].as_ref().to_str().unwrap() == "VK_test_a"_str);
    rstd_assert(owned[usize(1)].as_ref().to_str().unwrap() == "VK_test_b"_str);
    rstd_assert(owned[usize()].as_ptr()[9] == '\0');

    array<Extension, 2> missing {
        Extension { true, "VK_test_first_missing"_str },
        Extension { true, "VK_test_second_missing"_str },
    };
    auto rejected = SelectExtensionNames(supported, missing.as_slice());
    rstd_assert(rejected.is_err());
    rstd_assert(rejected.unwrap_err() == "VK_test_first_missing"_str);
    rstd_assert(SelectExtensionNames(supported, {}).unwrap().is_empty());

    supported.insert("invalid\0name"_Str);
    array<Extension, 1> embedded_nul { Extension { true, "invalid\0name"_str } };
    rstd_assert(SelectExtensionNames(supported, embedded_nul.as_slice()).is_err());

    vrento::vulkan::Instance instance;
    vrento::vulkan::Device   device;
    rstd_assert(instance.enabled_extensions().is_empty());
    rstd_assert(device.enabled_instance_extensions().is_empty());
    rstd_assert(device.enabled_device_extensions().is_empty());
    rstd_assert(! instance.supportExt("VK_test_a"_str));
    rstd_assert(! instance.supportLayer("VK_test_a"_str));
    rstd_assert(! device.supportExt("VK_test_a"_str));

    int  calls = 0;
    auto check = [count = 0, &calls](const vvk::PhysicalDevice&) mutable {
        calls = ++count;
        return true;
    };
    auto callback = vrento::vulkan::CheckGpuOp::from_ref(check);
    rstd_assert(callback.as_mut_ref()->operator()(vvk::PhysicalDevice {}));
    rstd_assert(calls == 1);
    rstd_assert(callback.as_mut_ref()->operator()(vvk::PhysicalDevice {}));
    rstd_assert(calls == 2);
}
