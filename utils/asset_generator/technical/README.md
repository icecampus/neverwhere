# Technical atlas generators (stable)

Эта папка содержит **технические** генераторы атласов ландшафта. Они предназначены для:

- проверки масок/триангуляции;
- отладки тайл-сета (4x6);
- генерации “технических” атласов с простым освещением и стенками.

## Скрипты

- `generate_atlas.py`: базовый генератор (маски + освещение).
- `generate_atlas_ridge.py`: вариант разбиения **Ridge/Hybrid**.
- `generate_atlas_valley.py`: вариант разбиения **Valley/Concave**.

Маски и пояснения по точкам см. в `../TILE_MASKS.md` (в корне `asset_generator`) — там же описаны различия Valley/Ridge.

## Как запускать

Зависимость: `Pillow`.

```bash
pip install Pillow
```

Примеры:

```bash
python utils/asset_generator/technical/generate_atlas.py technical_atlas.png
python utils/asset_generator/technical/generate_atlas_ridge.py technical_atlas_ridge.png
python utils/asset_generator/technical/generate_atlas_valley.py technical_atlas_valley.png
```

## Важно (что считается “техническим”)

- Нет попыток “релизного” материала/арта.
- Минимум параметров, максимум предсказуемости.
- Любые эксперименты с материалами/краями/декалями делаем **в отдельной ветке**: `../material`.

