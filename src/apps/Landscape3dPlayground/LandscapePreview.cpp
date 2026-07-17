#include "pch.h"

#include "LandscapePreview.h"

#include <cmath>

#include <glm/geometric.hpp>

#include <render_core/image_loader.h>
#include <spdlog/spdlog.h>

namespace landscape3d {

namespace {

constexpr float kIsoScaleX = 19.0f;
constexpr float kIsoScaleY = 9.5f;
constexpr float kHeightScale = 25.0f;

bool looksLikeDataRoot(const std::filesystem::path& directory) {
    std::error_code error;
    return std::filesystem::exists(
               directory / "src" / "apps" / "SplattingPlayground" / "resources" / "materials" / "grass.png",
               error) &&
        std::filesystem::exists(directory / "resources" / "textures" / "rock_3.jpg", error);
}

std::filesystem::path findDataRootUpwards(std::filesystem::path startDirectory) {
    std::error_code error;
    startDirectory = std::filesystem::weakly_canonical(startDirectory, error);
    if (startDirectory.empty()) {
        startDirectory = std::filesystem::current_path(error);
    }

    for (int level = 0; level < 16 && !startDirectory.empty(); ++level) {
        if (looksLikeDataRoot(startDirectory)) {
            return startDirectory;
        }
        const std::filesystem::path parent = startDirectory.parent_path();
        if (parent == startDirectory) {
            break;
        }
        startDirectory = parent;
    }
    return {};
}

glm::vec3 toGlm(const landscape_mesh::Vec3& value) {
    return {value.x, value.y, value.z};
}

glm::vec4 toGlm(const landscape_mesh::ColorRgba& value) {
    return {
        (float)value.r / 255.0f,
        (float)value.g / 255.0f,
        (float)value.b / 255.0f,
        (float)value.a / 255.0f,
    };
}

float projectionAnchorY(float height) {
    return std::min(170.0f, std::max(96.0f, height * 0.34f));
}

glm::vec2 projectWorld(
    const LandscapeModel& model,
    const LandscapePreviewSettings& settings,
    glm::vec2 viewportSize,
    glm::vec3 point) {

    const float centeredX = point.x - (float)model.width() * 0.5f;
    const float centeredZ = point.z - (float)model.height() * 0.5f;
    return {
        viewportSize.x * 0.5f + settings.pan.x + (centeredX - centeredZ) * kIsoScaleX * settings.zoom,
        projectionAnchorY(viewportSize.y) + settings.pan.y +
            ((centeredX + centeredZ) * kIsoScaleY - point.y * kHeightScale) * settings.zoom,
    };
}

} // namespace

void LandscapePreview::init() {
    initTextures();
    m_renderer.init();
}

void LandscapePreview::shutdown() {
    m_renderer.shutdown();
    if (m_materialSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_materialSampler);
        m_materialSampler.id = SG_INVALID_ID;
    }
    destroyTexture(m_grass);
    destroyTexture(m_rock);
    m_renderedQuadCount = 0;
}

bool LandscapePreview::render(
    const LandscapeModel& model,
    const LandscapeCellCatalog& catalog,
    const LandscapePreviewSettings& settings,
    int width,
    int height) {

    if (width <= 0 || height <= 0 || m_grass.view.id == SG_INVALID_ID ||
        m_rock.view.id == SG_INVALID_ID || m_materialSampler.id == SG_INVALID_ID) {
        return false;
    }

    m_highgroundHeight = catalog.settings().highgroundHeight;
    const std::vector<render_core::MeshPreviewQuad> quads = buildRenderQuads(model, catalog);
    m_renderedQuadCount = (int)quads.size();
    return m_renderer.render(
        quads,
        makeRenderParams(model, settings, {(float)width, (float)height}),
        m_grass.view,
        m_rock.view,
        m_materialSampler,
        width,
        height);
}

std::optional<GridPoint> LandscapePreview::pickNode(
    const LandscapeModel& model,
    const LandscapePreviewSettings& settings,
    glm::vec2 viewportSize,
    glm::vec2 cursorPosition) const {

    const float radius = std::clamp(11.0f * settings.zoom, 8.0f, 22.0f);
    const float maxDistanceSquared = radius * radius;
    float nearestDistanceSquared = maxDistanceSquared;
    std::optional<GridPoint> result;

    for (int y = 0; y <= model.height(); ++y) {
        for (int x = 0; x <= model.width(); ++x) {
            const GridPoint node{x, y};
            const glm::vec2 point = projectNode(model, settings, viewportSize, node);
            const glm::vec2 delta = point - cursorPosition;
            const float distanceSquared = glm::dot(delta, delta);
            if (distanceSquared < nearestDistanceSquared) {
                nearestDistanceSquared = distanceSquared;
                result = node;
            }
        }
    }
    return result;
}

glm::vec2 LandscapePreview::projectNode(
    const LandscapeModel& model,
    const LandscapePreviewSettings& settings,
    glm::vec2 viewportSize,
    GridPoint node) const {

    const float height = model.nodeIsHigh(node) ? m_highgroundHeight : 0.0f;
    return projectWorld(model, settings, viewportSize, {(float)node.x, height, (float)node.y});
}

void LandscapePreview::initTextures() {
    static constexpr std::array<std::uint8_t, 16> grassFallback{{
        88, 137, 73, 255,
        108, 158, 86, 255,
        99, 151, 80, 255,
        76, 125, 68, 255,
    }};
    static constexpr std::array<std::uint8_t, 16> rockFallback{{
        104, 99, 88, 255,
        126, 119, 104, 255,
        82, 78, 72, 255,
        112, 106, 96, 255,
    }};

    const std::filesystem::path root = findDataRootUpwards(std::filesystem::current_path());
    const std::filesystem::path grassPath =
        root / "src" / "apps" / "SplattingPlayground" / "resources" / "materials" / "grass.png";
    const std::filesystem::path rockPath = root / "resources" / "textures" / "rock_3.jpg";
    m_grassLoaded = loadTexture(grassPath, grassFallback, m_grass);
    m_rockLoaded = loadTexture(rockPath, rockFallback, m_rock);

    sg_sampler_desc sampler{};
    sampler.min_filter = SG_FILTER_LINEAR;
    sampler.mag_filter = SG_FILTER_LINEAR;
    sampler.wrap_u = SG_WRAP_REPEAT;
    sampler.wrap_v = SG_WRAP_REPEAT;
    sampler.label = "landscape3d-template-material-sampler";
    m_materialSampler = sg_make_sampler(&sampler);
}

void LandscapePreview::destroyTexture(Texture& texture) {
    if (texture.view.id != SG_INVALID_ID) {
        sg_destroy_view(texture.view);
    }
    if (texture.image.id != SG_INVALID_ID) {
        sg_destroy_image(texture.image);
    }
    texture = {};
}

bool LandscapePreview::loadTexture(
    const std::filesystem::path& path,
    const std::array<std::uint8_t, 16>& fallback,
    Texture& texture) {

    destroyTexture(texture);

    render_core::ImageRGBA8 image;
    bool loaded = false;
    try {
        image = render_core::loadImageRGBA8(path);
        loaded = true;
    } catch (const std::exception&) {
        image.width = 2;
        image.height = 2;
        image.pixels.assign(fallback.begin(), fallback.end());
        spdlog::warn("Landscape3dPlayground: material '{}' is unavailable; using fallback", path.string());
    }

    sg_image_desc imageDescription{};
    imageDescription.width = image.width;
    imageDescription.height = image.height;
    imageDescription.pixel_format = SG_PIXELFORMAT_RGBA8;
    imageDescription.data.mip_levels[0].ptr = image.pixels.data();
    imageDescription.data.mip_levels[0].size = image.pixels.size();
    imageDescription.label = loaded ? "landscape3d-template-material" : "landscape3d-template-fallback";
    texture.image = sg_make_image(&imageDescription);
    if (texture.image.id == SG_INVALID_ID) {
        return false;
    }

    sg_view_desc viewDescription{};
    viewDescription.texture.image = texture.image;
    texture.view = sg_make_view(&viewDescription);
    if (texture.view.id == SG_INVALID_ID) {
        destroyTexture(texture);
        return false;
    }
    return loaded;
}

std::vector<render_core::MeshPreviewQuad> LandscapePreview::buildRenderQuads(
    const LandscapeModel& model,
    const LandscapeCellCatalog& catalog) const {

    std::vector<render_core::MeshPreviewQuad> result;
    result.reserve((std::size_t)model.width() * (std::size_t)model.height() * 8);

    for (int y = 0; y < model.height(); ++y) {
        for (int x = 0; x < model.width(); ++x) {
            const LandscapeCellTemplate& cellTemplate = catalog.templateFor(model.cellTypeAt({x, y}));
            for (const landscape_mesh::MeshQuad& source : cellTemplate.quads) {
                render_core::MeshPreviewQuad quad;
                const glm::vec3 offset{(float)x, 0.0f, (float)y};
                quad.a = toGlm(source.a) + offset;
                quad.b = toGlm(source.b) + offset;
                quad.c = toGlm(source.c) + offset;
                quad.d = toGlm(source.d) + offset;
                quad.normal = toGlm(source.normal);
                quad.color = toGlm(source.color);
                quad.faceKind = source.cliffWall ? 1.0f : 0.0f;
                quad.cliffDistance = source.cliffWall ? 0.0f : 1000.0f;
                quad.relief = source.relief;
                quad.heightFraction = source.heightFraction;
                quad.sunShadow = 1.0f;

                if (source.cliffWall) {
                    quad.uvA = {quad.a.x * 0.35f + quad.a.z * 0.17f, -quad.a.y * 0.42f};
                    quad.uvB = {quad.b.x * 0.35f + quad.b.z * 0.17f, -quad.b.y * 0.42f};
                    quad.uvC = {quad.c.x * 0.35f + quad.c.z * 0.17f, -quad.c.y * 0.42f};
                    quad.uvD = {quad.d.x * 0.35f + quad.d.z * 0.17f, -quad.d.y * 0.42f};
                } else {
                    quad.uvA = {quad.a.x * 0.5f, quad.a.z * 0.5f};
                    quad.uvB = {quad.b.x * 0.5f, quad.b.z * 0.5f};
                    quad.uvC = {quad.c.x * 0.5f, quad.c.z * 0.5f};
                    quad.uvD = {quad.d.x * 0.5f, quad.d.z * 0.5f};
                }

                const glm::vec3 center = (quad.a + quad.b + quad.c + quad.d) * 0.25f;
                quad.depth = center.x + center.z - center.y * 0.25f;
                result.push_back(quad);
            }
        }
    }
    return result;
}

render_core::MeshPreviewRenderParams LandscapePreview::makeRenderParams(
    const LandscapeModel& model,
    const LandscapePreviewSettings& settings,
    glm::vec2 viewportSize) const {

    render_core::MeshPreviewRenderParams params;
    params.worldCenter = {(float)model.width() * 0.5f, (float)model.height() * 0.5f};
    params.pan = settings.pan;
    params.lightDirection = glm::normalize(settings.lightDirection);
    params.zoom = settings.zoom;
    params.anchorY = projectionAnchorY(viewportSize.y);
    params.isoScaleX = kIsoScaleX;
    params.isoScaleY = kIsoScaleY;
    params.heightScale = kHeightScale;
    params.ambient = settings.ambient;
    params.diffuse = settings.diffuseStrength;
    params.wallBrightness = settings.wallBrightness;
    params.textureScale = settings.textureScale;
    params.cliffDarkeningRadius = settings.cliffDarkeningRadius;
    params.cliffDarkeningStrength = settings.cliffDarkeningStrength;
    params.minTopBrightness = settings.minTopBrightness;
    params.edgeDarkness = settings.edgeDarkness;
    params.wallAoStrength = settings.wallAoStrength;
    params.wallEdgeWearStrength = settings.wallEdgeWearStrength;
    params.wallCreviceStrength = settings.wallCreviceStrength;
    params.wallGrainStrength = settings.wallGrainStrength;
    return params;
}

} // namespace landscape3d
