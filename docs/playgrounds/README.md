# Плейграунды и прототипы

Стенды живут в `src/landscape_playgrounds/` (ландшафтные, таргет = имя папки, без суффикса `Playground`) и `src/apps/*Playground` (остальные). Общие правила — в `AGENTS.md` → «Тестирование и прототипы». У каждого стенда есть `--smoke` (CPU-сценарий с `TEST PASS`/`TEST FAIL`) и, как правило, `--shot=<png>` для скриншотов; на Linux скриншоты снимать под `xvfb-run`, на macOS с Metal `--shot` нечем читать — переконфигурировать с GLCORE-бэкендом (opt-in флаги вроде `-DSDFGL_FORCE_GLCORE=ON`).

| Документ | Стенды |
|---|---|
| `sdf_landscapes.md` | SDFGeneratedLandscape (Texture 2D, учебные Box/Circle/Mask 3D), SDFWithMaterialLandscape (PBR-lite материал, рельеф), HighgroundWithEffects (стыковка с землёй: AO, shadow map, материалы стыка), Z-буфер плоского iso, HiDPI-грабли sokol_app + ImGui |
| `brep_landscape.md` | B-repGeneratedLandscape — форк `landscape_mesh`, материалы Poly Haven/ambientCG, многоуровневость, bake-деформация, профиль стены, осыпь |
| `stone_generator.md` | StoneGenerator — cut-based камни на CGAL Nef, пары, кластеры, детектор разрывов |
| `stone_cube.md` | StoneCube (voronoi-камень iq), `stone_gen` (SDF → surface nets → бейк), `StoneField` |
| `fence_path.md` | FencePathPlayground — модель забора `fence_core`, 3D-куски из ShapeML |
| `shapeml.md` | ShapemlPlayground + `shapeml_ref` (GPL — только в плейграунде) |
| `shadertoy.md` | Shadertoy — хост демок из `docs/reference/shadertoy` |

Дорога «демка → кубик с материалом → плейграунд → инструмент → редактор» описана в `docs/SDF_TO_MESH_PLAYBOOK.md`.
