#include "../../pch.h"

#include "builtins.h"

#include <unordered_map>

namespace pgg {
namespace {

ParamSig val(const char* name, ScalarType base, bool required = false) {
    ParamSig p;
    p.name = name;
    p.base = base;
    p.kind = ParamKind::Value;
    p.required = required;
    return p;
}

ParamSig valDef(const char* name, ScalarType base, Value def) {
    ParamSig p = val(name, base);
    p.defValue = std::move(def);
    p.hasDefValue = true;
    return p;
}

ParamSig geoArg(const char* name, bool required = true) {
    ParamSig p = val(name, ScalarType::Geo, required);
    p.geoKind = GeoKind::Any;
    return p;
}

ParamSig geoArg(const char* name, GeoKind kind) {
    ParamSig p = val(name, ScalarType::Geo, true);
    p.geoKind = kind;
    return p;
}

ParamSig fld(const char* name, ScalarType base, bool required = false) {
    ParamSig p;
    p.name = name;
    p.base = base;
    p.kind = ParamKind::Field;
    p.required = required;
    return p;
}

ParamSig fldDef(const char* name, ScalarType base, Value def) {
    ParamSig p = fld(name, base);
    p.defValue = std::move(def);
    p.hasDefValue = true;
    return p;
}

ParamSig anyEither(const char* name) {
    ParamSig p;
    p.name = name;
    p.base = ScalarType::Any;
    p.kind = ParamKind::Either;
    p.required = true;
    return p;
}

Type geoResult() { return Type{ScalarType::Geo, false, GeoKind::Any}; }
Type fieldResult(ScalarType base) { return Type{base, true, GeoKind::Any}; }

BuiltinSig sig(BuiltinId id, const char* name, std::vector<ParamSig> params, Type result) {
    BuiltinSig s;
    s.id = id;
    s.name = name;
    s.params = std::move(params);
    s.result = result;
    return s;
}

BuiltinSig exprSig(BuiltinId id, const char* name, std::vector<ParamSig> params) {
    BuiltinSig s = sig(id, name, std::move(params), Type{});
    s.exprFunc = true;
    return s;
}

BuiltinSig deferredSig(const char* name, const char* stage) {
    BuiltinSig s;
    s.id = BuiltinId::Deferred;
    s.name = name;
    s.deferredStage = stage;
    return s;
}

const std::vector<BuiltinSig>& registry() {
    static const std::vector<BuiltinSig> kRegistry = [] {
        std::vector<BuiltinSig> r;

        // --- §8.1 sources ---------------------------------------------------
        r.push_back(sig(BuiltinId::IcoSphere, "ico_sphere",
                        {val("subdiv", ScalarType::Int, true), val("radius", ScalarType::F32, true)},
                        Type{ScalarType::Geo, false, GeoKind::Mesh}));
        r.push_back(sig(BuiltinId::Box, "box",
                        {val("size", ScalarType::Vec3, true), valDef("res", ScalarType::Int, Value(int64_t(1)))},
                        Type{ScalarType::Geo, false, GeoKind::Mesh}));
        r.push_back(sig(BuiltinId::Grid, "grid",
                        {val("size", ScalarType::Vec2, true), val("res", ScalarType::Int, true)},
                        Type{ScalarType::Geo, false, GeoKind::Mesh}));
        r.push_back(sig(BuiltinId::MeshLine, "mesh_line",
                        {val("count", ScalarType::Int, true), val("length", ScalarType::F32, true),
                         valDef("dir", ScalarType::Vec3, Value(glm::vec3(0, 0, 1)))},
                        Type{ScalarType::Geo, false, GeoKind::Points}));
        r.push_back(sig(BuiltinId::PointCloud, "point_cloud",
                        {val("count", ScalarType::Int, true), val("bounds", ScalarType::Vec3, true),
                         val("rng", ScalarType::Rng, true)},
                        Type{ScalarType::Geo, false, GeoKind::Points}));

        // --- §8.2 transforms --------------------------------------------------
        {
            BuiltinSig s = sig(BuiltinId::Transform, "transform",
                               {geoArg("geo"), valDef("translate", ScalarType::Vec3, Value(glm::vec3(0.0f))),
                                valDef("rotate", ScalarType::Vec3, Value(glm::vec3(0.0f))),
                                valDef("scale", ScalarType::Vec3, Value(glm::vec3(1.0f)))},
                               geoResult());
            s.resultGeoKindOfFirstArg = true;
            r.push_back(s);
        }
        {
            ParamSig pos = fld("pos", ScalarType::Vec3);
            pos.optional = true;
            BuiltinSig s = sig(BuiltinId::SetPosition, "set_position",
                               {geoArg("geo"), fldDef("offset", ScalarType::Vec3, Value(glm::vec3(0.0f))), pos,
                                fldDef("where", ScalarType::Bool, Value(true))},
                               geoResult());
            s.resultGeoKindOfFirstArg = true;
            r.push_back(s);
        }
        {
            BuiltinSig s = sig(BuiltinId::Smooth, "smooth",
                               {geoArg("geo"), val("iterations", ScalarType::Int, true),
                                valDef("factor", ScalarType::F32, Value(0.5f))},
                               geoResult());
            s.resultGeoKindOfFirstArg = true;
            r.push_back(s);
        }
        {
            ParamSig mode = valDef("mode", ScalarType::String, Value(std::string("smooth")));
            mode.enumValues = {"smooth", "flat", "by_angle"};
            BuiltinSig s = sig(BuiltinId::ComputeNormals, "compute_normals", {geoArg("geo"), mode}, geoResult());
            s.resultGeoKindOfFirstArg = true;
            r.push_back(s);
        }

        // --- §8.5 rng ---------------------------------------------------------
        r.push_back(sig(BuiltinId::RngFromSeed, "rng_from_seed", {val("seed", ScalarType::Int, true)},
                        Type{ScalarType::Rng, false, GeoKind::Any}));
        {
            ParamSig key = val("key", ScalarType::Any, true);
            key.allowString = true;
            r.push_back(sig(BuiltinId::SplitRng, "split_rng",
                            {val("parent", ScalarType::Rng, true), key},
                            Type{ScalarType::Rng, false, GeoKind::Any}));
        }
        r.push_back(sig(BuiltinId::AliasRng, "alias_rng", {val("parent", ScalarType::Rng, true)},
                        Type{ScalarType::Rng, false, GeoKind::Any}));

        // --- §8.5 field generators --------------------------------------------
        {
            ParamSig at = fld("at", ScalarType::Vec3);
            at.defPosition = true;
            r.push_back(sig(BuiltinId::Fbm, "fbm",
                            {at, valDef("scale", ScalarType::F32, Value(1.0f)),
                             valDef("octaves", ScalarType::Int, Value(int64_t(4))), val("rng", ScalarType::Rng, true),
                             valDef("lacunarity", ScalarType::F32, Value(2.0f)),
                             valDef("gain", ScalarType::F32, Value(0.5f))},
                            fieldResult(ScalarType::F32)));
        }
        {
            ParamSig at = fld("at", ScalarType::Vec3);
            at.defPosition = true;
            r.push_back(sig(BuiltinId::Vnoise, "vnoise",
                            {at, valDef("scale", ScalarType::F32, Value(1.0f)), val("rng", ScalarType::Rng, true)},
                            fieldResult(ScalarType::Vec3)));
        }
        {
            ParamSig counter = fld("counter", ScalarType::Int);
            counter.defIndex = true;
            r.push_back(sig(BuiltinId::Random, "random",
                            {val("lo", ScalarType::F32, true), val("hi", ScalarType::F32, true),
                             val("rng", ScalarType::Rng, true), counter},
                            fieldResult(ScalarType::F32)));
        }
        {
            ParamSig counter = fld("counter", ScalarType::Int);
            counter.defIndex = true;
            r.push_back(sig(BuiltinId::RandomVec, "random_vec",
                            {val("lo", ScalarType::Vec3, true), val("hi", ScalarType::Vec3, true),
                             val("rng", ScalarType::Rng, true), counter},
                            fieldResult(ScalarType::Vec3)));
        }
        {
            ParamSig counter = fld("counter", ScalarType::Int);
            counter.defIndex = true;
            r.push_back(sig(BuiltinId::RandomInt, "random_int",
                            {val("n", ScalarType::Int, true), val("rng", ScalarType::Rng, true), counter},
                            fieldResult(ScalarType::Int)));
        }
        r.push_back(sig(BuiltinId::DistanceTo, "distance_to", {geoArg("target")}, fieldResult(ScalarType::F32)));
        r.push_back(sig(BuiltinId::Position, "position", {}, fieldResult(ScalarType::Vec3)));
        r.push_back(sig(BuiltinId::Normal, "normal", {}, fieldResult(ScalarType::Vec3)));
        r.push_back(sig(BuiltinId::Index, "index", {}, fieldResult(ScalarType::Int)));

        // --- §6.3 expression functions ------------------------------------------
        r.push_back(exprSig(BuiltinId::Dot, "dot", {anyEither("a"), anyEither("b")}));
        r.push_back(exprSig(BuiltinId::Cross, "cross", {anyEither("a"), anyEither("b")}));
        r.push_back(exprSig(BuiltinId::Length, "length", {anyEither("x")}));
        r.push_back(exprSig(BuiltinId::Normalize, "normalize", {anyEither("x")}));
        r.push_back(exprSig(BuiltinId::Clamp, "clamp", {anyEither("x"), anyEither("lo"), anyEither("hi")}));
        r.push_back(exprSig(BuiltinId::Smoothstep, "smoothstep", {anyEither("e0"), anyEither("e1"), anyEither("x")}));
        r.push_back(exprSig(BuiltinId::Mix, "mix", {anyEither("a"), anyEither("b"), anyEither("t")}));
        r.push_back(exprSig(BuiltinId::Abs, "abs", {anyEither("x")}));
        r.push_back(exprSig(BuiltinId::Min, "min", {anyEither("a"), anyEither("b")}));
        r.push_back(exprSig(BuiltinId::Max, "max", {anyEither("a"), anyEither("b")}));
        r.push_back(exprSig(BuiltinId::Floor, "floor", {anyEither("x")}));
        r.push_back(exprSig(BuiltinId::Pow, "pow", {anyEither("x"), anyEither("y")}));
        {
            ParamSig x = anyEither("x");
            ParamSig y = anyEither("y");
            y.required = false;
            y.optional = true;
            r.push_back(exprSig(BuiltinId::Vec2, "vec2", {x, y}));
        }
        {
            ParamSig x = anyEither("x");
            ParamSig y = anyEither("y");
            y.required = false;
            y.optional = true;
            ParamSig z = anyEither("z");
            z.required = false;
            z.optional = true;
            r.push_back(exprSig(BuiltinId::Vec3, "vec3", {x, y, z}));
        }
        {
            ParamSig x = anyEither("x");
            ParamSig y = anyEither("y");
            y.required = false;
            y.optional = true;
            ParamSig z = anyEither("z");
            z.required = false;
            z.optional = true;
            ParamSig w = anyEither("w");
            w.required = false;
            w.optional = true;
            r.push_back(exprSig(BuiltinId::Vec4, "vec4", {x, y, z, w}));
        }
        r.push_back(exprSig(BuiltinId::CastInt, "int", {anyEither("x")}));
        r.push_back(exprSig(BuiltinId::CastF32, "f32", {anyEither("x")}));
        r.push_back(exprSig(BuiltinId::CastBool, "bool", {anyEither("x")}));
        r.push_back(exprSig(BuiltinId::OrientFromEuler, "orient_from_euler", {anyEither("rot")}));
        {
            BuiltinSig s = exprSig(BuiltinId::Ramp, "ramp", {anyEither("x")});
            s.variadic = true;
            r.push_back(s);
        }

        // --- §8.6 groups -----------------------------------------------------
        {
            ParamSig domain = valDef("domain", ScalarType::String, Value(std::string("points")));
            domain.enumValues = {"points", "corners", "faces", "detail"};
            BuiltinSig s = sig(BuiltinId::Mark, "mark",
                               {geoArg("geo"), val("name", ScalarType::String, true),
                                fld("where", ScalarType::Bool, true), domain},
                               geoResult());
            s.resultGeoKindOfFirstArg = true;
            r.push_back(s);
        }
        {
            BuiltinSig s = sig(BuiltinId::Unmark, "unmark",
                               {geoArg("geo"), val("name", ScalarType::String, true)}, geoResult());
            s.resultGeoKindOfFirstArg = true;
            r.push_back(s);
        }
        r.push_back(sig(BuiltinId::Ingroup, "ingroup", {val("name", ScalarType::String, true)},
                        fieldResult(ScalarType::Bool)));

        // --- §8.7 attributes --------------------------------------------------
        {
            BuiltinSig s = sig(BuiltinId::SetAttr, "set",
                               {geoArg("geo"), val("name", ScalarType::String, true),
                                fld("value", ScalarType::Any, true)},
                               geoResult());
            s.resultGeoKindOfFirstArg = true;
            r.push_back(s);
        }
        {
            BuiltinSig s = sig(BuiltinId::RemoveAttr, "remove_attr",
                               {geoArg("geo"), val("name", ScalarType::String, true)}, geoResult());
            s.resultGeoKindOfFirstArg = true;
            r.push_back(s);
        }
        {
            BuiltinSig s = sig(BuiltinId::RenameAttr, "rename_attr",
                               {geoArg("geo"), val("old", ScalarType::String, true),
                                val("new", ScalarType::String, true)},
                               geoResult());
            s.resultGeoKindOfFirstArg = true;
            r.push_back(s);
        }
        {
            ParamSig from = val("from", ScalarType::String, true);
            from.enumValues = {"points", "corners", "faces", "detail"};
            ParamSig to = val("to", ScalarType::String, true);
            to.enumValues = {"points", "corners", "faces", "detail"};
            ParamSig mode = valDef("mode", ScalarType::String, Value(std::string("average")));
            mode.enumValues = {"sum", "average", "first"};
            BuiltinSig s = sig(BuiltinId::Promote, "promote",
                               {geoArg("geo"), val("name", ScalarType::String, true), from, to, mode},
                               geoResult());
            s.resultGeoKindOfFirstArg = true;
            r.push_back(s);
        }

        // --- §8.3 merge (E2 subset of topology) ---------------------------------
        {
            BuiltinSig s = sig(BuiltinId::Merge, "merge", {geoArg("a"), geoArg("b")}, geoResult());
            s.resultGeoKindOfFirstArg = true;
            r.push_back(s);
        }

        // --- §8.8 scatter and instancing ----------------------------------------
        {
            ParamSig mode = valDef("mode", ScalarType::String, Value(std::string("poisson")));
            mode.enumValues = {"poisson", "uniform"};
            r.push_back(sig(BuiltinId::DistributePoints, "distribute_points",
                            {geoArg("geo", GeoKind::Mesh), fld("density", ScalarType::F32, true), mode,
                             valDef("min_dist", ScalarType::F32, Value(0.0f)),
                             val("rng", ScalarType::Rng, true)},
                            Type{ScalarType::Geo, false, GeoKind::Points}));
        }
        {
            ParamSig variants = valDef("variants", ScalarType::Geo,
                                       Value(std::make_shared<const std::vector<Value>>()));
            variants.isList = true;
            variants.geoKind = GeoKind::Mesh;
            r.push_back(sig(BuiltinId::InstanceOnPoints, "instance_on_points",
                            {geoArg("pts", GeoKind::Points), geoArg("source", GeoKind::Mesh), variants},
                            Type{ScalarType::Geo, false, GeoKind::Instances}));
        }
        r.push_back(sig(BuiltinId::Realize, "realize", {geoArg("inst", GeoKind::Instances)},
                        Type{ScalarType::Geo, false, GeoKind::Mesh}));

        // --- §8.10 aggregators ---------------------------------------------------
        {
            BuiltinSig s = sig(BuiltinId::Bbox, "bbox", {geoArg("geo")}, Type{});
            s.results = {Type{ScalarType::Vec3, false, GeoKind::Any},
                         Type{ScalarType::Vec3, false, GeoKind::Any}};
            r.push_back(s);
        }
        r.push_back(sig(BuiltinId::Extent, "extent", {geoArg("geo")},
                        Type{ScalarType::Vec3, false, GeoKind::Any}));
        r.push_back(sig(BuiltinId::Centroid, "centroid", {geoArg("geo")},
                        Type{ScalarType::Vec3, false, GeoKind::Any}));
        {
            ParamSig domain = valDef("domain", ScalarType::String, Value(std::string("points")));
            domain.enumValues = {"points", "corners", "faces", "detail"};
            r.push_back(sig(BuiltinId::Count, "count",
                            {geoArg("geo"), domain, fldDef("where", ScalarType::Bool, Value(true))},
                            Type{ScalarType::Int, false, GeoKind::Any}));
        }
        for (BuiltinId id : {BuiltinId::MinOf, BuiltinId::MaxOf, BuiltinId::AvgOf, BuiltinId::SumOf}) {
            const char* n = id == BuiltinId::MinOf ? "min_of"
                          : id == BuiltinId::MaxOf ? "max_of"
                          : id == BuiltinId::AvgOf ? "avg_of"
                                                   : "sum_of";
            r.push_back(sig(id, n,
                            {fld("field", ScalarType::F32, true), geoArg("on"),
                             fldDef("where", ScalarType::Bool, Value(true))},
                            Type{ScalarType::F32, false, GeoKind::Any}));
        }

        // --- §8.4 SDF ----------------------------------------------------------
        {
            Type sdfResult{ScalarType::Sdf, false, GeoKind::Any};
            ParamSig sdfA = val("a", ScalarType::Sdf, true);
            ParamSig sdfB = val("b", ScalarType::Sdf, true);
            r.push_back(sig(BuiltinId::SdfSphere, "sdf_sphere", {val("r", ScalarType::F32, true)}, sdfResult));
            r.push_back(sig(BuiltinId::SdfBox, "sdf_box", {val("size", ScalarType::Vec3, true)}, sdfResult));
            r.push_back(sig(BuiltinId::SdfUnion, "sdf_union", {sdfA, sdfB}, sdfResult));
            r.push_back(sig(BuiltinId::SdfUnionSmooth, "sdf_union_smooth",
                            {sdfA, sdfB, val("k", ScalarType::F32, true)}, sdfResult));
            r.push_back(sig(BuiltinId::SdfSubtract, "sdf_subtract", {sdfA, sdfB}, sdfResult));
            r.push_back(sig(BuiltinId::SdfSubtractSmooth, "sdf_subtract_smooth",
                            {sdfA, sdfB, val("k", ScalarType::F32, true)}, sdfResult));
            r.push_back(sig(BuiltinId::SdfIntersect, "sdf_intersect", {sdfA, sdfB}, sdfResult));
            r.push_back(sig(BuiltinId::SdfDisplace, "sdf_displace",
                            {val("s", ScalarType::Sdf, true), fld("amount", ScalarType::F32, true)}, sdfResult));
            r.push_back(sig(BuiltinId::SdfInstanceOnPoints, "sdf_instance_on_points",
                            {geoArg("pts", GeoKind::Points), val("source", ScalarType::Sdf, true),
                             valDef("k", ScalarType::F32, Value(0.0f))},
                            sdfResult));
            r.push_back(sig(BuiltinId::SdfFromMesh, "sdf_from_mesh",
                            {geoArg("geo"), val("voxel", ScalarType::F32, true)}, sdfResult));
            {
                ParamSig method = valDef("method", ScalarType::String, Value(std::string("marching_cubes")));
                method.enumValues = {"marching_cubes"};
                r.push_back(sig(BuiltinId::MeshFromSdf, "mesh_from_sdf",
                                {val("s", ScalarType::Sdf, true), val("voxel", ScalarType::F32, true),
                                 valDef("iso", ScalarType::F32, Value(0.0f)), method},
                                Type{ScalarType::Geo, false, GeoKind::Mesh}));
            }
        }

        // --- known but deferred past this stage ---------------------------------
        r.push_back(deferredSig("import_mesh", "deferred: no host asset contract yet, Q4"));
        r.push_back(deferredSig("subdivide", "topology ops are a later stage (post-E4)"));
        r.push_back(deferredSig("triangulate", "topology ops are a later stage (post-E4)"));
        r.push_back(deferredSig("merge_by_distance", "topology ops are a later stage (post-E4)"));
        r.push_back(deferredSig("delete", "topology ops are a later stage (post-E4)"));
        r.push_back(deferredSig("separate", "topology ops are a later stage (post-E4)"));
        r.push_back(deferredSig("islands", "topology ops are a later stage (post-E4)"));
        for (const char* n : {"raycast", "transfer"})
            r.push_back(deferredSig(n, "sampling ops are a later stage (post-E4)"));
        r.push_back(deferredSig("fracture", "fracture is stage E7"));
        return r;
    }();
    return kRegistry;
}

}  // namespace

const BuiltinSig* findBuiltin(const std::string& name) {
    static const std::unordered_map<std::string, const BuiltinSig*> kIndex = [] {
        std::unordered_map<std::string, const BuiltinSig*> m;
        for (const BuiltinSig& s : registry()) m.emplace(s.name, &s);
        return m;
    }();
    auto it = kIndex.find(name);
    return it == kIndex.end() ? nullptr : it->second;
}

bool bindCallArgs(const BuiltinSig& sig, const Call& call,
                  std::vector<const CallArg*>& outByParam,
                  std::vector<const CallArg*>& outVariadic,
                  std::vector<Diagnostic>& diags) {
    const size_t np = sig.params.size();
    outByParam.assign(np, nullptr);
    outVariadic.clear();
    bool ok = true;
    size_t nextPositional = 0;
    for (const CallArg& arg : call.args) {
        if (!arg.hasName) {
            if (nextPositional >= np) {
                if (sig.variadic) {
                    outVariadic.push_back(&arg);
                    continue;
                }
                diags.push_back(Diagnostic{"E202", call.span,
                                           "too many arguments for '" + std::string(sig.name) + "'",
                                           "the operation takes " + std::to_string(np) + " argument(s)", false});
                ok = false;
                continue;
            }
            outByParam[nextPositional++] = &arg;
            continue;
        }
        size_t idx = np;
        for (size_t i = 0; i < np; ++i)
            if (sig.params[i].name == arg.name) idx = i;
        if (idx == np) {
            std::string names;
            for (size_t i = 0; i < np; ++i) names += (i ? ", " : "") + sig.params[i].name;
            diags.push_back(Diagnostic{"E203", call.span,
                                       "unknown parameter '" + arg.name + "' of '" + std::string(sig.name) + "'",
                                       "parameters: " + names, false});
            ok = false;
            continue;
        }
        if (outByParam[idx]) {
            diags.push_back(Diagnostic{"E202", call.span,
                                       "argument '" + arg.name + "' of '" + std::string(sig.name) + "' is passed twice",
                                       {}, false});
            ok = false;
            continue;
        }
        outByParam[idx] = &arg;
        if (idx >= nextPositional) nextPositional = idx + 1;
    }
    for (size_t i = 0; i < np; ++i) {
        const ParamSig& p = sig.params[i];
        if (p.required && !outByParam[i]) {
            diags.push_back(Diagnostic{"E202", call.span,
                                       "missing required argument '" + p.name + "' of '" + std::string(sig.name) + "'",
                                       "pass " + p.name + " = ...", false});
            ok = false;
        }
    }
    return ok;
}

Value evalBuiltinCall(const BoundCall& bound, RunContext& run) {
    const BuiltinSig& sig = *bound.sig;
    const std::vector<Value>& v = bound.values;
    switch (sig.id) {
        case BuiltinId::IcoSphere:
            return Value(genIcoSphere(static_cast<int>(asInt(v[0])), asF32(v[1])));
        case BuiltinId::Box:
            return Value(genBox(asVec3(v[0]), static_cast<int>(asInt(v[1]))));
        case BuiltinId::Grid:
            return Value(genGrid(asVec2(v[0]), static_cast<int>(asInt(v[1]))));
        case BuiltinId::MeshLine:
            return Value(genMeshLine(static_cast<int>(asInt(v[0])), asF32(v[1]), asVec3(v[2])));
        case BuiltinId::PointCloud:
            return Value(genPointCloud(static_cast<int>(asInt(v[0])), asVec3(v[1]), asRng(v[2])));
        case BuiltinId::RngFromSeed:
            return Value(rngFromSeed(asInt(v[0])));
        case BuiltinId::SplitRng: {
            const Value& key = v[1];
            if (valueBase(key) == ScalarType::String)
                return Value(splitRng(asRng(v[0]), asString(key)));
            return Value(splitRng(asRng(v[0]), asInt(key)));
        }
        case BuiltinId::AliasRng:
            return Value(aliasRng(asRng(v[0])));
        case BuiltinId::Transform:
        case BuiltinId::SetPosition:
        case BuiltinId::Smooth:
        case BuiltinId::ComputeNormals:
            return evalTransformBuiltin(bound, run);
        case BuiltinId::Mark:
        case BuiltinId::Unmark:
        case BuiltinId::SetAttr:
        case BuiltinId::RemoveAttr:
        case BuiltinId::RenameAttr:
        case BuiltinId::Promote:
            return evalAttrBuiltin(bound, run);
        case BuiltinId::Merge:
        case BuiltinId::DistributePoints:
        case BuiltinId::InstanceOnPoints:
        case BuiltinId::Realize:
            return evalScatterBuiltin(bound, run);
        case BuiltinId::Bbox:
        case BuiltinId::Extent:
        case BuiltinId::Centroid:
        case BuiltinId::Count:
        case BuiltinId::MinOf:
        case BuiltinId::MaxOf:
        case BuiltinId::AvgOf:
        case BuiltinId::SumOf:
            return evalAggregateBuiltin(bound, run);
        case BuiltinId::SdfSphere:
        case BuiltinId::SdfBox:
        case BuiltinId::SdfUnion:
        case BuiltinId::SdfUnionSmooth:
        case BuiltinId::SdfSubtract:
        case BuiltinId::SdfSubtractSmooth:
        case BuiltinId::SdfIntersect:
        case BuiltinId::SdfDisplace:
        case BuiltinId::SdfInstanceOnPoints:
        case BuiltinId::SdfFromMesh:
        case BuiltinId::MeshFromSdf:
            return evalSdfBuiltin(bound, run);
        default:
            break;
    }
    if (sig.exprFunc) {
        // Constant folding: 1-element buffers through the same per-element
        // implementation as the field path. values holds only the supplied
        // arguments (plus the variadic tail), so vec broadcasts see the true
        // arity; everything past the declared params is the variadic tail.
        std::vector<ConstBufferPtr> args;
        const size_t np = std::min(sig.params.size(), v.size());
        for (size_t i = 0; i < np; ++i) args.push_back(makeConstBuffer(v[i], 1));
        std::vector<Value> params(v.begin() + static_cast<ptrdiff_t>(np), v.end());
        ConstBufferPtr out = evalExprFuncBuf(static_cast<int>(sig.id), args, params, 1, run.threads);
        return bufferValueAt(*out, 0);
    }
    (void)run;
    return Value();
}

}  // namespace pgg
