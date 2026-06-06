# aparis69 / Implicit-Volumetric-Terrains

**Repository:** https://github.com/aparis69/Implicit-Volumetric-Terrains  
**Fork (team):** https://github.com/Arches-Team/Implicit-Volumetric-Terrains  
**License:** MIT  
**Paper:** [Paris 2019 TOG](../papers/paris-2019-terrain-amplification/)

## Summary

Console app exporting **OBJ scenes** from *Terrain Amplification with Implicit 3D Features*. Implicit construction trees, landform grammars, erosion/percolation-guided features: **arches, overhangs, karst, hoodoos**, stratified cliffs.

## Included in upstream

- VS2022 / G++ build, sample OBJs in `Objs/`
- Core implicit terrain + amplification pipeline (recoded)

## Not in upstream

- Optimized marching cubes
- Hoodoos shape-grammar growth

## Neverwhere relevance

**Primary candidate to port for macro terrain `f`** before combining with Rock-fracturing blocks (`fe = max(f, t)`).

Roadmap: [../../ports/aparis69-rock-fracturing-ref/SCENE_EVOLUTION_ROADMAP.md](../../ports/aparis69-rock-fracturing-ref/SCENE_EVOLUTION_ROADMAP.md)
