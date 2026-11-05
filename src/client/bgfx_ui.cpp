#include "client/bgfx_ui.h"

#include <bx/math.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

namespace nsync::client {
namespace {

struct UiVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
};

constexpr std::uint64_t kUiTextureFlags =
    BGFX_SAMPLER_U_CLAMP |
    BGFX_SAMPLER_V_CLAMP |
    BGFX_SAMPLER_MIP_POINT;

} // namespace

BgfxUiRenderer::BgfxUiRenderer(
    bgfx::ProgramHandle program,
    bgfx::UniformHandle sampler,
    int width,
    int height)
    : program_(program), sampler_(sampler), width_(width), height_(height) {
    layout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    const std::uint32_t white = 0xFFFFFFFF;
    white_texture_ = bgfx::createTexture2D(
        1, 1, false, 1, bgfx::TextureFormat::RGBA8,
        kUiTextureFlags,
        bgfx::copy(&white, sizeof(white)));
}

BgfxUiRenderer::~BgfxUiRenderer() {
    shutdown();
}

void BgfxUiRenderer::set_dimensions(int width, int height) {
    width_ = width;
    height_ = height;
}

void BgfxUiRenderer::shutdown() {
    if (bgfx::isValid(white_texture_)) {
        bgfx::destroy(white_texture_);
        white_texture_ = BGFX_INVALID_HANDLE;
    }
}

Rml::CompiledGeometryHandle BgfxUiRenderer::CompileGeometry(
    Rml::Span<const Rml::Vertex> vertices,
    Rml::Span<const int> indices) {
    if (vertices.empty() || indices.empty()) {
        return 0;
    }
    std::vector<UiVertex> converted(vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const Rml::Vertex& source = vertices[i];
        UiVertex& target = converted[i];
        target.x = source.position.x;
        target.y = source.position.y;
        target.z = 0.0f;
        target.u = source.tex_coord.x;
        target.v = source.tex_coord.y;
        target.r = source.colour.red;
        target.g = source.colour.green;
        target.b = source.colour.blue;
        target.a = source.colour.alpha;
    }

    auto* resource = new GeometryResource();
    resource->index_count = static_cast<std::uint32_t>(indices.size());
    resource->vertex_buffer = bgfx::createVertexBuffer(
        bgfx::copy(converted.data(), static_cast<std::uint32_t>(converted.size() * sizeof(UiVertex))),
        layout_);
    resource->index_buffer = bgfx::createIndexBuffer(
        bgfx::copy(indices.data(), static_cast<std::uint32_t>(indices.size() * sizeof(int))),
        BGFX_BUFFER_INDEX32);

    if (!bgfx::isValid(resource->vertex_buffer) || !bgfx::isValid(resource->index_buffer)) {
        if (bgfx::isValid(resource->vertex_buffer)) {
            bgfx::destroy(resource->vertex_buffer);
        }
        if (bgfx::isValid(resource->index_buffer)) {
            bgfx::destroy(resource->index_buffer);
        }
        delete resource;
        return 0;
    }

    return reinterpret_cast<Rml::CompiledGeometryHandle>(resource);
}

void BgfxUiRenderer::RenderGeometry(
    Rml::CompiledGeometryHandle geometry,
    Rml::Vector2f translation,
    Rml::TextureHandle texture) {
    auto* resource = reinterpret_cast<GeometryResource*>(geometry);
    if (resource == nullptr ||
        !bgfx::isValid(resource->vertex_buffer) ||
        !bgfx::isValid(resource->index_buffer)) {
        return;
    }

    float translation_matrix[16];
    bx::mtxTranslate(translation_matrix, translation.x, translation.y, 0.0f);

    if (has_transform_) {
        float model[16];
        bx::mtxMul(model, translation_matrix, transform_.data());
        bgfx::setTransform(model);
    } else {
        bgfx::setTransform(translation_matrix);
    }

    bgfx::setState(
        BGFX_STATE_WRITE_RGB |
        BGFX_STATE_WRITE_A |
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));

    if (scissor_enabled_) {
        const int x = std::clamp(scissor_region_.Left(), 0, std::max(width_ - 1, 0));
        const int y = std::clamp(scissor_region_.Top(), 0, std::max(height_ - 1, 0));
        const int w = std::clamp(scissor_region_.Width(), 1, std::max(width_ - x, 1));
        const int h = std::clamp(scissor_region_.Height(), 1, std::max(height_ - y, 1));
        bgfx::setScissor(
            static_cast<std::uint16_t>(x),
            static_cast<std::uint16_t>(y),
            static_cast<std::uint16_t>(w),
            static_cast<std::uint16_t>(h));
    } else {
        bgfx::setScissor(0, 0, static_cast<std::uint16_t>(width_), static_cast<std::uint16_t>(height_));
    }

    bgfx::TextureHandle texture_handle = white_texture_;
    if (texture != 0) {
        auto* texture_resource = reinterpret_cast<TextureResource*>(texture);
        if (bgfx::isValid(texture_resource->texture)) {
            texture_handle = texture_resource->texture;
        }
    }

    bgfx::setTexture(0, sampler_, texture_handle);
    bgfx::setVertexBuffer(0, resource->vertex_buffer);
    bgfx::setIndexBuffer(resource->index_buffer);
    bgfx::submit(1, program_);
}

void BgfxUiRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
    auto* resource = reinterpret_cast<GeometryResource*>(geometry);
    if (resource == nullptr) {
        return;
    }
    if (bgfx::isValid(resource->vertex_buffer)) {
        bgfx::destroy(resource->vertex_buffer);
    }
    if (bgfx::isValid(resource->index_buffer)) {
        bgfx::destroy(resource->index_buffer);
    }
    delete resource;
}

Rml::TextureHandle BgfxUiRenderer::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) {
    texture_dimensions = {};
    std::fprintf(stderr, "RmlUi LoadTexture is not implemented yet: %s\n", source.c_str());
    return 0;
}

Rml::TextureHandle BgfxUiRenderer::GenerateTexture(
    Rml::Span<const Rml::byte> source,
    Rml::Vector2i source_dimensions) {
    if (source.empty() || source_dimensions.x <= 0 || source_dimensions.y <= 0) {
        return 0;
    }

    auto* resource = new TextureResource();
    resource->texture = bgfx::createTexture2D(
        static_cast<std::uint16_t>(source_dimensions.x),
        static_cast<std::uint16_t>(source_dimensions.y),
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        kUiTextureFlags,
        bgfx::copy(source.data(), static_cast<std::uint32_t>(source.size())));

    if (!bgfx::isValid(resource->texture)) {
        delete resource;
        return 0;
    }

    return reinterpret_cast<Rml::TextureHandle>(resource);
}

void BgfxUiRenderer::ReleaseTexture(Rml::TextureHandle texture) {
    auto* resource = reinterpret_cast<TextureResource*>(texture);
    if (resource == nullptr) {
        return;
    }
    if (bgfx::isValid(resource->texture)) {
        bgfx::destroy(resource->texture);
    }
    delete resource;
}

void BgfxUiRenderer::EnableScissorRegion(bool enable) {
    scissor_enabled_ = enable;
}

void BgfxUiRenderer::SetScissorRegion(Rml::Rectanglei region) {
    scissor_region_ = region;
}

void BgfxUiRenderer::SetTransform(const Rml::Matrix4f* transform) {
    if (transform == nullptr) {
        has_transform_ = false;
        return;
    }
    transform_ = *transform;
    has_transform_ = true;
}

} // namespace nsync::client
