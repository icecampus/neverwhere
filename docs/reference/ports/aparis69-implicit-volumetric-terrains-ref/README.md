# aparis69-implicit-volumetric-terrains-ref — implementation notes

Neverwhere port of [aparis69/Implicit-Volumetric-Terrains](https://github.com/aparis69/Implicit-Volumetric-Terrains) (Paris 2019 TOG).

| Document | Description |
|----------|-------------|
| [upstream.meta.json](./upstream.meta.json) | Machine-readable upstream metadata |

**Algorithm code:** `src/refs/aparis69-implicit-volumetric-terrains-ref/ivt/`

**Papers & external projects:**

- [Paris 2019 TOG PDF](../../papers/paris-2019-terrain-amplification/)
- [Upstream project notes](../../projects/aparis69-implicit-volumetric-terrains/)
- [Rock-fracturing port (2020 TVC)](../aparis69-rock-fracturing-ref/) — micro blocks layer `t` for `fe = max(f, t)`

## What was ported

Full `Code/Include` + `Code/Source` from upstream (minus `main.cpp`). Neverwhere adds:

- **Interactive Sokol + ImGui viewer** (default launch)
- Async rebuild via `IvtScene` (`Build*TerrainTree()` + marching cubes)
- GPU mesh viewport with depth-tested grid
- `--batch-export` / `--scene sea|karst|island|all` (upstream `XxxScene()` entry points)
- `--smoke-test` (all batch scenes → `islands.obj`, `karst.obj`, `sea.obj`; sea@350³ is slow)
- `spdlog` logging; batch path fixes `std::srand(1234)` like upstream

## Dual scene entry points (intentional)

Upstream `*-scene.cpp` files keep both APIs:

| API | Used by | MC resolution |
|-----|---------|---------------|
| `Build*TerrainTree()` | Viewer (`IvtScene`) | User-controlled (`mcResolution`, clamp 24–350) |
| `SeaScene()` / `KarstScene()` / `FloatingIsland()` | `BatchExport.cpp` | Hardcoded upstream: 350 / 200 / 100 |

`ivt_scenes.h` exposes only `Build*TerrainTree()` for the viewer. Batch export forward-declares `XxxScene()` directly in `BatchExport.cpp`.

## Known gaps vs upstream

| Topic | Status |
|-------|--------|
| Batch `srand(1234)` | Matches upstream; viewer uses configurable `seed` per rebuild |
| Viewer MC defaults | Lower than batch (80 / 160 / 120) for interactive latency; slider allows up to 350 |
| `--smoke-test` | Validates all three batch OBJ exports (>64 bytes each) via isolated subprocess per scene; sea@350³ can take several minutes |
| Prebuilt `Objs/` | Not vendored; optional regression assets |
| Optimized MC / hoodoos grammar | Not in upstream repo either |

## Upstream gaps (from repo README)

- Optimized marching cubes
- Hoodoos shape-grammar growth
