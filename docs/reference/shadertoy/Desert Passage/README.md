# Desert Passage

- Ссылка: https://www.shadertoy.com/view/XtyGzc
- Автор: Shane
- Теги: raymarch, cellular, tile, cave, rock, desert, passage
- Источник: web.archive.org snapshot 2019-11-10 (совпадает с живой версией — сверено с пастой пользователя, `diff -w` чистый)

A cave-like desert passageway: синусоидальная пещерная масса вырезается кастомным
cellular-алгоритмом (псевдо-Voronoi без циклов — `cellTile`), крупные слои реймарчатся,
мелкие — бампятся. Трёхслойная пыль поверх.

## Файлы

- `Image.glsl` — весь шейдер одним пассом (raymarch каньона, ~22 КБ).
- `textures/iChannel0.jpg` — sandstone-текстура для `tex3D`/bump (mipmap/repeat, 1024², из web.archive.org).
