#pragma once

#include <aegis/export.h>

#include <volk.h>

#include <aegis/core/noncopyable.hpp>
#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>
#include <aegis/memory/gpu_buffer.hpp>
#include <aegis/vk/device.hpp>

#include <array>
#include <vector>

#include <aegis/renderer2d/glyph_instance.hpp>
#include <aegis/renderer2d/line_instance.hpp>
#include <aegis/renderer2d/part_instance.hpp>
#include <aegis/renderer2d/sprite_instance.hpp>

namespace aegis::renderer2d {

/// Draw pipelines owned by the batch renderer. Particle uses additive blending
/// (glow/energy); Sprite, Line, UI, Glyph and Part use standard alpha blending.
/// Line is a specialized world-space segment pipeline (orbit paths, velocity
/// vectors); its instances are bit-compatible with SpriteInstance and share the
/// same persistently mapped instance buffers. Glyph is the bitmap-text pipeline
/// (no textures; the 5x7 rows travel inside the instance, see glyph_instance.hpp).
/// Part is the procedural part pipeline (SDF silhouettes, see part_instance.hpp)
/// and uses its own 56-byte instance ring.
enum class PrimitiveKind : core::u32 { Sprite = 0, Particle = 1, UI = 2, Line = 3, Glyph = 4, Part = 5 };

[[nodiscard]] AEGIS_API const char* primitive_kind_name(PrimitiveKind kind) noexcept;

struct AEGIS_API BatchStats {
    /// Flushed in the most recent flush() call (per rendered frame when called once per frame).
    core::u32 draw_calls = 0;
    core::u64 instance_count = 0;
    core::u64 sprite_instances = 0;
    core::u64 particle_instances = 0;
    core::u64 ui_instances = 0;
    core::u64 line_instances = 0;
    core::u64 glyph_instances = 0;
    core::u64 part_instances = 0;
    /// Accumulated over the renderer's lifetime (never reset).
    core::u64 lifetime_draw_calls = 0;
    core::u64 lifetime_instances = 0;
};

struct AEGIS_API BatchRendererConfig {
    /// Hard cap on instanced quads per frame and per instance class
    /// (the 40-byte class: Sprite/Particle/UI/Line/Glyph — and the 56-byte
    /// Part class gets its own ring of the same size).
    core::u32 max_instances = 8192;
    /// Number of frames-in-flight; one persistently mapped instance buffer is kept per slot.
    core::u32 max_frames_in_flight = 2;
};

/// Universal instanced 2D batch renderer.
///
/// Design:
/// - No vertex buffers. The six quad corners are derived from gl_VertexIndex
///   in the shader; every per-draw datum is a per-instance attribute
///   (location 0..2, binding 0, stride sizeof(SpriteInstance)).
/// - One persistently mapped HOST_VISIBLE|COHERENT instance buffer per
///   frames-in-flight slot. append() writes instances with no per-instance GPU
///   traffic and no allocation.
/// - Batches are split by PrimitiveKind. draw_*() calls append to the open
///   batch; a kind change (or a full buffer, or flush()) closes it. Painter's
///   order is preserved *within* consecutive same-kind draws; across kinds the
///   renderer draws batches in first-use order — call flush() yourself between
///   kind groups if you need strict cross-kind ordering control.
/// - Pipelines are created with dynamic viewport/scissor and Vulkan dynamic
///   rendering (no render pass). Pipelines are immutable after creation.
/// - Lines are a specialized kind: one instance per world-space segment
///   (LineInstance), expanded to a filled quad in the line vertex shader —
///   GL line topology is not used (GL lineWidth is ignored on most drivers).
/// - Stats are accumulated per flush() and exposed via stats(); lifetime
///   counters survive for the renderer's lifetime.
///
/// Threading: all methods must be called from the thread that owns the
/// command buffer recording (typically the render thread).
class AEGIS_API BatchRenderer : public core::NonCopyable {
public:
    BatchRenderer() = default;
    ~BatchRenderer();
    BatchRenderer(BatchRenderer&& other) noexcept;
    BatchRenderer& operator=(BatchRenderer&& other) noexcept;

    /// Creates the renderer: shaders are loaded from the AEGIS_SPV_DIR baked in
    /// at build time, four pipelines are created (sprite / additive / UI / line),
    /// and max_frames_in_flight persistent instance buffers are allocated and mapped.
    /// Stride of the shared 40-byte instance class (Sprite/Particle/UI/Line/Glyph).
    static constexpr core::u32 kInstanceStride40 = sizeof(SpriteInstance);
    /// Stride of the Part instance class.
    static constexpr core::u32 kInstanceStride56 = sizeof(PartInstance);

    [[nodiscard]] static core::Result<BatchRenderer> create(
        VkDevice device,
        const VkPhysicalDeviceMemoryProperties& memory_properties,
        VkFormat color_format,
        const BatchRendererConfig& config = {});

    /// Begins recording a frame. `projection` is a column-major mat4
    /// (see make_orthographic()); `extent` drives the dynamic viewport/scissor
    /// state set during flush().
    void begin_frame(VkCommandBuffer cmd,
                     core::u32 frame_index,
                     const core::f32 projection[16],
                     VkExtent2D extent);

    void draw_sprite(const SpriteInstance& instance);
    void draw_particle(const SpriteInstance& instance);
    void draw_ui(const SpriteInstance& instance);
    void draw_line(const LineInstance& instance);
    void draw_glyph(const GlyphInstance& instance);
    void draw_part(const PartInstance& instance);
    /// Uploads the CPU-baked font coverage atlas (R8) as the glyph pipeline's
    /// set-0 texture. Call once, after create() and before the first frame
    /// that draws text. `pixels` is copied synchronously (blocking upload).
    /// Until this is called, glyph batches are skipped with a one-time warning
    /// (text is invisible) — everything else renders normally.
    void set_glyph_atlas(VkQueue queue, core::u32 queue_family_index, const core::u8* pixels,
                         core::u32 width, core::u32 height);
    /// Convenience overload. `thickness` is in WORLD units (use
    /// Camera2D::world_per_pixel() * desired_px for constant screen width).
    void draw_line(core::f32 ax, core::f32 ay, core::f32 bx, core::f32 by,
                   core::f32 r, core::f32 g, core::f32 b, core::f32 a,
                   core::f32 thickness, core::f32 softness);

    /// Closes the open batch and records viewport/scissor + one draw per
    /// closed batch into the bound command buffer (call inside dynamic
    /// rendering). Flushes automatically when the instance buffer fills up.
    void flush();

    [[nodiscard]] const BatchStats& stats() const noexcept { return stats_; }
    [[nodiscard]] const BatchRendererConfig& config() const noexcept { return config_; }
    [[nodiscard]] bool frame_started() const noexcept { return cmd_ != VK_NULL_HANDLE; }

private:
    struct Batch {
        PrimitiveKind kind = PrimitiveKind::Sprite;
        core::u32 first_instance = 0;  ///< index into the class ring (stride-aligned)
        core::u32 instance_count = 0;
    };

    enum class BlendMode : core::u32 { Normal = 0, Additive = 1 };

    void append(PrimitiveKind kind, const void* instance_bytes, core::u32 instance_bytes_size);
    void close_open_batch();
    core::Result<void> create_pipelines();
    core::Result<VkShaderModule> load_shader_module(const char* spv_filename) const;
    VkPipeline create_pipeline(VkShaderModule vert, VkShaderModule frag, BlendMode blend,
                               core::u32 instance_stride,
                               const VkVertexInputAttributeDescription* attributes,
                               core::u32 attribute_count) const;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkFormat color_format_ = VK_FORMAT_UNDEFINED;
    BatchRendererConfig config_{};

    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    std::array<VkPipeline, 6> pipelines_{};  // indexed by PrimitiveKind
    VkDescriptorSetLayout glyph_set_layout_ = VK_NULL_HANDLE;  ///< set 0: sampler2D coverage atlas
    VkImage glyph_image_ = VK_NULL_HANDLE;
    VkDeviceMemory glyph_image_memory_ = VK_NULL_HANDLE;
    VkImageView glyph_image_view_ = VK_NULL_HANDLE;
    VkSampler glyph_sampler_ = VK_NULL_HANDLE;
    VkDescriptorPool glyph_descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet glyph_descriptor_set_ = VK_NULL_HANDLE;  ///< VK_NULL until set_glyph_atlas()
    VkPhysicalDeviceMemoryProperties memory_properties_{};
    bool glyph_atlas_warned_ = false;  ///< one-shot log guard

    std::vector<memory::GpuBuffer> instance_buffers_;      // 40-byte class
    std::vector<memory::GpuBuffer> part_instance_buffers_; // 56-byte class

    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    core::u32 frame_index_ = 0;
    core::u32 append_index_ = 0;       // 40-byte class cursor
    core::u32 append_part_index_ = 0;  // 56-byte class cursor
    std::array<core::f32, 16> projection_{};
    VkExtent2D extent_{};

    PrimitiveKind open_kind_ = PrimitiveKind::Sprite;
    bool open_batch_valid_ = false;
    core::u32 open_batch_first_ = 0;
    core::u32 open_batch_count_ = 0;
    std::vector<Batch> batches_;
    bool overflow_warned_ = false;  ///< one-shot warn guard (instance buffer full / outside frame)

    BatchStats stats_{};
    core::u64 lifetime_draw_calls_ = 0;
    core::u64 lifetime_instances_ = 0;
};

}  // namespace aegis::renderer2d
