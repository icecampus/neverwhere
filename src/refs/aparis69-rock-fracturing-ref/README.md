# aparis69-rock-fracturing-ref

Референс-порт [aparis69/Rock-fracturing](https://github.com/aparis69/Rock-fracturing) — алгоритма из статьи *Modeling Rocky Scenery using Implicit Blocks* (Paris et al., TVC 2020). Standalone-приложение для изучения и визуализации **implicit blocks, SDF, marching cubes и процедурного дробления камня** в контексте Neverwhere (Sokol, ImGui).

## Цель

Научиться получать **красивые, правильно слитые скальные стены**: блоки и трещины должны выглядеть естественно, без артефактов стыков и с убедительной формой рельефа.

## Upstream

- **Repository:** [aparis69/Rock-fracturing](https://github.com/aparis69/Rock-fracturing) — implicit blocks, SDF, marching cubes, процедурное дробление камня.
- **Paper:** Paris et al., *Modeling Rocky Scenery using Implicit Blocks*, The Visual Computer, 2020

**Сравнение с upstream:** [references/rock-fracturing-upstream.md](./references/rock-fracturing-upstream.md) — карта файлов, что портировано, чего нет в оригинале и в paper. Метаданные репозитория: [references/upstream.meta.json](./references/upstream.meta.json).

## Сборка и запуск

CMake-таргет: `aparis69-rock-fracturing-ref` (папка `src/refs/aparis69-rock-fracturing-ref`).

Windows: сгенерировать решение через `generate_vs.bat`, собрать таргет из `_intermediate_64\Neverwhere.sln` или:

```bat
cmake --build --preset debug --target aparis69-rock-fracturing-ref
```

## Статус

Активный референс-порт; API и сценарии могут меняться по мере освоения алгоритма.
