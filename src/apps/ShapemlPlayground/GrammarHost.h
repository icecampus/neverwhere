// Bridge to the ShapeML core (grammar parse -> derive -> flat render buffers).
// The only place in this playground that sees shapeml headers; the rest of the
// app works with the plain structs below. Derivation is main-thread only
// (Interpreter is a singleton), called between frames.
#pragma once

namespace shapemlhost {

// Editable copy of one grammar parameter (`param` declarations in .shp).
struct Param {
    // NB: "Bool" is an X11 macro (#define Bool int) leaking from sokol_app.h,
    // so the enum value is spelled "Boolean".
    enum class Type { Boolean, Int, Number, String };

    std::string name;
    Type type = Type::Number;
    bool b = false;
    int i = 0;
    double f = 0.0;
    std::string s;

    bool operator==(const Param& other) const {
        return name == other.name && type == other.type && b == other.b &&
            i == other.i && f == other.f && s == other.s;
    }
};

struct Vertex {
    float pos[3];
    float nrm[3];
    float uv[2];
    float color[4];
};

// One draw call range inside DerivedModel::indices, grouped by texture.
struct DrawRange {
    int firstIndex = 0;
    int indexCount = 0;
    std::string texturePath; // absolute; empty = plain color material
};

struct DerivedModel {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<DrawRange> draws;
    float aabbMin[3] = {0.0f, 0.0f, 0.0f};
    float aabbMax[3] = {0.0f, 0.0f, 0.0f};
    int leafCount = 0;
    double deriveMs = 0.0;
};

class GrammarHost {
public:
    GrammarHost();
    ~GrammarHost();
    GrammarHost(const GrammarHost&) = delete;
    GrammarHost& operator=(const GrammarHost&) = delete;

    // Parses the .shp file. On success params() returns the grammar defaults.
    bool load(const std::string& path, std::string* error);

    const std::string& path() const;
    const std::string& basePath() const;
    const std::vector<Param>& params() const;
    void setParams(const std::vector<Param>& params);

    // Full derivation with the current params. On success the model is ready
    // for upload and exportObj() can be called.
    bool derive(unsigned seed, DerivedModel* out, std::string* error);

    // Exports the last derived model (their Exporter, merge_vertices = true).
    bool exportObj(const std::string& file, std::string* error) const;

    // *.shp files in dir (sorted, file names only, no path).
    static std::vector<std::string> scanGrammars(const std::string& dir);

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace shapemlhost
