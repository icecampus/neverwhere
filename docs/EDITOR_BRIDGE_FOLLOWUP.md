## Follow-up: подключение Epic Map Editor к `game_data` (без Qt в данных)

Цель: чтобы **EpicGameRuntime** и **Epic Map Editor** использовали один и тот же слой данных, при этом Qt оставался только в editor-адаптерах и UI.

### 1) Вынести Qt из `base_data` и `math`
- **Проблема**: сейчас `src/libs/base_data/*` и даже `src/libs/math/ivec.h` тянут Qt (`QObject`, `Q_NAMESPACE`, `Q_GADGET`).
- **Решение**:
  - постепенно перестать использовать `base_data` как “источник истины”;
  - оставить `base_data` как legacy/мост на время миграции, либо удалить позже.

### 2) Слой данных `game_data` как источник истины
- `src/libs/game_data` содержит:
  - JSON-совместимые структуры (chapters/maps/balance/resources)
  - enum’ы (`LayerType`, `GameObjectType`) и правила сериализации
  - файловую индексацию ресурсов/ассетов (asset index)

### 3) Editor-слой: `editor_qt_adapters`
Создать новый target, например `src/libs/editor_qt_adapters`:
- Qt-экспорт enum’ов в QML (`Q_NAMESPACE`, `Q_ENUM_NS`)
- конвертеры:
  - `toQt(game_data::LayerType)` / `fromQt(LayerTypes::Type)`
  - `toQt(game_data::GameObjectType)` / `fromQt(...)`
- “view models” и QObject-обёртки для UI (там, где сейчас `core/map/MapModel`, `LayerModel`)

### 4) Смена моделей карты в редакторе без ломания UI
Эволюционный путь:
1. `MapModel::load()` начинает грузить JSON через `game_data::Map::load()` вместо `BaseData::Map::load()`.
2. Внутреннее состояние `MapModel` хранит `game_data::Map` (или snapshot на `immer`).
3. `LayerModel` остаётся `QAbstractListModel`, но отдаёт элементы, построенные из `game_data` (адаптер).
4. Запись обратно в JSON: `MapModel::save()` сериализует через `game_data` (с сохранением формата).

### 5) Play-Test host и фикстуры
Когда редактор запускает runtime:
- передавать текущую карту/главу “как есть” (или через временный map.json)
- для недостающих системных данных подставлять fixture-реализации:
  - `IProfileStore`, `IInventoryStore`, `ISaveGameStore`
- в будущем: сделать пресеты фикстур (JSON-шаблоны) для воспроизводимых тест-сценариев.

