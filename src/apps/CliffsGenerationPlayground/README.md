# Cliffs Generation Playground

Песочница для экспериментов с **процедурной геометрией скальных стен**. Здесь мы учимся генерировать и визуализировать скалы и обрывы — без UI редактора, с фокусом на чистую геометрию и её качество.

## Цель

Научиться получать **красивые, правильно слитые скальные стены**: блоки и трещины должны выглядеть естественно, без артефактов стыков и с убедительной формой рельефа.

## Референс

За основу взят подход из статьи *Modeling Rocky Scenery using Implicit Blocks* (Paris et al., TVC 2020). Исходный исследовательский код:

- [aparis69/Rock-fracturing](https://github.com/aparis69/Rock-fracturing) — implicit blocks, SDF, marching cubes, процедурное дробление камня.

Этот playground адаптирует и изучает те же идеи в контексте Neverwhere (Sokol, ImGui, собственный рендер).

## Сборка и запуск

CMake-таргет: `CliffsGenerationPlayground` (папка `src/apps/CliffsGenerationPlayground`).

Windows: сгенерировать решение через `generate_vs.bat`, собрать таргет из `_intermediate_64\Neverwhere.sln` или:

```bat
cmake --build --preset debug --target CliffsGenerationPlayground
```

## Статус

Активная экспериментальная площадка; API и сценарии могут меняться по мере освоения алгоритма.
