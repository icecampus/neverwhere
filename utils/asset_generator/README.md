# Asset Generator Utilities

Набор утилит для генерации **атласов ландшафта** (сетка **4x6**) и связанных ассетов.

В репозитории генераторы разделены на две независимые ветки, чтобы **стабильное** не ломалось экспериментами:

- **Technical (stable)**: техническая генерация (маски/триангуляция/освещение).

## Куда пишутся временные файлы

По умолчанию все прогоны пишут результаты в:

- `_intermediate_64/_asset_generator/...`

Эта папка уже в `.gitignore`, поэтому промежуточные данные не попадают в git.

## Publish (обновление ассетов)

После генерации скрипты **публикуют** результат в `resources/assets/landscape`:

- **Technical** перезаписывает существующие паки:
  - `resources/assets/landscape/TechnicalGrassRidge/atlas.png`
  - `resources/assets/landscape/TechnicalGrassValley/atlas.png`
  - и перегенерирует `thumbnail.png` из `tileIndex=0`.

Чтобы сгенерировать **только temp** и не трогать `resources/`, используйте `--no-publish`.

## Где читать подробнее

- **Technical**: `utils/asset_generator/technical/README.md`
  - Маски и пояснения: `utils/asset_generator/technical/TILE_MASKS.md`

