#include <aegis/renderer2d/batch_renderer.hpp>

#include <aegis/core/assert.hpp>
#include <aegis/core/log.hpp>

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#ifndef AEGIS_SPV_DIR
#error "AEGIS_SPV_DIR must be defined by the build (directory containing compiled .spv files)"
#endif

namespace aegis::renderer2d {

using core::f32;
using core::u32;
using core::u64;

const char* primitive_kind_name(const PrimitiveKind kind) noexcept {
    switch (kind) {
        case PrimitiveKind::Sprite: return "sprite";
        case PrimitiveKind::Particle: return "particle";
        case PrimitiveKind::UI: return "ui";
        case PrimitiveKind::Line: return "line";
        case PrimitiveKind::Glyph: return "glyph";
        case PrimitiveKind::Part: return "part";
    }
    return "unknown";
}

BatchRenderer::~BatchRenderer() { destroy(); }

BatchRenderer::BatchRenderer(BatchRenderer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      color_format_(std::exchange(other.color_format_, VK_FORMAT_UNDEFINED)),
      config_(other.config_),
      layout_(std::exchange(other.layout_, VK_NULL_HANDLE)),
      pipelines_(other.pipelines_),
      instance_buffers_(std::move(other.instance_buffers_)),
      part_instance_buffers_(std::move(other.part_instance_buffers_)),
      extent_(other.extent_),
      stats_(other.stats_),
      lifetime_draw_calls_(other.lifetime_draw_calls_),
      lifetime_instances_(other.lifetime_instances_) {
    other.pipelines_.fill(VK_NULL_HANDLE);
    other.stats_ = BatchStats{};
    other.lifetime_draw_calls_ = 0;
    other.lifetime_instances_ = 0;
}

BatchRenderer& BatchRenderer::operator=(BatchRenderer&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        color_format_ = std::exchange(other.color_format_, VK_FORMAT_UNDEFINED);
        config_ = other.config_;
        layout_ = std::exchange(other.layout_, VK_NULL_HANDLE);
        pipelines_ = other.pipelines_;
        other.pipelines_.fill(VK_NULL_HANDLE);
        instance_buffers_ = std::move(other.instance_buffers_);
        part_instance_buffers_ = std::move(other.part_instance_buffers_);
        extent_ = other.extent_;
        stats_ = other.stats_;
        lifetime_draw_calls_ = other.lifetime_draw_calls_;
        lifetime_instances_ = other.lifetime_instances_;
        other.stats_ = BatchStats{};
        other.lifetime_draw_calls_ = 0;
        other.lifetime_instances_ = 0;
    }
    return *this;
}

core::Result<BatchRenderer> BatchRenderer::create(const VkDevice device,
                                                  const VkPhysicalDeviceMemoryProperties& memory_properties,
                                                  const VkFormat color_format,
                                                  const BatchRendererConfig& config) {
    if (device == VK_NULL_HANDLE) {
        return core::Result<BatchRenderer>::failure(core::ErrorCode::InvalidArgument,
                                                    "BatchRenderer requires a valid VkDevice");
    }
    if (color_format == VK_FORMAT_UNDEFINED) {
        return core::Result<BatchRenderer>::failure(core::ErrorCode::InvalidArgument,
                                                    "BatchRenderer requires a color format (swapchain format)");
    }
    if (config.max_instances == 0 || config.max_instances > (1u << 22)) {
        return core::Result<BatchRenderer>::failure(core::ErrorCode::InvalidArgument,
                                                    "max_instances must be in [1, 4M]");
    }
    if (config.max_frames_in_flight == 0 || config.max_frames_in_flight > 4) {
        return core::Result<BatchRenderer>::failure(core::ErrorCode::InvalidArgument,
                                                    "max_frames_in_flight must be in [1, 4]");
    }

    BatchRenderer renderer;
    renderer.device_ = device;
    renderer.color_format_ = color_format;
    renderer.config_ = config;

    const core::u64 buffer_bytes = static_cast<core::u64>(config.max_instances) * sizeof(SpriteInstance);
    const core::u64 part_buffer_bytes = static_cast<core::u64>(config.max_instances) * sizeof(PartInstance);
    renderer.instance_buffers_.reserve(config.max_frames_in_flight);
    renderer.part_instance_buffers_.reserve(config.max_frames_in_flight);
    for (u32 frame = 0; frame < config.max_frames_in_flight; ++frame) {
        memory::GpuBufferDesc desc;
        desc.size = buffer_bytes;
        desc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        desc.memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        auto buffer = memory::GpuBuffer::create(device, memory_properties, desc);
        if (!buffer) {
            renderer.destroy();
            return core::Result<BatchRenderer>::failure(
                buffer.error().code,
                "failed to allocate instance buffer: " + buffer.error().message,
                buffer.error().native_code);
        }
        renderer.instance_buffers_.push_back(std::move(buffer.value()));

        memory::GpuBufferDesc part_desc = desc;
        part_desc.size = part_buffer_bytes;
        auto part_buffer = memory::GpuBuffer::create(device, memory_properties, part_desc);
        if (!part_buffer) {
            renderer.destroy();
            return core::Result<BatchRenderer>::failure(
                part_buffer.error().code,
                "failed to allocate part instance buffer: " + part_buffer.error().message,
                part_buffer.error().native_code);
        }
        renderer.part_instance_buffers_.push_back(std::move(part_buffer.value()));
    }

    if (auto result = renderer.create_pipelines(); !result) {
        const std::string message = result.error().message;
        const core::ErrorCode code = result.error().code;
        const core::i32 native = result.error().native_code;
        renderer.destroy();
        return core::Result<BatchRenderer>::failure(code, message, native);
    }

    AEGIS_LOG_INFO("[aegis] batch renderer created: " << config.max_instances
                   << " instances (40B) + " << config.max_instances << " part instances (56B) x "
                   << config.max_frames_in_flight << " frames-in-flight");
    return core::Result<BatchRenderer>::ok(std::move(renderer));
}

void BatchRenderer::begin_frame(const VkCommandBuffer cmd,
                                const u32 frame_index,
                                const f32 projection[16],
                                const VkExtent2D extent) {
    AEGIS_ASSERT_MSG(cmd != VK_NULL_HANDLE, "begin_frame requires a valid command buffer");
    cmd_ = cmd;
    frame_index_ = frame_index % config_.max_frames_in_flight;
    std::memcpy(projection_.data(), projection, sizeof(projection_));
    extent_ = extent;
    append_index_ = 0;
    append_part_index_ = 0;
    open_batch_valid_ = false;
    batches_.clear();
    overflow_warned_ = false;
    stats_ = BatchStats{};
}

void BatchRenderer::draw_sprite(const SpriteInstance& instance) { append(PrimitiveKind::Sprite, &instance, sizeof(SpriteInstance)); }
void BatchRenderer::draw_particle(const SpriteInstance& instance) { append(PrimitiveKind::Particle, &instance, sizeof(SpriteInstance)); }
void BatchRenderer::draw_ui(const SpriteInstance& instance) { append(PrimitiveKind::UI, &instance, sizeof(SpriteInstance)); }
void BatchRenderer::draw_line(const LineInstance& instance) { append(PrimitiveKind::Line, &instance, sizeof(LineInstance)); }
void BatchRenderer::draw_glyph(const GlyphInstance& instance) { append(PrimitiveKind::Glyph, &instance, sizeof(GlyphInstance)); }
void BatchRenderer::draw_part(const PartInstance& instance) { append(PrimitiveKind::Part, &instance, sizeof(PartInstance)); }

void BatchRenderer::draw_line(const core::f32 ax, const core::f32 ay, const core::f32 bx, const core::f32 by,
                              const core::f32 r, const core::f32 g, const core::f32 b, const core::f32 a,
                              const core::f32 thickness, const core::f32 softness) {
    LineInstance instance;
    instance.ax = ax;
    instance.ay = ay;
    instance.bx = bx;
    instance.by = by;
    instance.r = r;
    instance.g = g;
    instance.b = b;
    instance.a = a;
    instance.thickness = thickness;
    instance.softness = softness;
    draw_line(instance);
}

void BatchRenderer::append(const PrimitiveKind kind, const void* const instance,
                           const core::u32 instance_bytes_size) {
    if (cmd_ == VK_NULL_HANDLE) {
        if (!overflow_warned_) {
            AEGIS_LOG_WARN("[aegis] BatchRenderer::draw_* called outside begin_frame; dropping instance");
            overflow_warned_ = true;
        }
        return;
    }
    static_assert(sizeof(SpriteInstance) == sizeof(LineInstance) && sizeof(LineInstance) == sizeof(GlyphInstance),
                  "the 40-byte class must keep a uniform stride");
    const bool part_class = (kind == PrimitiveKind::Part);
    if (part_class) {
        if (append_part_index_ >= config_.max_instances) {
            if (!overflow_warned_) {
                AEGIS_LOG_WARN("[aegis] part instance buffer full (max_instances=" << config_.max_instances
                               << "); dropping further part instances this frame");
                overflow_warned_ = true;
            }
            return;
        }
        const u32 slot = append_part_index_++;
        part_instance_buffers_[frame_index_].write(instance, static_cast<u64>(slot) * sizeof(PartInstance),
                                                   sizeof(PartInstance));
        if (!open_batch_valid_ || open_kind_ != kind) {
            close_open_batch();
            open_kind_ = kind;
            open_batch_first_ = slot;
            open_batch_count_ = 1;
            open_batch_valid_ = true;
        } else {
            ++open_batch_count_;
        }
        return;
    }
    if (append_index_ >= config_.max_instances) {
        if (!overflow_warned_) {
            AEGIS_LOG_WARN("[aegis] instance buffer full (max_instances=" << config_.max_instances
                           << "); dropping further instances this frame");
            overflow_warned_ = true;
        }
        return;
    }
    const u32 slot = append_index_++;
    instance_buffers_[frame_index_].write(instance, static_cast<u64>(slot) * sizeof(SpriteInstance),
                                          sizeof(SpriteInstance));
    (void)instance_bytes_size;

    if (!open_batch_valid_ || open_kind_ != kind) {
        close_open_batch();
        open_kind_ = kind;
        open_batch_first_ = slot;
        open_batch_count_ = 1;
        open_batch_valid_ = true;
    } else {
        ++open_batch_count_;
    }
}

void BatchRenderer::close_open_batch() {
    if (!open_batch_valid_) {
        return;
    }
    batches_.push_back(Batch{open_kind_, open_batch_first_, open_batch_count_});
    open_batch_valid_ = false;
}

void BatchRenderer::flush() {
    if (cmd_ == VK_NULL_HANDLE) {
        AEGIS_LOG_WARN("[aegis] BatchRenderer::flush called outside begin_frame; ignoring");
        return;
    }
    close_open_batch();

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<f32>(extent_.width);
    viewport.height = static_cast<f32>(extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent_;

    vkCmdSetViewport(cmd_, 0, 1, &viewport);
    vkCmdSetScissor(cmd_, 0, 1, &scissor);

    u32 draw_calls = 0;
    u64 instance_count = 0;
    u64 sprite = 0, particle = 0, ui = 0, line = 0, glyph = 0, part = 0;

    for (const Batch& batch : batches_) {
        const auto kind_index = static_cast<std::size_t>(batch.kind);
        vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_[kind_index]);
        vkCmdPushConstants(cmd_, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(projection_),
                           projection_.data());

        // Binding 0 carries the persistently mapped instance buffer. Bind it for every
        // batch (not just once per frame) so the draw is always preceded by a bind of the
        // buffer its pipeline's binding 0 descriptor targets. The binding offset stays 0
        // and the per-batch instance offset travels in vkCmdDraw's firstInstance — binding
        // a 40-byte-stride offset would not be guaranteed to satisfy
        // minVertexInputBindingOffsetAlignment on all devices.
        const VkBuffer instance_buffer = (batch.kind == PrimitiveKind::Part)
                                             ? part_instance_buffers_[frame_index_].handle()
                                             : instance_buffers_[frame_index_].handle();
        constexpr VkDeviceSize binding_offset = 0;
        vkCmdBindVertexBuffers(cmd_, 0, 1, &instance_buffer, &binding_offset);
        vkCmdDraw(cmd_, 6, batch.instance_count, 0, batch.first_instance);

        ++draw_calls;
        instance_count += batch.instance_count;
        switch (batch.kind) {
            case PrimitiveKind::Sprite: sprite += batch.instance_count; break;
            case PrimitiveKind::Particle: particle += batch.instance_count; break;
            case PrimitiveKind::UI: ui += batch.instance_count; break;
            case PrimitiveKind::Line: line += batch.instance_count; break;
            case PrimitiveKind::Glyph: glyph += batch.instance_count; break;
            case PrimitiveKind::Part: part += batch.instance_count; break;
        }
    }

    stats_.draw_calls = draw_calls;
    stats_.instance_count = instance_count;
    stats_.sprite_instances = sprite;
    stats_.particle_instances = particle;
    stats_.ui_instances = ui;
    stats_.line_instances = line;
    stats_.glyph_instances = glyph;
    stats_.part_instances = part;

    lifetime_draw_calls_ += draw_calls;
    lifetime_instances_ += instance_count;
    stats_.lifetime_draw_calls = lifetime_draw_calls_;
    stats_.lifetime_instances = lifetime_instances_;

    batches_.clear();
}

core::Result<void> BatchRenderer::create_pipelines() {
    auto vert = load_shader_module(AEGIS_SPV_DIR "/quad.vert.spv");
    if (!vert) {
        return core::Result<void>::failure(vert.error().code, vert.error().message,
                                           vert.error().native_code);
    }
    auto frag = load_shader_module(AEGIS_SPV_DIR "/quad.frag.spv");
    if (!frag) {
        vkDestroyShaderModule(device_, vert.value(), nullptr);
        return core::Result<void>::failure(frag.error().code, frag.error().message,
                                           frag.error().native_code);
    }
    auto line_vert = load_shader_module(AEGIS_SPV_DIR "/line.vert.spv");
    if (!line_vert) {
        vkDestroyShaderModule(device_, vert.value(), nullptr);
        vkDestroyShaderModule(device_, frag.value(), nullptr);
        return core::Result<void>::failure(line_vert.error().code, line_vert.error().message,
                                           line_vert.error().native_code);
    }
    auto line_frag = load_shader_module(AEGIS_SPV_DIR "/line.frag.spv");
    if (!line_frag) {
        vkDestroyShaderModule(device_, vert.value(), nullptr);
        vkDestroyShaderModule(device_, frag.value(), nullptr);
        vkDestroyShaderModule(device_, line_vert.value(), nullptr);
        return core::Result<void>::failure(line_frag.error().code, line_frag.error().message,
                                           line_frag.error().native_code);
    }
    auto part_vert = load_shader_module(AEGIS_SPV_DIR "/part.vert.spv");
    auto part_frag = load_shader_module(AEGIS_SPV_DIR "/part.frag.spv");
    auto glyph_vert = load_shader_module(AEGIS_SPV_DIR "/glyph.vert.spv");
    auto glyph_frag = load_shader_module(AEGIS_SPV_DIR "/glyph.frag.spv");
    if (!part_vert || !part_frag || !glyph_vert || !glyph_frag) {
        for (VkShaderModule module : {part_vert ? part_vert.value() : VK_NULL_HANDLE,
                                      part_frag ? part_frag.value() : VK_NULL_HANDLE,
                                      glyph_vert ? glyph_vert.value() : VK_NULL_HANDLE,
                                      glyph_frag ? glyph_frag.value() : VK_NULL_HANDLE}) {
            if (module != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device_, module, nullptr);
            }
        }
        vkDestroyShaderModule(device_, vert.value(), nullptr);
        vkDestroyShaderModule(device_, frag.value(), nullptr);
        vkDestroyShaderModule(device_, line_vert.value(), nullptr);
        vkDestroyShaderModule(device_, line_frag.value(), nullptr);
        const char* which = !part_vert ? "part.vert" : !part_frag ? "part.frag" : !glyph_vert ? "glyph.vert" : "glyph.frag";
        return core::Result<void>::failure(core::ErrorCode::VulkanFailure,
                                           std::string("failed to load ") + which + " SPIR-V");
    }

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(std::array<f32, 16>);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;

    if (const VkResult result = vkCreatePipelineLayout(device_, &layout_info, nullptr, &layout_);
        result != VK_SUCCESS) {
        vkDestroyShaderModule(device_, vert.value(), nullptr);
        vkDestroyShaderModule(device_, frag.value(), nullptr);
        vkDestroyShaderModule(device_, line_vert.value(), nullptr);
        vkDestroyShaderModule(device_, line_frag.value(), nullptr);
        vkDestroyShaderModule(device_, part_vert.value(), nullptr);
        vkDestroyShaderModule(device_, part_frag.value(), nullptr);
        vkDestroyShaderModule(device_, glyph_vert.value(), nullptr);
        vkDestroyShaderModule(device_, glyph_frag.value(), nullptr);
        return core::Result<void>::failure(core::ErrorCode::VulkanFailure,
                                           "vkCreatePipelineLayout failed",
                                           static_cast<core::i32>(result));
    }

    constexpr VkFormat kVec4 = VK_FORMAT_R32G32B32A32_SFLOAT;
    constexpr VkFormat kVec2 = VK_FORMAT_R32G32_SFLOAT;
    constexpr VkFormat kF32 = VK_FORMAT_R32_SFLOAT;
    const std::array<VkVertexInputAttributeDescription, 3> attributes_40{{
        {0, 0, kVec4, 0},
        {1, 0, kVec4, 16},
        {2, 0, kVec2, 32},
    }};
    const std::array<VkVertexInputAttributeDescription, 5> attributes_part{{
        {0, 0, kVec4, 0},
        {1, 0, kVec4, 16},
        {2, 0, kF32, 32},
        {3, 0, kVec4, 36},
        {4, 0, kF32, 52},
    }};

    pipelines_[static_cast<std::size_t>(PrimitiveKind::Sprite)] = create_pipeline(
        vert.value(), frag.value(), BlendMode::Normal, sizeof(SpriteInstance), attributes_40.data(), 3);
    pipelines_[static_cast<std::size_t>(PrimitiveKind::Particle)] = create_pipeline(
        vert.value(), frag.value(), BlendMode::Additive, sizeof(SpriteInstance), attributes_40.data(), 3);
    pipelines_[static_cast<std::size_t>(PrimitiveKind::UI)] = create_pipeline(
        vert.value(), frag.value(), BlendMode::Normal, sizeof(SpriteInstance), attributes_40.data(), 3);
    pipelines_[static_cast<std::size_t>(PrimitiveKind::Line)] = create_pipeline(
        line_vert.value(), line_frag.value(), BlendMode::Normal, sizeof(LineInstance), attributes_40.data(), 3);
    pipelines_[static_cast<std::size_t>(PrimitiveKind::Glyph)] = create_pipeline(
        glyph_vert.value(), glyph_frag.value(), BlendMode::Normal, sizeof(GlyphInstance), attributes_40.data(), 3);
    pipelines_[static_cast<std::size_t>(PrimitiveKind::Part)] = create_pipeline(
        part_vert.value(), part_frag.value(), BlendMode::Normal, sizeof(PartInstance), attributes_part.data(), 5);

    vkDestroyShaderModule(device_, vert.value(), nullptr);
    vkDestroyShaderModule(device_, frag.value(), nullptr);
    vkDestroyShaderModule(device_, line_vert.value(), nullptr);
    vkDestroyShaderModule(device_, line_frag.value(), nullptr);
    vkDestroyShaderModule(device_, part_vert.value(), nullptr);
    vkDestroyShaderModule(device_, part_frag.value(), nullptr);
    vkDestroyShaderModule(device_, glyph_vert.value(), nullptr);
    vkDestroyShaderModule(device_, glyph_frag.value(), nullptr);

    for (std::size_t i = 0; i < pipelines_.size(); ++i) {
        if (pipelines_[i] == VK_NULL_HANDLE) {
            return core::Result<void>::failure(
                core::ErrorCode::VulkanFailure,
                std::string("failed to create the ") + primitive_kind_name(static_cast<PrimitiveKind>(i)) +
                    " pipeline");
        }
    }
    return core::Result<void>::ok();
}

VkPipeline BatchRenderer::create_pipeline(const VkShaderModule vert,
                                          const VkShaderModule frag,
                                          const BlendMode blend,
                                          const core::u32 instance_stride,
                                          const VkVertexInputAttributeDescription* const attributes,
                                          const core::u32 attribute_count) const {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    // No vertex buffers: a single instance-rate binding carries the instances.
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = instance_stride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = attribute_count;
    vertex_input.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;  // dynamic
    viewport_state.scissorCount = 1;   // dynamic

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.alphaToCoverageEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    attachment.blendEnable = VK_TRUE;
    if (blend == BlendMode::Normal) {
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    } else {  // Additive
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.logicOpEnable = VK_FALSE;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &attachment;

    const std::array<VkDynamicState, 2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<u32>(dynamic_states.size());
    dynamic_state.pDynamicStates = dynamic_states.data();

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &color_format_;

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext = &rendering;
    pipeline_info.stageCount = static_cast<u32>(std::size(stages));
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = layout_;
    pipeline_info.renderPass = VK_NULL_HANDLE;  // dynamic rendering
    pipeline_info.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (const VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
                                                          &pipeline);
        result != VK_SUCCESS) {
        AEGIS_LOG_ERROR("[aegis] vkCreateGraphicsPipelines failed with VkResult " << result);
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

core::Result<VkShaderModule> BatchRenderer::load_shader_module(const char* const spv_filename) const {
    std::ifstream file(spv_filename, std::ios::binary | std::ios::ate);
    if (!file) {
        return core::Result<VkShaderModule>::failure(core::ErrorCode::IoError,
                                                     std::string("cannot open shader: ") + spv_filename);
    }

    const std::streamoff size = file.tellg();
    if (size <= 0 || (size % 4) != 0) {
        return core::Result<VkShaderModule>::failure(core::ErrorCode::IoError,
                                                     std::string("shader file is empty or not word-aligned: ") + spv_filename);
    }
    std::vector<u32> words(static_cast<std::size_t>(size) / sizeof(u32));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), size);

    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = words.size() * sizeof(u32);
    info.pCode = words.data();  // std::vector<u32> is u32-aligned (spec requires 4-byte alignment)

    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult result = vkCreateShaderModule(device_, &info, nullptr, &module);
    if (result != VK_SUCCESS) {
        return core::Result<VkShaderModule>::failure(core::ErrorCode::VulkanFailure,
                                                     std::string("vkCreateShaderModule failed for ") + spv_filename,
                                                     static_cast<core::i32>(result));
    }
    return core::Result<VkShaderModule>::ok(module);
}

void BatchRenderer::destroy() noexcept {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    for (VkPipeline& pipeline : pipelines_) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
    instance_buffers_.clear();  // GpuBuffer destructors release buffer + memory
    part_instance_buffers_.clear();
    cmd_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}

}  // namespace aegis::renderer2d
