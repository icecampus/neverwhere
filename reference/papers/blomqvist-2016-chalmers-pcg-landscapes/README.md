# Blomqvist et al., 2016 — Generating Compelling Procedural 3D Environments and Landscapes

**Type:** Bachelor of Science thesis  
**Institution:** Chalmers University of Technology / University of Gothenburg  
**Date:** June 2016  
**Authors:** Oscar Blomqvist, Pierre Kraft, Hampus Lidin, Rimmer Motzheim, Adam Tonderski, Gabriel Wagner  
**Supervisor:** Staffan Björk

## Local file

- [Blomqvist2016_Generating_Procedural_3D_Environments_Landscapes.pdf](./Blomqvist2016_Generating_Procedural_3D_Environments_Landscapes.pdf)

## Official link

- Chalmers record 244588: https://publications.lib.chalmers.se/records/fulltext/244588/244588.pdf

## What it covers

Open-source **modular PCG engine** (deterministic seed → pipeline of modules):

- **Landmass** generation (continents / islands)
- **Terrain** from heightmaps and **voxel** surfaces (Simplex noise, chunked worlds)
- **Biome interpolation** between attribute sets (plains ↔ mountains)
- **Heightmap → density** conversion for volumetric terrain (relevant when building macro `f` from `h(x,y)`)
- **L-system trees** and **object scattering** on terrain
- Literature survey: heightfields, voxels, fractals, erosion, PCG in games

## Fit for Neverwhere

| Useful | Less relevant |
|--------|----------------|
| Modular pipeline mental model (like our planned `rock_scene/`) | No geological block fracturing (Paris line) |
| Biome blending, chunked terrain | BSc level — not peer-reviewed research |
| Voxel / density terrain basics | Modules were **not fully integrated** in the thesis prototype |
| Game-dev PCG context (indie scope) | No arches / implicit construction trees |

Good **practical companion** to research papers when designing a deterministic playground pipeline (seed → landmass → terrain → detail → foliage).

## Related in [reference/](../../README.md)

- Research cliffs/blocks: [paris-2020-implicit-blocks](../paris-2020-implicit-blocks/), [paris-2019-terrain-amplification](../paris-2019-terrain-amplification/)
- Survey: [surveys-and-classics](../surveys-and-classics/)

## Code

Thesis describes an open-source engine; verify current availability separately (Chalmers thesis — may not be actively maintained on GitHub).
