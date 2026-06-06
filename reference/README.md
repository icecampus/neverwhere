# Reference library — procedural cliffs, rocks, and 3D terrain

Curated papers, open-source projects, and Neverwhere-specific notes for **volumetric cliffs**, **implicit blocks**, **arches/overhangs**, and related geology-inspired generation.

## Papers (local PDFs)

| Folder | Paper | Role |
|--------|-------|------|
| [papers/paris-2019-terrain-amplification](./papers/paris-2019-terrain-amplification/) | Paris et al., TOG 2019 — *Terrain Amplification with Implicit 3D Features* | **Macro:** arches, karst, hoodoos, construction trees, heightfield → 3D |
| [papers/paris-2020-implicit-blocks](./papers/paris-2020-implicit-blocks/) | Paris et al., TVC 2020 — *Modeling Rocky Scenery using Implicit Blocks* | **Meso/micro:** fractured blocks, gradient warping, replication (paper only) |
| [papers/peytavie-2009-arches](./papers/peytavie-2009-arches/) | Peytavie et al., CGF 2009 — *Arches: a Framework for Modelling Complex Terrains* | **Conceptual ancestor:** material layers + implicit sculpting |
| [papers/blomqvist-2016-chalmers-pcg-landscapes](./papers/blomqvist-2016-chalmers-pcg-landscapes/) | Blomqvist et al., Chalmers BSc 2016 — *Generating Procedural 3D Environments and Landscapes* | **Practical PCG:** modular pipeline, biomes, heightmap→voxel density |
| [papers/surveys-and-classics](./papers/surveys-and-classics/) | Galin STAR 2019 (local PDF) + classic paper links | Terrain modeling survey & citations |

**Recommended reading order for “paper-quality sea cliffs” (Fig. 11–14):** 2009 Arches (background) → 2019 TOG (`f`, landforms) → 2020 TVC (`bi`, blocks, `fe = max(f,t)`).

## Open-source projects (links + notes)

| Folder | Upstream |
|--------|----------|
| [projects/aparis69-rock-fracturing](./projects/aparis69-rock-fracturing/) | https://github.com/aparis69/Rock-fracturing |
| [projects/aparis69-implicit-volumetric-terrains](./projects/aparis69-implicit-volumetric-terrains/) | https://github.com/aparis69/Implicit-Volumetric-Terrains |
| [projects/acfaruk-proc-rock](./projects/acfaruk-proc-rock/) | https://github.com/acfaruk/proc-rock |
| [projects/instant-organic-caves](./projects/instant-organic-caves/) | https://github.com/gregorik/InstantOrganicCaves |
| [projects/unity-procedural-rock](./projects/unity-procedural-rock/) | https://github.com/przemyslawzaworski/Unity-Procedural-Rock-Generation |
| [projects/jc-g-mountains](./projects/jc-g-mountains/) | https://github.com/JC-G/Mountains |

## Neverwhere implementation notes

| Folder | Content |
|--------|---------|
| [neverwhere/aparis69-rock-fracturing-ref](./neverwhere/aparis69-rock-fracturing-ref/) | Gap analysis vs upstream, scene evolution roadmap, `upstream.meta.json` |

Ported algorithm code lives in `src/refs/aparis69-rock-fracturing-ref/rock_fracture/` (not duplicated here).

Related apps in the monorepo (no separate reference folder yet):

- `src/apps/MeshGenerationPlayground` — FastNoise cliff silhouette prototype
- `src/libs/landscape_mesh` — heightmap cliff walls for the map editor
- `src/apps/Landscape3dPlayground` — 3D terrain preview

## LIRIS research line (same authors)

Most cliff/block papers share authors **Paris, Peytavie, Galin, Guérin** (Univ. Lyon, LIRIS):

- Project pages: https://aparis69.github.io/public_html/projects/
- Peytavie publications: https://perso.liris.cnrs.fr/apeytavi/website/publication/

Open releases are **recoded subsets** of internal “Arches” platform code — always treat GitHub as partial, not identical to paper figures.
