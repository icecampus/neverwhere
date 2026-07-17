# Vertex-Centric Landscape

Модель данных и пайплайн ландшафта в Neverwhere. Описывает, как ландшафт
представлен через **вершины** (corners/nodes), почему выбран такой подход, и как
он реализован в коде — от генератора «чаши» до 3D-меша.

> Связанные документы:
> - [`TILE_RESOLUTION_ANALYSIS.md`](./TILE_RESOLUTION_ANALYSIS.md) — сравнение
>   vertex-centric с blob 47 (почему выбрано именно это).
> - [`../utils/asset_generator/technical/TILE_MASKS.md`](../utils/asset_generator/technical/TILE_MASKS.md)
>   — маски точек для генерации **текстурных** атласов (9 точек на тайл, это
>   отдельная задача, не путать с моделью данных ниже).

---

## TL;DR

Ландшафт хранится не как тайлы, а как **дискретные уровни высоты на вершинах**
(node levels). Каждая ячейка (cell) получает тип поверхности, вычисляя 4-битную
маску из её 4 угловых вершин. Эта маска через классификацию Marching Squares даёт
один из **15 типов тайла + Unknown (всего 16 значений)**. То же разрешение, что у
классического blob 47, достигается уменьшением ячейки в 2 раза — без усложнения
модели данных.

**Единый источник правды о форме ландшафта — вершины (nodes), а не тайлы.**

---

## 1. Концепция: vertex-centric vs tile-centric

Есть два способа представлять 2D-ландшафт на сетке.

### Tile-centric (он же blob / Wang tiles)

Данные хранятся «на тайлах»: центр тайла + середины рёбер + углы, сетка
`(2W+1) × (2H+1)`. Каждый тайл описывается **8 битами** (4 ребра + 4 угла).
Полный набор — 2⁸ = 256 комбинаций; blob-ограничение отсекает 209, остаётся 47
уникальных тайлов. Геометрия/спрайт **выбирается из атласа**.

### Vertex-centric (наш подход)

Данные хранятся **только на вершинах** (углах ячеек), сетка `(W+1) × (H+1)`.
Каждая ячейка определяется конфигурацией своих **4 вершин** → 4 бита → 2⁴ = 16
значений (15 типов + Unknown). Высоты непрерывные (не бинарные), поэтому форма
получается **процедурной геометрией**, а не выбором из атласа.

| | Blob 47 (tile-centric) | **Vertex-Centric (Neverwhere)** |
|---|---|---|
| Где данные | центры + midpoints + corners | **только corners (вершины)** |
| Бит на ячейку | 8 | **4** |
| Кол-во типов | 47 | **16 (15 + Unknown)** |
| Минимальный элемент | 1 тайл | 2×2 тайла (одна вершина влияет на 4 ячейки) |
| Геометрия | выбор из атласа | **генерация из высот вершин** |

**Почему vertex-centric:** 15 типов вместо 47 — проще логика, меньше арта,
процедурная геометрия вместо атласов. Чтобы не терять в разрешении по сравнению с
blob 47 (у которого midpoints рёбер), размер ячейки уменьшается в 2 раза по X/Y —
тогда вершины мелкой сетки естественным образом становятся теми же midpoints.

---

## 2. Модель данных: `LandscapeLevelGrid`

Живёт в `src/libs/landscape_core/include/landscape_core/landscape_logic.h` — это
чистая библиотека без Qt, используется и редактором, и рантаймом.

```cpp
struct LandscapeLevelGrid {
    int width = 0;            // кол-во ячеек по X
    int height = 0;           // кол-во ячеек по Y
    int levelCount = 1;       // дискретных уровней высоты (напр. 4)
    float levelHeight = 1.0f; // высота одного уровня в мировых единицах

    std::vector<std::uint8_t>        cellLevels;  // [width * height]
    std::vector<std::uint8_t>        nodeLevels;  // [(width+1) * (height+1)]
    std::vector<LandscapeZone>       zones;       // [width * height], по ячейкам
    // ...
};
```

Ключевое:
- **`cellLevels`** — уровень высоты для каждой **ячейки** (cell). Индексируется
  `y * width + x`. Это основная сетка данных.
- **`nodeLevels`** — уровень высоты для каждой **вершины** (node). Индексируется
  `y * (width+1) + x`. Размер `(W+1)×(H+1)` — вершины стоят по углам ячеек.
- **`zones`** — семантическая зона ячейки (`Lowland`, `Clearing`, `Slope`,
  `HighGround`, `Hill`). Нужно для пост-обработки и арта, не для масок тайлов.

> **Важно:** именно `nodeLevels` — «истина последней инстанции» о форме ландшафта.
> Тип тайла ячейки всегда вычисляется из её 4 вершин, а не хранится явно.

### Откуда берутся node levels

В процедурном генераторе (`generateLandscapeBowl`) ячейки заполняются первичными
уровнями из функции высот, а узлы **выводятся из ячеек** — `deriveNodeLevelsFromCells()`:
уровень узла = `max` уровней 4 ячеек, соприкасающихся в этой вершине:

```cpp
for each node (x, y):
    level = max(cellLevelAt(x-1, y-1), cellLevelAt(x, y-1),
                cellLevelAt(x-1, y),   cellLevelAt(x, y))   // границы → вне сетки
    nodeLevels[nodeIndex(x,y)] = level
```

Почему `max`, а не, скажем, среднее: ландшафт — это дискретные **ступени**
(террасы), и вершина на стыке двух ячеек разного уровня относится к верхнему
уровню, чтобы поверхность верхней ячейки не «проваливалась» на её краю.

### В редакторе — наоборот: узлы рисуются кистью

В редакторе пользователь рисует **точки (узлы)**, а типы тайлов ячеек
**вычисляются** из них (см. раздел 6). Там первичны узлы, ячейки — производное.
В обоих направлениях контракт один: 4 узла ячейки → её тип.

---

## 3. 4-битная маска и 16 типов тайлов

Тип поверхности ячейки определяется маской «какие из 4 вершин достигают данного
уровня высоты». Это классический **Marching Squares** над бинарным полем.

### Маска

`std::array<bool, 4>` в порядке слотов **[Left, Up, Right, Down]** — против
часовой стрелки от левой вершины ромба. (Порядок слотов задаётся топологией —
см. раздел 6.)

```cpp
std::array<bool, 4> cellNodeMaskAtLevel(const LandscapeLevelGrid& grid,
                                        int x, int y, std::uint8_t minLevel) {
    return {
        grid.nodeLevelAt(x,     y + 1) >= minLevel,  // Left
        grid.nodeLevelAt(x,     y    ) >= minLevel,  // Up
        grid.nodeLevelAt(x + 1, y    ) >= minLevel,  // Right
        grid.nodeLevelAt(x + 1, y + 1) >= minLevel,  // Down
    };
}
```

### Все 16 значений (2⁴)

Реализовано в `nodeMaskToTileType` (`landscape_logic.cpp`). `tileTypeToNodeMask`
— точная обратная функция.

| Маска `[L,U,R,D]` | Тип | Семантика |
|---|---|---|
| `0000` | `Unknown` | нет поверхности (дыра/воздух) |
| `1111` | `Full` | сплошная плоскость |
| `0001` | `DownCorner` | один угол (D) |
| `0010` | `RightCorner` | один угол (R) |
| `0100` | `UpCorner` | один угол (U) |
| `1000` | `LeftCorner` | один угол (L) |
| `0011` | `RightDownLine` | ребро R-D (стена с двух сторон) |
| `0110` | `RightUpLine` | ребро R-U |
| `1001` | `LeftDownLine` | ребро L-D |
| `1100` | `LeftUpLine` | ребро L-U |
| `0101` | `UpAndDownCorners` | два противоположных угла (седло/хребет) |
| `1010` | `LeftRightCorners` | два противоположных угла |
| `0111` | `LeftLack` | 3 угла занято, не хватает L |
| `1011` | `UpLack` | не хватает U |
| `1101` | `RightLack` | не хватает R |
| `1110` | `DownLack` | не хватает D |

Итого ровно 16 = 2⁴. Категории:
- **Corner** (4) — 1 бит: один угол.
- **Line** (4) — 2 соседних бита: стена-перемычка.
- **Opposite** (2) — 2 противоположных бита: седло/хребет (`UpAndDown`/`LeftRight`).
- **Lack** (4) — 3 бита: почти полный, не хватает одного угла.
- **Full** / **Unknown** — все / нет.

> Это **аналитическая** классификация: она говорит только о форме в плане. Как
> именно тайл будет выглядеть в 3D (складка, пик, седло) — задача mesh-слоя
> (`landscape_mesh`) и атласов (`TILE_MASKS.md`).

---

## 4. Пайплайн генерации «чаши» (`generateLandscapeBowl`)

Процедурный генератор в `landscape_logic.cpp`. Производит готовый
`LandscapeLevelGrid` из настроек. Используется в playground'ах и тестах;
в редакторе узлы пока рисуются пользователем.

```
LandscapeBowlSettings
        │
        ▼
  sanitize(settings)            ── ограничить параметры в разумные диапазоны
        │
        ▼
  field height → cellLevels     ── levelForSample() по центру каждой ячейки
        │                          (clearing + кольцо highGround + холмы + шум)
        ▼
  enforcePyramidLevelSpacing    ── соседи не могут отличаться >1 уровня
        │
        ▼
  removeThinLevelFeatures       ── сгладить 1×1 пики/впадины (до 8 проходов)
        │
        ▼
  deriveNodeLevelsFromCells     ── node = max(4 соседних cell)
        │
        ▼
  LandscapeLevelGrid + BowlGenerationStats
```

### Зачем пост-обработка уровней

Два обязательных прохода после первичной функции высот — без них генератор
нарушил бы предположения mesh-слоя:

1. **`enforcePyramidLevelSpacing`** — гарантирует `maxAdjacentLevelDelta ≤ 1`
   (соседние ячейки отличаются не более чем на 1 уровень). Сначала BFS от
   `Clearing`-ячеек считает расстояние до поляны, затем запрещает перепады
   больше 1 между соседями. Иначе возникли бы отвесные стены высотой в 2+
   уровня, которые mesh-композер не умеет корректно стыковать.

2. **`removeThinLevelFeatures`** — убирает одиночные пики/впадины (ячейка, у
   которой ≥3 из 4 ортогональных соседей ниже/выше). До 8 итеративных
   проходов. Без этого вокруг одного 1×1 «шпиля» сходятся 4 стены и 4 угловых
   скоса, давая вырожденный «вихрь» из крошечных разнонаправленных фасетов.

Оба инварианта проверяются тестами
(`BowlGeneratorKeepsClearingLowAndUsesDiscreteLevels`,
`BowlGeneratorStacksHighGroundAsPyramid`) — `maxAdjacentLevelDelta` не может
превысить 1.

---

## 5. От вершин к 3D-мешу

Mesh-слой — `src/libs/landscape_mesh` (`landscape_mesh.h/.cpp`). Он берёт
`LandscapeLevelGrid` и строит список `MeshQuad` (вершины + нормали + цвет +
метаданные стены). Главные точки входа:

- `composeLandscapeMesh(grid, settings)` — полный меш из level-grid.
- `composeSolidMaskMesh(request, settings)` — то же, но из произвольной
  `SolidMaskGrid` (для cutout'ов, тестов, скал).
- `TileMeshCatalog` — кэш мешей по `LandscapeTileKey`: одинаковые тайлы
  переиспользуются (`reusedCount`), уникальные генерируются один раз.

Композер обходит ячейки, для каждой:
1. Определяет `LandscapeTileType` (через те же 4-битные маски вершин).
2. Строит top-surface (плоскость верха ячейки) + wall-quads для обрывов по
   границам с более низкими соседями.
3. Скашивает углы (`cornerBevel`) — вместо прямого угла обрыва срез, чтобы
   избежать «пиксельного» вида.
4. Валидирует результат: `SeamValidation` (нет щелей между ячейками),
   `NormalOrientationStats` (нормали стен смотрят наружу).

Контракт mesh-слоя с vertex-centric моделью: **он ничего не знает о тайлах как о
данных**. Тип тайла — всегда вычисляемое свойство ячейки; меш опирается на
уровни вершин и ячеек.

---

## 6. Контракт: редактор и рантайм говорят на одном языке

Vertex-centric модель описана **дважды** — в Qt-редакторе и в чистой библиотеке
`landscape_core`. Они обязаны совпадать, иначе тайл в редакторе и тайл в
runtime/3D будут разными.

### Редактор (Qt): `TileSet` + `DiamondTiledLandscape`

`src/libs/core/topology/topology_common.h`:
```cpp
struct TileSet {
    enum TileType { Unknown, Full, RightCorner, /* ... 16 значений */ };
    using NeighboursNodeMask = std::array<bool, 4>;
    std::map<NeighboursNodeMask, TileType> tileset;        // маска → тип
    std::map<TileType, NeighboursNodeMask> tilename2mask;  // тип → маска
};
```

`src/libs/core/topology/diamond_tiled_landscape.cpp`:
```cpp
// 4 угловые вершины ячейки cellPosition в порядке слотов [Left, Up, Right, Down]
ModeNeighbours getNeighboursNodeForCell(const math::ivec2& cellPosition) {
    return { cellPosition + (0,1),   // slot 0: Left
             cellPosition + (0,0),   // slot 1: Up
             cellPosition + (1,0),   // slot 2: Right
             cellPosition + (1,1) }; // slot 3: Down
}
```

Пользователь рисует узлы (vertices) кистью; для затронутых ячеек читаются 4
узла, строится маска, `TileSet::tileset[mask]` даёт тип тайла → в `LayerModel`
кладётся `Landscape` с нужным `tileIndex`.

### Рантайм/3D: `landscape_core::nodeMaskToTileType`

Та же таблица 16 масок → 16 типов, но в чистой (без Qt) библиотеке, чтобы её
можно было использовать в `EpicGameRuntime` и в `landscape_mesh`.

### Кто гарантирует совпадение — тест

`src/tests/landscape/landscape_test.cpp`:
```cpp
TEST(LandscapePipelineTest, SharedTileResolverMatchesTileSetMasks) {
    TileSet tileSet;
    for (const auto& [mask, tileType] : tileSet.tileset) {
        const auto sharedType = landscape_core::nodeMaskToTileType(mask);
        EXPECT_EQ((int)sharedType, (int)tileType)
            << "Shared no-Qt tile resolver must preserve editor TileSet mask semantics";
    }
}
```

Если меняется таблица масок в любом из двух мест — тест падает. **Менять порядок
слотов или таблицу масок нужно одновременно в трёх местах:** `TileSet`
(редактор), `nodeMaskToTileType`/`tileTypeToNodeMask` (`landscape_core`) и в
`getNeighboursNodeForCell` (геометрия слотов).

---

## 7. Геометрия в редакторе: порядок слотов

Порядок слотов `[Left, Up, Right, Down]` — не произвольный, он зафиксирован
геометрией ромба и **совпадает** с `landscape_core`. Узел `(nx, ny)`
геометрически стоит в Up-угле ячейки `(nx, ny)` (см. комментарий в
`diamond_tiled_landscape.cpp`):

```
            Up (slot 1)
             ●
            / \
   Left ●─── cell ───● Right      (ромб, против часовой от левой вершины)
            \ /
             ●
           Down (slot 3)
```

Для ячейки `(cx, cy)` её 4 узла:
- Left  → `(cx,   cy+1)`
- Up    → `(cx,   cy)`
- Right → `(cx+1, cy)`
- Down  → `(cx+1, cy+1)`

Имена тайлов (`UpCorner`, `LeftLack`…) описывают, **какой геометрический угол**
несёт бит. Например `UpCorner` = маска `0100` = единственный бит в слоте Up.

---

## 8. Где что искать

| Что | Файл |
|---|---|
| Модель данных `LandscapeLevelGrid` | `src/libs/landscape_core/include/landscape_core/landscape_logic.h` |
| 16 масок → тайлы (`nodeMaskToTileType`) | `src/libs/landscape_core/src/landscape_logic.cpp` |
| Генератор «чаши» + пост-обработка | там же (`generateLandscapeBowl`, `enforcePyramidLevelSpacing`, `removeThinLevelFeatures`, `deriveNodeLevelsFromCells`) |
| Mesh-композер (вершины → 3D) | `src/libs/landscape_mesh/include/landscape_mesh/landscape_mesh.h` |
| Editor tileset + slot order | `src/libs/core/topology/topology_common.h`, `src/libs/core/topology/diamond_tiled_landscape.{h,cpp}` |
| Тесты (контракт + инварианты) | `src/tests/landscape/landscape_test.cpp` |
| Тени от солнца (поверх level-grid) | `src/libs/landscape_core/include/landscape_core/sun_shadow.h` |
| Playground'ы (визуальная проверка) | `src/apps/Landscape3dPlayground/`, `src/apps/MeshGenerationPlayground/` |

---

## 9. Бинарный 3D-прототип: `Landscape3dPlayground`

`Landscape3dPlayground` — рабочая визуальная проверка vertex-centric контракта
для одного перехода `0 → 1`. Он не хранит тайлы: единственное авторское
состояние — бинарный узел. Для каждой ячейки вызывается
`landscape_core::nodeMaskToTileType`, после чего выбирается один из 16
`LandscapeCellTemplate`.

Шаблон состоит из:
- плоскости lowland на `y=0`;
- плоского фрагмента highground на `y=levelHeight`;
- только вертикальных cliff-панелей по Marching Squares-контуру.

Для двух противоположных high-углов генерируются два независимых фрагмента
highground — центральная вершина и промежуточный уровень не вводятся. Внешняя
граница сетки заблокирована в low, поэтому контур highground всегда замкнут.

Кисть рисует и стирает именно узлы; изменение узла обновляет четыре прилежащие
ячейки. Рендер использует `render_core::MeshPreviewRenderer`, тот же
offscreen-production-preview путь, что и `MeshGenerationPlayground`.
`Landscape3dPlayground.exe --smoke` проверяет полноту 16 шаблонов, отсутствие
наклонных top-поверхностей и контракт кисти.

---

## 10. Что концепт / чего пока нет

- **Непрерывные высоты.** Сейчас `nodeLevels` — дискретные `uint8_t` (ступени).
  В анализе (`TILE_RESOLUTION_ANALYSIS.md`) упоминается «непрерывные высоты →
  континуум форм», но в коде уровни квантованы. Mesh-слой частично компенсирует
  это фасетками (`terraceSteps`, `cornerBevel`), но непрерывного рельефа нет.
- **Слои высоты > 1 активного.** Модель поддерживает `levelCount` до 8, но
  многослойный ландшафт (несколько независимых полей высоты) — пока концепт
  (`ROADMAP_VISION.md`, pillar «Advanced Landscape»).
- **Рисование узлов в основном редакторе как первичной операции.** Прототип
  `Landscape3dPlayground` уже даёт этот UX для бинарного рельефа; основной
  редактор пока частично завязан на выбор 2D-тайлов.
