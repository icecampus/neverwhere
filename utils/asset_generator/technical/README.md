# Technical atlas generators (stable)

Эта папка содержит **технические** генераторы атласов ландшафта. Они предназначены для:

- проверки масок/триангуляции;
- отладки тайл-сета (4x6);
- генерации “технических” атласов с простым освещением и стенками.

## Скрипты

- `generate_atlas_ridge.py`: вариант разбиения **Ridge/Hybrid**.
- `generate_atlas_valley.py`: вариант разбиения **Valley/Concave**.

Маски и пояснения по точкам см. в `TILE_MASKS.md` — там же описаны различия Valley/Ridge.

## Как запускать

Зависимость: `Pillow`.

```bash
pip install Pillow
```

## Новые правила (workflow)

- **Временные файлы** и любые промежуточные результаты живут в `_intermediate_64/_asset_generator/...` (папка уже в `.gitignore`).
- Каждый запуск **по умолчанию**:
  - пишет результат в `_intermediate_64/_asset_generator/...`
  - **публикует** его в `resources/assets/landscape/TechnicalGrassRidge|TechnicalGrassValley`
  - и **перегенерирует** `thumbnail.png` из `tileIndex=0` (верхний левый тайл 4x6).
- Если нужно получить только временный файл без публикации — используйте `--no-publish`.

## Примеры

```bash
python utils/asset_generator/technical/generate_atlas_ridge.py --run-id 20260204-1200_test
python utils/asset_generator/technical/generate_atlas_valley.py --run-id 20260204-1200_test

# temp-only (без обновления assets)
python utils/asset_generator/technical/generate_atlas_ridge.py --no-publish
```

## Важно (что считается “техническим”)

- Нет попыток “релизного” материала/арта.
- Минимум параметров, максимум предсказуемости.
- Любые эксперименты с материалами/краями/декалями делаем **в отдельной ветке**: `../material`.

