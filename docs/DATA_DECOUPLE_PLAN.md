# План: отвязать форматы данных от Qt и сохранить QML-адаптеры

## Проблема сейчас
Часть “данных” и enum-типов живёт в `src/libs/base_data`, но уже тянет Qt:
- пример: `src/libs/base_data/maps.h` включает `QObject`, использует `Q_NAMESPACE`/`Q_ENUM_NS`.

Для **runtime-игры без Qt** это блокер: любую попытку “просто подключить base_data” будет тянуть Qt как транзитивную зависимость.

## Цель
Сделать так, чтобы:
- **runtime** мог линковаться с “data layer” без Qt (чистый C++20);
- **editor** продолжал удобно работать с QML (enum в QML, модели, сигналы), но через адаптеры.

## Предложенное разбиение

### 1) `game_data` (или `nw_data`) — чистые данные
Новый target (папка) например: `src/libs/game_data/`

**Содержимое**:
- чистые enum’ы: `enum class LayerType`, `enum class GameObjectType` (без `Q_NAMESPACE`);
- структуры сериализации JSON: `Chapter`, `Map`, `Layer`, `GameObject` и вложенные данные;
- сериализация на `nlohmann::json` (как сейчас), filesystem paths, uuid и т.д.

**Переезд**:
- `src/libs/base_data/chapters.{h,cpp}` → в `game_data`;
- `src/libs/base_data/maps.{h,cpp}` → в `game_data`;
- всё, что не требует Qt, уходит в `game_data`.

### 2) `editor_qt_adapters` — мост в Qt/QML
Новый target: `src/libs/editor_qt_adapters/` (или держать внутри `core/`).

**Содержимое**:
- Qt-экспорт enum’ов в QML (`Q_NAMESPACE`, `Q_ENUM_NS`) в отдельных заголовках:
  - `QtLayerTypes.h` ↔ конвертация к `game_data::LayerType`;
  - `QtGameObjectTypes.h` ↔ конвертация к `game_data::GameObjectType`.
- Конвертеры:
  - `toQt(game_data::LayerType)` / `fromQt(LayerTypes::Type)` и т.п.
- Модели/адаптеры, которые сейчас уже живут в `core` (`MapModel`, `LayerModel`, `ChaptersModel`) — остаются Qt-частью и продолжают работать, но читают/пишут через `game_data`.

## Порядок миграции (минимум боли)
1. **Добавить новый lib `game_data`**, скопировать туда текущие структуры/сериализацию (без ломания существующего).
2. Убрать Qt-зависимость из `base_data`:
   - либо “заморозить” `base_data` и постепенно перестать его использовать;
   - либо сделать `base_data` тонким “реэкспортом” `game_data` (если нужно сохранить include paths).
3. В редакторе заменить использование `LayerTypes::Type`/`GameObjectTypes::Type` на:
   - в UI/QML: Qt enum’ы из `editor_qt_adapters`;
   - в данных: enum’ы `game_data`.
4. Runtime начинает линковать только `game_data` (без Qt).

## Ключевые замечания
- **Не смешивать** Qt-moc макросы и “чистые данные” в одном заголовке.
- Для JSON ключей слоёв:
  - сейчас используется `magic_enum::enum_name(layerType)` (см. `src/libs/base_data/maps.cpp`);
  - после перехода на `enum class` можно оставить `magic_enum` (работает) или завести явный table для стабильности.
- Сейчас `CoreContext` хардкодит путь к ресурсам (`src/libs/core/core_context.cpp`): runtime должен получить отдельный механизм `--data-dir` или relative path, но это отдельный следующий шаг.

