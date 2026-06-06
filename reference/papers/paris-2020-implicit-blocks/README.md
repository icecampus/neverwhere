# Paris et al., 2020 — Modeling Rocky Scenery using Implicit Blocks

**Venue:** The Visual Computer (TVC), 2020 · CGI 2020  
**Authors:** Axel Paris, Adrien Peytavie, Eric Guérin, Jean-Michel Dischler, Eric Galin

## Local file

- [Paris2020_Modeling_Rocky_Scenery_Implicit_Blocks.pdf](./Paris2020_Modeling_Rocky_Scenery_Implicit_Blocks.pdf)

## Official links

- Project page: https://aparis69.github.io/public_html/projects/paris2020_Blocks.html
- Preprint (remote): https://drive.google.com/file/d/1-_rq9KOybtksWPod0lac1LDwlNd0Opkt/view
- Code: https://github.com/aparis69/Rock-fracturing (MIT)

## What it covers

- Geological fracture types (equidimensional, rhombohedral, polyhedral, tabular)
- Poisson samples → fracture constraints → clustering → convex block SDFs
- Gradient-based warping (triplanar relief) for micro detail
- Cubic tile `C`; **replication operator** and tiling described in paper §5 but **not** in GitHub release

## Neverwhere port

- Code: `src/apps/CliffsGenerationPlayground/rock_fracture/`
- Analysis: [../../neverwhere/cliffs-generation-playground/rock-fracturing-upstream.md](../../neverwhere/cliffs-generation-playground/rock-fracturing-upstream.md)

## Key sections to read

- §3 Overview — pipeline figure (tile → amplify terrain)
- §4 Fracturing — fracture distributions
- §5 Terrain amplification — `fe = max(f, t)`, presence function `e`, strata
