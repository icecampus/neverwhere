#include "pch.h"

#include "LandscapeCellCatalog.h"

#include <cmath>

namespace landscape3d {

namespace {

using landscape_mesh::ColorRgba;
using landscape_mesh::MeshBoundarySegment;
using landscape_mesh::MeshBuildSettings;
using landscape_mesh::MeshQuad;
using landscape_mesh::Vec3;

struct Point {
    float x = 0.0f;
    float z = 0.0f;
};

constexpr std::array<Point, 4> kCorners{{
    {0.0f, 1.0f}, // Left
    {0.0f, 0.0f}, // Up
    {1.0f, 0.0f}, // Right
    {1.0f, 1.0f}, // Down
}};

constexpr ColorRgba kLowlandColor{81, 132, 78, 255};
constexpr ColorRgba kHighgroundColor{112, 165, 87, 255};

Point midpoint(Point lhs, Point rhs) {
    return {(lhs.x + rhs.x) * 0.5f, (lhs.z + rhs.z) * 0.5f};
}

Vec3 pointAtHeight(Point point, float height) {
    return {point.x, height, point.z};
}

void appendTopQuad(
    LandscapeCellTemplate& result,
    Vec3 a,
    Vec3 b,
    Vec3 c,
    Vec3 d,
    ColorRgba color) {

    MeshQuad quad;
    quad.a = a;
    quad.b = b;
    quad.c = c;
    quad.d = d;
    quad.normal = {0.0f, 1.0f, 0.0f};
    quad.color = color;
    quad.cliffWall = false;
    quad.heightFraction = 1.0f;
    result.quads.push_back(quad);
    result.topQuadCount++;
}

void appendTopPolygon(
    LandscapeCellTemplate& result,
    const std::vector<Point>& polygon,
    float height) {

    if (polygon.size() < 3) {
        return;
    }

    const Vec3 first = pointAtHeight(polygon[0], height);
    for (std::size_t index = 1; index + 1 < polygon.size(); ++index) {
        const Vec3 second = pointAtHeight(polygon[index], height);
        const Vec3 third = pointAtHeight(polygon[index + 1], height);
        appendTopQuad(result, first, second, third, third, kHighgroundColor);
    }
}

Vec3 outwardNormalForContour(const std::vector<Point>& polygon, Point start, Point end) {
    Point centroid{};
    for (const Point point : polygon) {
        centroid.x += point.x;
        centroid.z += point.z;
    }
    centroid.x /= (float)polygon.size();
    centroid.z /= (float)polygon.size();

    const Point middle = midpoint(start, end);
    float x = middle.x - centroid.x;
    float z = middle.z - centroid.z;
    const float length = std::sqrt(x * x + z * z);
    if (length <= 0.0001f) {
        return {0.0f, 0.0f, 1.0f};
    }
    return {x / length, 0.0f, z / length};
}

void appendCliffWall(
    LandscapeCellTemplate& result,
    Point start,
    Point end,
    const std::vector<Point>& highPolygon,
    const LandscapeCellCatalogSettings& catalogSettings) {

    MeshBuildSettings meshSettings;
    meshSettings.cellSize = 1.0f;
    meshSettings.levelHeight = catalogSettings.highgroundHeight;
    meshSettings.cornerBevel = 0.0f;
    meshSettings.rockEnabled = false;
    meshSettings.rockAmplitude = 0.0f;
    meshSettings.wallHorizontalSubdivisions = catalogSettings.wallHorizontalSubdivisions;
    meshSettings.wallVerticalSubdivisions = catalogSettings.wallVerticalSubdivisions;
    meshSettings.terraceSteps = 0;
    meshSettings.wallStyle = landscape_mesh::WallStyleId::BlockCliff;

    const Vec3 outward = outwardNormalForContour(highPolygon, start, end);
    MeshBoundarySegment segment;
    segment.a = pointAtHeight(start, 0.0f);
    segment.b = pointAtHeight(end, 0.0f);
    segment.normal = outward;
    segment.startNormal = outward;
    segment.endNormal = outward;
    segment.side = landscape_core::EdgeSide::Top;

    std::vector<MeshQuad> walls = landscape_mesh::buildWallQuadsFromBoundarySegment(
        segment,
        0.0f,
        catalogSettings.highgroundHeight,
        false,
        0.0f,
        meshSettings);

    result.wallQuadCount += (int)walls.size();
    result.quads.insert(result.quads.end(), walls.begin(), walls.end());
}

std::array<bool, 4> maskForBits(int bits) {
    return {
        (bits & 0b0001) != 0,
        (bits & 0b0010) != 0,
        (bits & 0b0100) != 0,
        (bits & 0b1000) != 0,
    };
}

} // namespace

void LandscapeCellCatalog::rebuild(const LandscapeCellCatalogSettings& settings) {
    m_settings = settings;
    m_settings.highgroundHeight = std::max(0.1f, m_settings.highgroundHeight);
    m_settings.wallHorizontalSubdivisions = std::clamp(m_settings.wallHorizontalSubdivisions, 1, 16);
    m_settings.wallVerticalSubdivisions = std::clamp(m_settings.wallVerticalSubdivisions, 1, 16);

    m_templates = {};
    for (int bits = 0; bits < 16; ++bits) {
        const std::array<bool, 4> mask = maskForBits(bits);
        const landscape_core::LandscapeTileType type = landscape_core::nodeMaskToTileType(mask);
        m_templates[(std::size_t)type] = buildTemplate(type, mask);
    }

    m_valid = true;
    for (const LandscapeCellTemplate& entry : m_templates) {
        if (entry.quads.empty()) {
            m_valid = false;
            break;
        }
    }
}

const LandscapeCellTemplate& LandscapeCellCatalog::templateFor(landscape_core::LandscapeTileType type) const {
    return m_templates[(std::size_t)type];
}

LandscapeCellTemplate LandscapeCellCatalog::buildTemplate(
    landscape_core::LandscapeTileType type,
    const std::array<bool, 4>& mask) const {

    LandscapeCellTemplate result;
    result.type = type;
    result.nodeMask = mask;

    appendTopQuad(
        result,
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
        kLowlandColor);

    const int highCount = (int)std::count(mask.begin(), mask.end(), true);
    if (highCount == 0) {
        return result;
    }

    if (highCount == 4) {
        appendTopQuad(
            result,
            {0.0f, m_settings.highgroundHeight, 0.0f},
            {1.0f, m_settings.highgroundHeight, 0.0f},
            {1.0f, m_settings.highgroundHeight, 1.0f},
            {0.0f, m_settings.highgroundHeight, 1.0f},
            kHighgroundColor);
        return result;
    }

    for (int firstHigh = 0; firstHigh < 4; ++firstHigh) {
        const int previous = (firstHigh + 3) % 4;
        if (!mask[(std::size_t)firstHigh] || mask[(std::size_t)previous]) {
            continue;
        }

        std::vector<int> run;
        int current = firstHigh;
        while (mask[(std::size_t)current]) {
            run.push_back(current);
            current = (current + 1) % 4;
        }

        const int lastHigh = run.back();
        const int after = (lastHigh + 1) % 4;
        const Point start = midpoint(kCorners[(std::size_t)previous], kCorners[(std::size_t)firstHigh]);
        const Point end = midpoint(kCorners[(std::size_t)lastHigh], kCorners[(std::size_t)after]);

        std::vector<Point> polygon;
        polygon.reserve(run.size() + 2);
        polygon.push_back(start);
        for (const int corner : run) {
            polygon.push_back(kCorners[(std::size_t)corner]);
        }
        polygon.push_back(end);

        appendTopPolygon(result, polygon, m_settings.highgroundHeight);
        appendCliffWall(result, start, end, polygon, m_settings);
    }

    return result;
}

} // namespace landscape3d
