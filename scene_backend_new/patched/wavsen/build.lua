local target = lito.target({ kind = "lib", name = "wavsen" })
local tool = lito.tool("glslang")

lito.run({
    tool = tool,
    cwd = ".",
    args = { "-V", "--vn", "nv12_to_rgba_spv", "-o", "@OUTPUT:1@", "@INPUT:1@" },
    inputs = { "shaders/nv12_to_rgba.comp" },
    outputs = { "include/nv12_to_rgba.spv.h" },
})

lito.target_add_generated_include(target, "include")

lito.write({
    output = "include/nv12_to_bgra.metal.h",
    inputs = { "src/video/apple/nv12_to_bgra.metal" },
    content = [[static constexpr unsigned char wavsen_nv12_metal[] = {
#embed "@INPUT:1@"
, 0
};
]],
})
