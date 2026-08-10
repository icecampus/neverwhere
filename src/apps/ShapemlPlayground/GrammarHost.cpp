#include "pch.h"

#include "GrammarHost.h"

#include <spdlog/spdlog.h>

#include <shapeml/exporter.h>
#include <shapeml/grammar.h>
#include <shapeml/interpreter.h>
#include <shapeml/parser/parser.h>
#include <shapeml/shape.h>

namespace shapemlhost {

namespace {

Param paramFromValue(const std::string& name, const shapeml::Value& v) {
    Param p;
    p.name = name;
    switch (v.type) {
    case shapeml::ValueType::BOOL:
        p.type = Param::Type::Boolean;
        p.b = v.b;
        break;
    case shapeml::ValueType::INT:
        p.type = Param::Type::Int;
        p.i = v.i;
        break;
    case shapeml::ValueType::FLOAT:
        p.type = Param::Type::Number;
        p.f = v.f;
        break;
    default:
        p.type = Param::Type::String;
        p.s = (v.type == shapeml::ValueType::STRING && v.s) ? *v.s : "";
        break;
    }
    return p;
}

shapeml::Value valueFromParam(const Param& p) {
    switch (p.type) {
    case Param::Type::Boolean: return shapeml::Value(p.b);
    case Param::Type::Int: return shapeml::Value(p.i);
    case Param::Type::Number: return shapeml::Value(p.f);
    default: return shapeml::Value(p.s);
    }
}

} // namespace

struct GrammarHost::Impl {
    std::unique_ptr<shapeml::Grammar> grammar;
    std::unique_ptr<shapeml::Shape> lastRoot;
    std::string path;
    std::string basePath;
    std::vector<Param> params;
};

GrammarHost::GrammarHost() : m(std::make_unique<Impl>()) {}
GrammarHost::~GrammarHost() = default;

bool GrammarHost::load(const std::string& path, std::string* error) {
    auto grammar = std::make_unique<shapeml::Grammar>();

    // NB: their Parse() returns true ON ERROR and fills base_path/file_name
    // itself (asset paths in grammars resolve relative to the grammar file).
    shapeml::parser::Parser parser;
    if (parser.Parse(path, grammar.get())) {
        if (error) {
            *error = "parse failed: " + path;
        }
        return false;
    }

    std::vector<Param> params;
    for (const std::string& name : grammar->parameter_ordering()) {
        const auto it = grammar->parameters().find(name);
        if (it != grammar->parameters().end()) {
            params.push_back(paramFromValue(name, it->second));
        }
    }

    m->grammar = std::move(grammar);
    m->lastRoot.reset();
    m->path = path;
    m->basePath = m->grammar->base_path();
    m->params = std::move(params);
    return true;
}

const std::string& GrammarHost::path() const { return m->path; }
const std::string& GrammarHost::basePath() const { return m->basePath; }
const std::vector<Param>& GrammarHost::params() const { return m->params; }

void GrammarHost::setParams(const std::vector<Param>& params) {
    m->params = params;
}

bool GrammarHost::derive(unsigned seed, DerivedModel* out, std::string* error) {
    if (!m->grammar) {
        if (error) {
            *error = "no grammar loaded";
        }
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();

    shapeml::ValueDict paramDict;
    for (const Param& p : m->params) {
        paramDict.emplace(p.name, valueFromParam(p));
    }

    shapeml::Shape* root = nullptr;
    try {
        shapeml::Interpreter& interp = shapeml::Interpreter::Get();
        // Same as ShapeMaker: a world-sized octree is always provided,
        // occlusion-aware grammars (octreeAdd) crash without it.
        shapeml::geometry::Octree octree(shapeml::Vec3::Zero(), 1000.0);
        root = interp.Init(m->grammar.get(), "Axiom", seed, &paramDict, &octree);
        if (root && !interp.Derive(root, 1000)) {
            delete root;
            root = nullptr;
        }
    } catch (const std::exception& e) {
        delete root;
        if (error) {
            *error = std::string("derivation error: ") + e.what();
        }
        return false;
    }
    if (!root) {
        if (error) {
            *error = "derivation failed (init or derive returned null)";
        }
        return false;
    }
    m->lastRoot.reset(root);

    // Collect render geometry from the terminal leaves (same flow as their
    // exporter): unit-space mesh buffers are cached per shared mesh, then
    // transformed into world space via MeshWorldTrafo/MeshNormalMatrix.
    struct MeshBuffer {
        std::vector<shapeml::geometry::RenderVertex> verts;
        shapeml::IdxVec indices;
    };
    std::unordered_map<const shapeml::geometry::HalfedgeMesh*, MeshBuffer>
        meshCache;
    struct Bucket {
        std::string texturePath;
        std::vector<Vertex> verts;
        std::vector<std::uint32_t> indices;
    };
    std::vector<Bucket> buckets;
    std::unordered_map<std::string, size_t> bucketByTexture;

    shapeml::LeafConstVisitor leafVisitor;
    root->AcceptVisitor(&leafVisitor);

    int leafCount = 0;
    for (const shapeml::Shape* s : leafVisitor.leaves()) {
        if (!s->terminal() || !s->visible() || !s->mesh() || s->mesh()->Empty()) {
            continue;
        }
        ++leafCount;

        const shapeml::geometry::HalfedgeMesh* meshKey = s->mesh().get();
        auto itMesh = meshCache.find(meshKey);
        if (itMesh == meshCache.end()) {
            MeshBuffer buf;
            s->mesh()->FillRenderBuffer(&buf.verts, &buf.indices);
            itMesh = meshCache.emplace(meshKey, std::move(buf)).first;
        }
        const MeshBuffer& buf = itMesh->second;

        std::string texPath;
        if (!s->material().texture.empty()) {
            texPath = (std::filesystem::path(m->basePath) /
                s->material().texture).lexically_normal().string();
        }
        auto itBucket = bucketByTexture.find(texPath);
        if (itBucket == bucketByTexture.end()) {
            Bucket b;
            b.texturePath = texPath;
            buckets.push_back(std::move(b));
            itBucket = bucketByTexture.emplace(texPath, buckets.size() - 1).first;
        }
        Bucket& bucket = buckets[itBucket->second];

        const shapeml::Affine3 trafo = s->MeshWorldTrafo();
        const shapeml::Mat3 normalMat = s->MeshNormalMatrix();
        const shapeml::Vec4 color = s->material().color;

        const auto baseVertex =
            static_cast<std::uint32_t>(bucket.verts.size());
        bucket.verts.reserve(bucket.verts.size() + buf.verts.size());
        for (const shapeml::geometry::RenderVertex& v : buf.verts) {
            const shapeml::Vec3 p =
                trafo * shapeml::Vec3(v.position[0], v.position[1], v.position[2]);
            const shapeml::Vec3 n =
                (normalMat * shapeml::Vec3(v.normal[0], v.normal[1], v.normal[2]))
                    .normalized();
            Vertex out_v;
            out_v.pos[0] = static_cast<float>(p.x());
            out_v.pos[1] = static_cast<float>(p.y());
            out_v.pos[2] = static_cast<float>(p.z());
            out_v.nrm[0] = static_cast<float>(n.x());
            out_v.nrm[1] = static_cast<float>(n.y());
            out_v.nrm[2] = static_cast<float>(n.z());
            out_v.uv[0] = v.uv[0];
            out_v.uv[1] = v.uv[1];
            for (int c = 0; c < 4; ++c) {
                out_v.color[c] = static_cast<float>(color[c]);
            }
            bucket.verts.push_back(out_v);
        }
        bucket.indices.reserve(bucket.indices.size() + buf.indices.size());
        for (const std::uint32_t idx : buf.indices) {
            bucket.indices.push_back(baseVertex + idx);
        }
    }

    // Flatten buckets into one VB/IB pair with per-bucket draw ranges.
    DerivedModel model;
    for (const Bucket& bucket : buckets) {
        if (bucket.indices.empty()) {
            continue;
        }
        const auto baseVertex =
            static_cast<std::uint32_t>(model.vertices.size());
        DrawRange range;
        range.firstIndex = static_cast<int>(model.indices.size());
        range.indexCount = static_cast<int>(bucket.indices.size());
        range.texturePath = bucket.texturePath;
        model.draws.push_back(std::move(range));
        model.vertices.insert(model.vertices.end(), bucket.verts.begin(),
            bucket.verts.end());
        for (const std::uint32_t idx : bucket.indices) {
            model.indices.push_back(baseVertex + idx);
        }
    }
    model.leafCount = leafCount;

    shapeml::AABBVisitor aabbVisitor;
    root->AcceptVisitor(&aabbVisitor);
    if (leafCount > 0) {
        const shapeml::geometry::AABB box = aabbVisitor.aabb();
        const shapeml::Vec3 lo = box.center - box.extent;
        const shapeml::Vec3 hi = box.center + box.extent;
        for (int c = 0; c < 3; ++c) {
            model.aabbMin[c] = static_cast<float>(lo[c]);
            model.aabbMax[c] = static_cast<float>(hi[c]);
        }
    }

    model.deriveMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    *out = std::move(model);
    return true;
}

bool GrammarHost::exportObj(const std::string& file, std::string* error) const {
    if (!m->lastRoot) {
        if (error) {
            *error = "nothing derived yet";
        }
        return false;
    }
    try {
        shapeml::Exporter exporter(m->lastRoot.get(), shapeml::ExportType::OBJ,
            file, m->basePath, true);
        (void)exporter;
    } catch (const std::exception& e) {
        if (error) {
            *error = std::string("export error: ") + e.what();
        }
        return false;
    }
    return true;
}

std::vector<std::string> GrammarHost::scanGrammars(const std::string& dir) {
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".shp") {
            files.push_back(entry.path().filename().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace shapemlhost
