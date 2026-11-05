#pragma once

#include <RmlUi/Core.h>
#include <bgfx/bgfx.h>

#include <cstdint>

namespace nsync::client {

class BgfxUiRenderer final : public Rml::RenderInterface {
public:
    BgfxUiRenderer(bgfx::ProgramHandle program, bgfx::UniformHandle sampler, int width, int height);
    ~BgfxUiRenderer() override;

    BgfxUiRenderer(const BgfxUiRenderer&) = delete;
    BgfxUiRenderer& operator=(const BgfxUiRenderer&) = delete;

    Rml::CompiledGeometryHandle CompileGeometry(
        Rml::Span<const Rml::Vertex> vertices,
        Rml::Span<const int> indices) override;
    void RenderGeometry(
        Rml::CompiledGeometryHandle geometry,
        Rml::Vector2f translation,
        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(
        Rml::Span<const Rml::byte> source,
        Rml::Vector2i source_dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;
    void SetTransform(const Rml::Matrix4f* transform) override;

    void set_dimensions(int width, int height);
    void shutdown();

private:
    struct GeometryResource {
        bgfx::VertexBufferHandle vertex_buffer = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle index_buffer = BGFX_INVALID_HANDLE;
        std::uint32_t index_count = 0;
    };

    struct TextureResource {
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    };

    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle white_texture_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout_;

    int width_ = 0;
    int height_ = 0;
    bool scissor_enabled_ = false;
    Rml::Rectanglei scissor_region_{};
    bool has_transform_ = false;
    Rml::Matrix4f transform_{};
};

} // namespace nsync::client
