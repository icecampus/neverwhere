# Paris et al., 2019 — Terrain Amplification with Implicit 3D Features

**Venue:** ACM Transactions on Graphics (SIGGRAPH Asia 2019)  
**Authors:** Axel Paris, Eric Galin, Adrien Peytavie, Eric Guérin, James Gain

## Local file

- [Paris2019_Terrain_Amplification_Implicit_3D_Features.pdf](./Paris2019_Terrain_Amplification_Implicit_3D_Features.pdf) — from [HAL hal-02273097](https://hal.science/hal-02273097v1/file/2019-tog.pdf)

## Official links

- Project page: https://aparis69.github.io/public_html/projects/paris2019_3D.html
- Preprint (remote): https://drive.google.com/file/d/1PiV5D2y8ku4Qw6pUQ9cEyzdrNxNeeIid/view
- ACM: https://dl.acm.org/doi/10.1145/3342765
- Code: https://github.com/aparis69/Implicit-Volumetric-Terrains (MIT, [GRSI replicability stamp](http://www.replicabilitystamp.org/))

## What it covers

- Implicit **construction trees** for geology and landforms
- Amplify a coarse heightfield into **true 3D**: slot canyons, **sea arches**, overhangs, hoodoos, karst caves
- Poisson placement, shape grammars, stratified erosion / invasion percolation
- Compatible with block amplification (cited as foundation for Paris 2020)

## Missing in open-source release

- Optimized marching cubes
- Hoodoos shape-grammar growth process

## Why it matters for Neverwhere

This is the **macro layer `f`** missing from Rock-fracturing alone. Pair with TVC 2020 for `fe = max(f, t)`.

- Code: `src/refs/aparis69-implicit-volumetric-terrains-ref/ivt/`
- Port notes: [../../ports/aparis69-implicit-volumetric-terrains-ref/](../../ports/aparis69-implicit-volumetric-terrains-ref/)

Combined cliff roadmap: [../../ports/aparis69-rock-fracturing-ref/SCENE_EVOLUTION_ROADMAP.md](../../ports/aparis69-rock-fracturing-ref/SCENE_EVOLUTION_ROADMAP.md).
