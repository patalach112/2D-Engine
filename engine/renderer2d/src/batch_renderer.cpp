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
      glyph_set_layout_(std::exchange(other.glyph_set_layout_, VK_NULL_HANDLE)),
      glyph_image_(std::exchange(other.glyph_image_, VK_NULL_HANDLE)),
      glyph_image_memory_(std::exchange(other.glyph_image_memory_, VK_NULL_HANDLE)),
      glyph_image_view_(std::exchange(other.glyph_image_view_, VK_NULL_HANDLE)),
      glyph_sampler_(std::exchange(other.glyph_sampler_, VK_NULL_HANDLE)),
      glyph_descriptor_pool_(std::exchange(other.glyph_descriptor_pool_, VK_NULL_HANDLE)),
      glyph_descriptor_set_(std::exchange(other.glyph_descriptor_set_, VK_NULL_HANDLE)),
      memory_properties_(other.memory_properties_),
      glyph_atlas_warned_(other.glyph_atlas_warned_),
      instance_buffers_(std::move(other.instance_buffers_)),
      part_instance_buffers_(std::move(other.part_instance_buffers_)),
      extent_(other.extent_),
      stats_(other.stats_),
      lifetime_draw_calls_(other.lifetime_draw_calls_),
      lifetime_instances_(other.lifetime_instances_) {
    other.pipelines_.fill(VK_NULL_HANDLE);
    other.memory_properties_ = VkPhysicalDeviceMemoryProperties{};
    other.glyph_atlas_warned_ = false;
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
        glyph_set_layout_ = std::exchange(other.glyph_set_layout_, VK_NULL_HANDLE);
        glyph_image_ = std::exchange(other.glyph_image_, VK_NULL_HANDLE);
        glyph_image_memory_ = std::exchange(other.glyph_image_memory_, VK_NULL_HANDLE);
        glyph_image_view_ = std::exchange(other.glyph_image_view_, VK_NULL_HANDLE);
        glyph_sampler_ = std::exchange(other.glyph_sampler_, VK_NULL_HANDLE);
        glyph_descriptor_pool_ = std::exchange(other.glyph_descriptor_pool_, VK_NULL_HANDLE);
        glyph_descriptor_set_ = std::exchange(other.glyph_descriptor_set_, VK_NULL_HANDLE);
        memory_properties_ = other.memory_properties_;
        glyph_atlas_warned_ = other.glyph_atlas_warned_;
        instance_buffers_ = std::move(other.instance_buffers_);
        part_instance_buffers_ = std::move(other.part_instance_buffers_);
        extent_ = other.extent_;
        stats_ = other.stats_;
        lifetime_draw_calls_ = other.lifetime_draw_calls_;
        lifetime_instances_ = other.lifetime_instances_;
        other.memory_properties_ = VkPhysicalDeviceMemoryProperties{};
        other.glyph_atlas_warned_ = false;
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
    renderer.memory_properties_ = memory_properties;

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
        if (batch.kind == PrimitiveKind::Glyph && glyph_descriptor_set_ == VK_NULL_HANDLE) {
            if (!glyph_atlas_warned_) {
                AEGIS_LOG_WARN("[aegis] glyph batches skipped: no font atlas uploaded (set_glyph_atlas)");
                glyph_atlas_warned_ = true;
            }
            continue;
        }
        vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_[kind_index]);
        if (batch.kind == PrimitiveKind::Glyph) {
            vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1,
                                    &glyph_descriptor_set_, 0, nullptr);
        }
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

    // Glyph pipeline set 0: one combined image sampler (the font atlas). All
    // pipelines share the layout; only the glyph shader statically uses set 0.
    VkDescriptorSetLayoutBinding glyph_binding{};
    glyph_binding.binding = 0;
    glyph_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    glyph_binding.descriptorCount = 1;
    glyph_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    glyph_binding.pImmutableSamplers = nullptr;
    VkDescriptorSetLayoutCreateInfo glyph_set_layout_info{};
    glyph_set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    glyph_set_layout_info.bindingCount = 1;
    glyph_set_layout_info.pBindings = &glyph_binding;
    if (const VkResult result = vkCreateDescriptorSetLayout(device_, &glyph_set_layout_info, nullptr,
                                                            &glyph_set_layout_);
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
                                           "vkCreateDescriptorSetLayout (glyph atlas) failed",
                                           static_cast<core::i32>(result));
    }

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(std::array<f32, 16>);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &glyph_set_layout_;
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
    constexpr VkFormat kUVec2 = VK_FORMAT_R32G32_UINT;
    const std::array<VkVertexInputAttributeDescription, 3> attributes_glyph{{
        {0, 0, kVec4, 0},
        {1, 0, kVec4, 16},
        {2, 0, kUVec2, 32},
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
        glyph_vert.value(), glyph_frag.value(), BlendMode::Normal, sizeof(GlyphInstance), attributes_glyph.data(), 3);
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
    if (glyph_descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, glyph_descriptor_pool_, nullptr);
        glyph_descriptor_pool_ = VK_NULL_HANDLE;
        glyph_descriptor_set_ = VK_NULL_HANDLE;
    }
    if (glyph_sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, glyph_sampler_, nullptr);
        glyph_sampler_ = VK_NULL_HANDLE;
    }
    if (glyph_image_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, glyph_image_view_, nullptr);
        glyph_image_view_ = VK_NULL_HANDLE;
    }
    if (glyph_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, glyph_image_, nullptr);
        glyph_image_ = VK_NULL_HANDLE;
    }
    if (glyph_image_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, glyph_image_memory_, nullptr);
        glyph_image_memory_ = VK_NULL_HANDLE;
    }
    if (glyph_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, glyph_set_layout_, nullptr);
        glyph_set_layout_ = VK_NULL_HANDLE;
    }
    instance_buffers_.clear();  // GpuBuffer destructors release buffer + memory
    part_instance_buffers_.clear();
    cmd_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}

namespace {

[[nodiscard]] bool find_memory_type(const VkPhysicalDeviceMemoryProperties& props, const core::u32 type_bits,
                                    const VkMemoryPropertyFlags required, core::u32& out) noexcept {
    for (core::u32 i = 0; i < props.memoryTypeCount; ++i) {
        const bool supported = (type_bits & (1u << i)) != 0;
        const VkMemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
        if (supported && (flags & required) == required) {
            out = i;
            return true;
        }
    }
    return false;
}

}  // namespace

void BatchRenderer::set_glyph_atlas(const VkQueue queue, const core::u32 queue_family_index,
                                    const core::u8* const pixels, const core::u32 width, const core::u32 height) {
    if (queue == VK_NULL_HANDLE || pixels == nullptr || width == 0 || height == 0) {
        AEGIS_LOG_WARN("[aegis] set_glyph_atlas: invalid arguments; glyph rendering disabled");
        return;
    }
    if (glyph_descriptor_set_ != VK_NULL_HANDLE) {
        // Repeat calls happen once per capture in tools that re-render; keep it
        // cheap and quiet (the first upload wins).
        return;
    }

    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height);

    // 1) Host-visible staging buffer holds a tightly packed copy of the atlas.
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = bytes;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device_, &buffer_info, nullptr, &staging) != VK_SUCCESS) {
            AEGIS_LOG_WARN("[aegis] set_glyph_atlas: staging buffer allocation failed");
            return;
        }
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, staging, &requirements);
        u32 type_index = 0;
        if (!find_memory_type(memory_properties_, requirements.memoryTypeBits,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              type_index)) {
            vkDestroyBuffer(device_, staging, nullptr);
            AEGIS_LOG_WARN("[aegis] set_glyph_atlas: no host-visible staging memory "
                           << "(type_bits=0x" << std::hex << requirements.memoryTypeBits << std::dec
                           << ", heaps=" << memory_properties_.memoryHeapCount
                           << ", types=" << memory_properties_.memoryTypeCount << ")");
            for (u32 i = 0; i < memory_properties_.memoryTypeCount; ++i) {
                AEGIS_LOG_INFO("[aegis]   memory type " << i << " flags=0x" << std::hex
                               << memory_properties_.memoryTypes[i].propertyFlags << std::dec
                               << " heap=" << memory_properties_.memoryTypes[i].heapIndex);
            }
            return;
        }
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex = type_index;
        if (vkAllocateMemory(device_, &alloc, nullptr, &staging_memory) != VK_SUCCESS) {
            vkDestroyBuffer(device_, staging, nullptr);
            AEGIS_LOG_WARN("[aegis] set_glyph_atlas: staging memory allocation failed");
            return;
        }
        if (vkBindBufferMemory(device_, staging, staging_memory, 0) != VK_SUCCESS) {
            vkFreeMemory(device_, staging_memory, nullptr);
            vkDestroyBuffer(device_, staging, nullptr);
            AEGIS_LOG_WARN("[aegis] set_glyph_atlas: staging memory bind failed");
            return;
        }
        void* mapped = nullptr;
        if (vkMapMemory(device_, staging_memory, 0, bytes, 0, &mapped) != VK_SUCCESS || mapped == nullptr) {
            vkFreeMemory(device_, staging_memory, nullptr);
            vkDestroyBuffer(device_, staging, nullptr);
            AEGIS_LOG_WARN("[aegis] set_glyph_atlas: staging memory map failed");
            return;
        }
        std::memcpy(mapped, pixels, static_cast<std::size_t>(bytes));
        vkUnmapMemory(device_, staging_memory);
    }

    // 2) Device-local R8 image, transfer-dst -> sampled.
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    {
        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = VK_FORMAT_R8_UNORM;
        image_info.extent = VkExtent3D{width, height, 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device_, &image_info, nullptr, &image) != VK_SUCCESS) {
            vkFreeMemory(device_, staging_memory, nullptr);
            vkDestroyBuffer(device_, staging, nullptr);
            AEGIS_LOG_WARN("[aegis] set_glyph_atlas: image allocation failed");
            return;
        }
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_, image, &requirements);
        u32 type_index = 0;
        if (!find_memory_type(memory_properties_, requirements.memoryTypeBits,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, type_index)) {
            vkDestroyImage(device_, image, nullptr);
            vkFreeMemory(device_, staging_memory, nullptr);
            vkDestroyBuffer(device_, staging, nullptr);
            AEGIS_LOG_WARN("[aegis] set_glyph_atlas: no device-local image memory");
            return;
        }
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex = type_index;
        if (vkAllocateMemory(device_, &alloc, nullptr, &image_memory) != VK_SUCCESS ||
            vkBindImageMemory(device_, image, image_memory, 0) != VK_SUCCESS) {
            if (image_memory != VK_NULL_HANDLE) {
                vkFreeMemory(device_, image_memory, nullptr);
            }
            vkDestroyImage(device_, image, nullptr);
            vkFreeMemory(device_, staging_memory, nullptr);
            vkDestroyBuffer(device_, staging, nullptr);
            AEGIS_LOG_WARN("[aegis] set_glyph_atlas: image memory bind failed");
            return;
        }
    }

    // 3) One-time staging -> image copy with the right barriers.
    {
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = queue_family_index;
        VkCommandPool pool = VK_NULL_HANDLE;
        if (vkCreateCommandPool(device_, &pool_info, nullptr, &pool) != VK_SUCCESS) {
            vkDestroyImage(device_, image, nullptr);
            vkFreeMemory(device_, image_memory, nullptr);
            vkFreeMemory(device_, staging_memory, nullptr);
            vkDestroyBuffer(device_, staging, nullptr);
            AEGIS_LOG_WARN("[aegis] set_glyph_atlas: transient command pool creation failed");
            return;
        }
        VkCommandBufferAllocateInfo cmd_info{};
        cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_info.commandPool = pool;
        cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_info.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device_, &cmd_info, &cmd) == VK_SUCCESS) {
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &begin);

            VkImageMemoryBarrier to_transfer{};
            to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_transfer.image = image;
            to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            to_transfer.subresourceRange.baseMipLevel = 0;
            to_transfer.subresourceRange.levelCount = 1;
            to_transfer.subresourceRange.baseArrayLayer = 0;
            to_transfer.subresourceRange.layerCount = 1;
            to_transfer.srcAccessMask = 0;
            to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &to_transfer);

            VkBufferImageCopy copy{};
            copy.bufferOffset = 0;
            copy.bufferRowLength = 0;    // tightly packed
            copy.bufferImageHeight = 0;
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = VkExtent3D{width, height, 1};
            vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

            VkImageMemoryBarrier to_sampled = to_transfer;
            to_sampled.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            to_sampled.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            to_sampled.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_sampled.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &to_sampled);
            vkEndCommandBuffer(cmd);

            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &cmd;
            if (vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
                vkQueueWaitIdle(queue) != VK_SUCCESS) {
                AEGIS_LOG_WARN("[aegis] set_glyph_atlas: atlas upload submit failed");
                vkDestroyCommandPool(device_, pool, nullptr);
                vkDestroyImage(device_, image, nullptr);
                vkFreeMemory(device_, image_memory, nullptr);
                vkFreeMemory(device_, staging_memory, nullptr);
                vkDestroyBuffer(device_, staging, nullptr);
                return;
            }
        } else {
            AEGIS_LOG_WARN("[aegis] set_glyph_atlas: command buffer allocation failed");
        }
        vkDestroyCommandPool(device_, pool, nullptr);
    }

    // staging resources are no longer needed
    vkFreeMemory(device_, staging_memory, nullptr);
    vkDestroyBuffer(device_, staging, nullptr);

    // 4) view + linear sampler (linear filtering of 8-bit coverage = smooth edges)
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R8_UNORM;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device_, &view_info, nullptr, &view) != VK_SUCCESS) {
            vkDestroyImage(device_, image, nullptr);
            vkFreeMemory(device_, image_memory, nullptr);
            AEGIS_LOG_WARN("[aegis] set_glyph_atlas: image view creation failed");
            return;
        }
        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.minLod = 0.0f;
        sampler_info.maxLod = 0.0f;
        sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        if (vkCreateSampler(device_, &sampler_info, nullptr, &sampler) != VK_SUCCESS) {
            vkDestroyImageView(device_, view, nullptr);
            vkDestroyImage(device_, image, nullptr);
            vkFreeMemory(device_, image_memory, nullptr);
            AEGIS_LOG_WARN("[aegis] set_glyph_atlas: sampler creation failed");
            return;
        }
    }

    // 5) descriptor pool (exactly one set) + set + write
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &glyph_descriptor_pool_) != VK_SUCCESS) {
        vkDestroySampler(device_, sampler, nullptr);
        vkDestroyImageView(device_, view, nullptr);
        vkDestroyImage(device_, image, nullptr);
        vkFreeMemory(device_, image_memory, nullptr);
        AEGIS_LOG_WARN("[aegis] set_glyph_atlas: descriptor pool creation failed");
        return;
    }
    VkDescriptorSetAllocateInfo set_info{};
    set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_info.descriptorPool = glyph_descriptor_pool_;
    set_info.descriptorSetCount = 1;
    set_info.pSetLayouts = &glyph_set_layout_;
    if (vkAllocateDescriptorSets(device_, &set_info, &glyph_descriptor_set_) != VK_SUCCESS) {
        vkDestroyDescriptorPool(device_, glyph_descriptor_pool_, nullptr);
        glyph_descriptor_pool_ = VK_NULL_HANDLE;
        vkDestroySampler(device_, sampler, nullptr);
        vkDestroyImageView(device_, view, nullptr);
        vkDestroyImage(device_, image, nullptr);
        vkFreeMemory(device_, image_memory, nullptr);
        AEGIS_LOG_WARN("[aegis] set_glyph_atlas: descriptor set allocation failed");
        return;
    }
    VkDescriptorImageInfo image_info{};
    image_info.sampler = sampler;
    image_info.imageView = view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = glyph_descriptor_set_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image_info;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    glyph_image_ = image;
    glyph_image_memory_ = image_memory;
    glyph_image_view_ = view;
    glyph_sampler_ = sampler;
    AEGIS_LOG_INFO("[aegis] font atlas uploaded: " << width << "x" << height << " R8");
}

}  // namespace aegis::renderer2d
