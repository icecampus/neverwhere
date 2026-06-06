# Surveys and classic techniques

Link-only references for context. Not all have free PDFs or open code.

## Surveys

| Work | Link | Notes |
|------|------|-------|
| **Galin et al., 2019** — *A review of digital terrain modeling* (Eurographics STAR) | Local: [Galin2019_Review_Digital_Terrain_Modeling_STAR.pdf](./Galin2019_Review_Digital_Terrain_Modeling_STAR.pdf) · Author: https://perso.liris.cnrs.fr/egalin/Articles/2019-star.pdf | Map of heightfields, voxels, layers, implicit terrains |
| Paris 2019 TOG §2 | [../paris-2019-terrain-amplification/](../paris-2019-terrain-amplification/) | Focused related-work for 3D landforms |

## Classic papers (cited by LIRIS line)

| Work | Link | Technique |
|------|------|-----------|
| **Musgrave et al., 1989** — *The synthesis and rendering of eroded fractal terrains* | SIGGRAPH 1989 (ACM DL) | Fractal erosion foundation |
| **Gamito & Musgrave, 2001** — *Procedural landscapes with overhangs* | Portuguese CG meeting; cited in Paris 2019/2020 | Warp heightfields for overhangs |
| **Blomqvist et al., 2016** — *Generating Compelling Procedural 3D Environments and Landscapes* (Chalmers BSc) | [../blomqvist-2016-chalmers-pcg-landscapes/](../blomqvist-2016-chalmers-pcg-landscapes/) | Modular game PCG: biomes, voxels, heightmap→density |
| **Ito et al., 2003** — voxel joint fracturing | Cited in Paris 2019 | Break voxel links along joints; costly |
| **Becher et al.** — convolution / layer-stack surfaces | Cited as [22] in Paris 2020 | Large-scale terrain; Fig. 13 comparison |
| **Cordonnier et al., 2016** — tectonic uplift + fluvial erosion | CGF 2016 | Large mountains/rivers, not block detail |

## Implementation classics (no geology blocks)

| Work | Link | Technique |
|------|------|-----------|
| **Lorensen & Cline, 1987** — Marching Cubes | Standard reference | Used in all Paris repos |
| **NVIDIA GPU Gems 3, Ch. 1** — Procedural terrains on GPU | https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-1-generating-complex-procedural-terrains-using-gpu | SDF density + MC, caves |
| **Paul Bourke** — polygonise | http://paulbourke.net/geometry/polygonise/ | MC tables |

## Theses

| Work | Link | Notes |
|------|------|-------|
| **Gamito** — *Techniques for Stochastic Implicit Surface Modelling and Rendering* | https://staffwww.dcs.shef.ac.uk/people/S.Maddock/phd_theses/gamito/thesis_small.pdf | Planets, implicit surfaces, overhangs |
