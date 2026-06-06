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

Full `Code/Include` + `Code/Source` from upstream (minus `main.cpp`). Neverwhere `main.cpp` adds:

- `--scene sea|karst|island|all`
- `--smoke-test` (FloatingIsland → `islands.obj`)
- `spdlog` logging, fixed `srand(1234)`

## Not ported yet

- Prebuilt `Objs/` from upstream (optional regression assets)
- VS solution / premake wrappers (replaced by CMake)
- Realtime Sokol viewer (future; rock-fracturing ref already has one)

## Upstream gaps (from repo README)

- Optimized marching cubes
- Hoodoos shape-grammar growth
