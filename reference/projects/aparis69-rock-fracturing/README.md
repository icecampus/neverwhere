# aparis69 / Rock-fracturing

**Repository:** https://github.com/aparis69/Rock-fracturing  
**License:** MIT  
**Paper:** [Paris 2020 TVC](../papers/paris-2020-implicit-blocks/)

## Summary

Console app exporting **four OBJ tiles** (one per fracture type). Implements Poisson sampling, fracture generation, clustering, block SDF + BVH, gradient warping, marching cubes (200³).

## Included in upstream

- `Code/Source/blocks.cpp`, `blocks-sdf.cpp`, `main.cpp`
- Headers: basics, blocks, MC, convhull, noise, vec, stb

## Not in upstream (paper only)

- Implicit replication operator
- Periodic / aperiodic tiling
- Some full paper scenes

## Neverwhere

Ported to `src/refs/aparis69-rock-fracturing-ref/rock_fracture/` with interactive viewer and async rebuild.

Analysis: [../../neverwhere/aparis69-rock-fracturing-ref/rock-fracturing-upstream.md](../../neverwhere/aparis69-rock-fracturing-ref/rock-fracturing-upstream.md)

Pinned commit (gap analysis baseline): `b91965b3011ed269cbc3a051b00c9b284aaa2e36`
