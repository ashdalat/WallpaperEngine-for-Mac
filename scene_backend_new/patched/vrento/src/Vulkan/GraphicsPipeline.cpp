module;

#include <rstd/macro.hpp>
#include <vulkan/vulkan_core.h>

#include "vvk/macros.hpp"

module vrento.vulkan;
import vrento.shader_types;
import rstd;
import rstd.log;

using namespace rstd::prelude;
using rstd::ffi::CString;
using namespace vrento::vulkan;

namespace
{

inline VkShaderStageFlagBits ToVkType(vrento::ShaderType stage) {
    using namespace vrento;
    switch (stage) {
    case ShaderType::VERTEX: return VK_SHADER_STAGE_VERTEX_BIT;
    case ShaderType::FRAGMENT: return VK_SHADER_STAGE_FRAGMENT_BIT;
    case ShaderType::GEOMETRY: return VK_SHADER_STAGE_GEOMETRY_BIT;
    default: rstd_assert(false); return VK_SHADER_STAGE_VERTEX_BIT;
    }
}

inline Option<vvk::ShaderModule> CreateShaderModule(const vvk::Device& device,
                                                    const ShaderSpv&   spv) {
    auto&                    data = spv.spirv;
    VkShaderModuleCreateInfo ci {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext    = nullptr,
        .codeSize = data.len().to_primitive() * sizeof(rstd::uint32_t),
        .pCode    = data.data(),
    };
    vvk::ShaderModule sm;
    VVK_CHECK_ACT(return None(), device.CreateShaderModule(ci, sm));
    return Some(rstd::move(sm));
}

} // namespace

GraphicsPipeline::GraphicsPipeline() { toDefault(); }
GraphicsPipeline::~GraphicsPipeline() {}

void GraphicsPipeline::toDefault() {
    depth_clip     = None();
    m_create_flags = 0;
    m_subpass      = 0;
    m_view         = VkPipelineViewportStateCreateInfo {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext         = nullptr,
        .viewportCount = 1,
        .scissorCount  = 1
    };
    multisample = VkPipelineMultisampleStateCreateInfo {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext                 = nullptr,
        .rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable   = false,
        .minSampleShading      = 1.0f,
        .alphaToCoverageEnable = false,
        .alphaToOneEnable      = false,
    };

    depth = VkPipelineDepthStencilStateCreateInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .pNext = nullptr
    };

    raster = VkPipelineRasterizationStateCreateInfo {
        .sType            = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext            = nullptr,
        .depthClampEnable = false,
        .polygonMode      = VK_POLYGON_MODE_FILL,
        .cullMode         = VK_CULL_MODE_NONE,
        .frontFace        = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable  = false,
        .lineWidth        = 1.0f,
    };
    m_color_attachments.clear();
    m_color_attachments.push(VkPipelineColorBlendAttachmentState {
        .blendEnable    = false,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT });
    m_color = VkPipelineColorBlendStateCreateInfo {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext           = nullptr,
        .logicOpEnable   = false,
        .logicOp         = VK_LOGIC_OP_COPY,
        .attachmentCount = static_cast<rstd::uint32_t>(m_color_attachments.len().to_primitive()),
        .pAttachments    = m_color_attachments.data(),
    };
    m_dynamic_states = Vec<VkDynamicState>::from(
        rstd::array<VkDynamicState, 2> { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }
            .as_slice());

    m_input_assembly = VkPipelineInputAssemblyStateCreateInfo {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext                  = nullptr,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
        .primitiveRestartEnable = false
    };
}

const ShaderSpv* GraphicsPipeline::getShaderSpv(VkShaderStageFlagBits stage) const {
    if (auto spv = m_stage_spv_map.get(u32(stage)); spv.is_some()) {
        return (**spv).as_ptr().as_raw_ptr();
    }
    return nullptr;
}

GraphicsPipeline&
GraphicsPipeline::setColorBlendStates(slice<VkPipelineColorBlendAttachmentState> stats) {
    m_color_attachments     = Vec<VkPipelineColorBlendAttachmentState>::from(stats);
    m_color.attachmentCount = static_cast<rstd::uint32_t>(m_color_attachments.len().to_primitive());
    m_color.pAttachments    = m_color_attachments.data();
    return *this;
}

GraphicsPipeline& GraphicsPipeline::setColorBlendOptions(VkPipelineColorBlendStateCreateFlags flags,
                                                         const rstd::array<float, 4>& constants) {
    m_color.flags = flags;
    rstd::slice_::copy_from_slice(
        rstd::mut_ref<float[]>::from_raw_parts(m_color.blendConstants, usize(4)),
        constants.as_slice());
    return *this;
}

GraphicsPipeline& GraphicsPipeline::setLogicOp(bool enable, VkLogicOp op) {
    m_color.logicOp       = op;
    m_color.logicOpEnable = enable;
    return *this;
}

GraphicsPipeline& GraphicsPipeline::setCreateInfoOptions(VkPipelineCreateFlags flags,
                                                         rstd::uint32_t        subpass) {
    m_create_flags = flags;
    m_subpass      = subpass;
    return *this;
}

GraphicsPipeline& GraphicsPipeline::setRenderPass(vvk::RenderPass pass) {
    m_pass = rstd::move(pass);
    return *this;
}

GraphicsPipeline& GraphicsPipeline::addStage(Uni_ShaderSpv&& spv) {
    VkShaderStageFlagBits stage = ::ToVkType(spv->stage);
    (void)m_stage_spv_map.insert(u32(stage), rstd::move(spv));
    return *this;
}

GraphicsPipeline&
GraphicsPipeline::addInputAttributeDescription(slice<VkVertexInputAttributeDescription> attrs) {
    m_input_attr_descriptions.extend_from_slice(attrs);
    return *this;
}
GraphicsPipeline&
GraphicsPipeline::addInputBindingDescription(slice<VkVertexInputBindingDescription> binds) {
    m_input_bind_descriptions.extend_from_slice(binds);
    return *this;
}
GraphicsPipeline& GraphicsPipeline::setTopology(VkPrimitiveTopology topology) {
    m_input_assembly.topology = topology;
    return *this;
}

GraphicsPipeline& GraphicsPipeline::setPrimitiveRestartEnable(bool enable) {
    m_input_assembly.primitiveRestartEnable = enable;
    return *this;
}

GraphicsPipeline& GraphicsPipeline::setViewportScissorCount(rstd::uint32_t viewport_count,
                                                            rstd::uint32_t scissor_count) {
    m_view.viewportCount = viewport_count;
    m_view.scissorCount  = scissor_count;
    return *this;
}

GraphicsPipeline& GraphicsPipeline::setDynamicStates(slice<VkDynamicState> states) {
    m_dynamic_states = Vec<VkDynamicState>::from(states);
    return *this;
}

GraphicsPipeline& GraphicsPipeline::setSampleCount(VkSampleCountFlagBits s) {
    multisample.rasterizationSamples = s;
    return *this;
}

bool GraphicsPipeline::create(const Device& device, VkRenderPass pass, VkPipelineLayout layout,
                              PipelineParameters& pipeline) {
    VkPipelineDynamicStateCreateInfo dynamic_info {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext             = nullptr,
        .dynamicStateCount = static_cast<rstd::uint32_t>(m_dynamic_states.len().to_primitive()),
        .pDynamicStates    = m_dynamic_states.data()
    };
    pipeline.layout = layout;

    auto entry_names = Vec<CString>::with_capacity(m_stage_spv_map.len());
    Vec<VkPipelineShaderStageCreateInfo> shaderStages;
    Vec<vvk::ShaderModule>               shader_modules;
    for (const auto& [stage, value] : m_stage_spv_map.iter()) {
        const auto& spv   = *value;
        auto        entry = CString::make(Vec<u8>::from(spv->entry_point.as_str().as_bytes()));
        if (entry.is_err()) return false;
        entry_names.push(rstd::move(entry).unwrap());
        VkPipelineShaderStageCreateInfo info {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .stage = ::ToVkType(spv->stage),
            .pName = entry_names.last().unwrap()->as_ptr()
        };
        if (auto opt = CreateShaderModule(device.handle(), *spv); opt.is_some()) {
            shader_modules.push(rstd::move(opt).unwrap());
            info.module = *shader_modules.last().unwrap().get();
        }

        shaderStages.push(rstd::move(info));
    }

    VkPipelineVertexInputStateCreateInfo input {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .vertexBindingDescriptionCount =
            static_cast<rstd::uint32_t>(m_input_bind_descriptions.len().to_primitive()),
        .pVertexBindingDescriptions = m_input_bind_descriptions.data(),
        .vertexAttributeDescriptionCount =
            static_cast<rstd::uint32_t>(m_input_attr_descriptions.len().to_primitive()),
        .pVertexAttributeDescriptions = m_input_attr_descriptions.data()
    };

    auto                                               raster_state = raster;
    VkPipelineRasterizationDepthClipStateCreateInfoEXT clip_state {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT,
        .pNext = raster_state.pNext,
        .depthClipEnable = depth_clip.unwrap_or(true) ? VK_TRUE : VK_FALSE,
    };
    if (depth_clip.is_some()) {
        if (device.capabilities().depth_clip_enable) {
            raster_state.pNext = &clip_state;
        } else {
            // Without independent clipping, preserve clipping rather than clamping.
            if (! *depth_clip && ! device.capabilities().depth_clamp) return false;
            raster_state.depthClampEnable = ! *depth_clip;
        }
    }
    VkGraphicsPipelineCreateInfo create {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext               = nullptr,
        .flags               = m_create_flags,
        .stageCount          = static_cast<rstd::uint32_t>(shaderStages.len().to_primitive()),
        .pStages             = shaderStages.data(),
        .pVertexInputState   = &input,
        .pInputAssemblyState = &m_input_assembly,
        .pViewportState      = &m_view,
        .pRasterizationState = &raster_state,
        .pMultisampleState   = &multisample,
        .pDepthStencilState  = &depth,
        .pColorBlendState    = &m_color,
        .pDynamicState       = &dynamic_info,
        .layout              = pipeline.layout,
        .renderPass          = pass,
        .subpass             = m_subpass,
    };
    VVK_CHECK_BOOL_RE(device.handle().CreateGraphicsPipeline(create, pipeline.handle));
    return true;
}
