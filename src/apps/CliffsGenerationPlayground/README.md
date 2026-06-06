# Cliffs Generation Playground

Песочница для экспериментов с **процедурной геометрией скальных стен**. Здесь мы учимся генерировать и визуализировать скалы и обрывы — без UI редактора, с фокусом на чистую геометрию и её качество.

## Цель

Научиться получать **красивые, правильно слитые скальные стены**: блоки и трещины должны выглядеть естественно, без артефактов стыков и с убедительной формой рельефа.

## Референс

Материалы (PDF, gap-анализ, roadmap, open-source) — в корне репозитория: **[reference/](../../../reference/README.md)**

- [Paris 2020 TVC — Implicit Blocks](../../../reference/papers/paris-2020-implicit-blocks/)
- [Paris 2019 TOG — Terrain Amplification](../../../reference/papers/paris-2019-terrain-amplification/)
- [Peytavie 2009 — Arches](../../../reference/papers/peytavie-2009-arches/)
- [aparis69/Rock-fracturing](https://github.com/aparis69/Rock-fracturing) — upstream MIT code
- [Neverwhere notes](../../../reference/neverwhere/cliffs-generation-playground/) — gap analysis & scene roadmap

Этот playground адаптирует Paris 2020 в контексте Neverwhere (Sokol, ImGui, собственный рендер).

## Сборка и запуск

CMake-таргет: `CliffsGenerationPlayground` (папка `src/apps/CliffsGenerationPlayground`).

Windows: сгенерировать решение через `generate_vs.bat`, собрать таргет из `_intermediate_64\Neverwhere.sln` или:

```bat
cmake --build --preset debug --target CliffsGenerationPlayground
```

## Статус

Активная экспериментальная площадка; API и сценарии могут меняться по мере освоения алгоритма.

### Режимы генерации

| Режим | Описание |
|-------|----------|
| **Single tile** | Один cubic tile (upstream workflow) — Poisson → fractures → SDF → MC |
| **Cliff scene** | Macro-terrain `f` (плато + vertical wall) + опционально replication `t` → `fe = max(f,t)` |

Cliff scene: без **Enable block replication** — только macro mesh; с replication — blocky стены по Paris 2020 §5 (slope presence v1).

Smoke-test: `--smoke-test` (без UI).

Export: кнопка **Export OBJ** → `export_cliffs_mesh.obj`.
